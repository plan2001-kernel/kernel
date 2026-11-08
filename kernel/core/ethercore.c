/*
 * Native Ethernet C core (E7.1).
 *
 * This file owns the controller registry, probe/reset lifecycle, stable
 * in-memory Ether lookup and the direct frame TX/RX boundary.  The legacy
 * #l adapter (devether.c) keeps the Netfile/Chan policy, netconsole and
 * frame multiplexing; it calls the operations here instead of owning the
 * hardware registry itself.
 *
 * The native path deliberately does not use Chan, Qid, Dirtab or Netfile.
 * RX delivery is one explicit per-controller callback; the legacy input
 * multiplexer still receives every frame for compatibility.
 */
#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "pool.h"
#include "ureg.h"
#include "../core/error.h"
#include "../core/netif.h"
#include "../core/etherif.h"

static Ether *etherxx[MaxEther];
static Lock corelock;
static QLock coreqlock;	/* serializes init and explicit probe transitions */
static int coreinited;
static int initcalls;
static int probecalls;

/*
 * Optional legacy #l adapter hooks (devether.c).  A native-only profile
 * leaves them nil and does not link that adapter.
 */
void (*etherlegacyrxhook)(Ether*, Block*);
void (*ethernetifinithook)(Netif*, char*, int, int);

typedef struct Ethercore Ethercore;
struct Ethercore {
	Ether		*ether;
	int		attached;
	QLock		attlock;	/* process-context attach transition */
	Lock		rxlock;
	EtherRxSink	rxsink;
	void		*rxarg;
	int		rxactive;	/* sink calls in flight */
	int		rxunregistering;
	Rendez		rxr;
	uvlong		txpackets;
	uvlong		rxpackets;
	uvlong		rxdropped;
};

static Ethercore cores[MaxEther];

static struct {
	char*	type;
	int	(*reset)(Ether*);
} cards[MaxEther+1];

static Ether *etherprobe(int cardno, int ctlrno, char *conf);

int
ethercore_initcalls(void)
{
	return initcalls;
}

int
ethercore_probecalls(void)
{
	return probecalls;
}

static Ethercore*
corestate(Ether *ether)
{
	int ctlrno;

	if(ether == nil)
		return nil;
	ctlrno = ether->ctlrno;
	if(ctlrno < 0 || ctlrno >= MaxEther)
		return nil;
	if(etherxx[ctlrno] != ether)
		return nil;
	return &cores[ctlrno];
}

static void
coreset(Ether *ether)
{
	Ethercore *cs;

	if(ether == nil)
		return;
	cs = &cores[ether->ctlrno];
	cs->ether = ether;
	cs->attached = 0;
	cs->rxsink = nil;
	cs->rxarg = nil;
	cs->txpackets = 0;
	cs->rxpackets = 0;
	cs->rxdropped = 0;
	etherxx[ether->ctlrno] = ether;
}

void
addethercard(char* t, int (*r)(Ether*))
{
	static int ncard;

	if(ncard == MaxEther)
		panic("too many ether cards");
	cards[ncard].type = t;
	cards[ncard].reset = r;
	ncard++;
}

int
ethercore_oqsize(Ether *ether)
{
	int b, q;

	b = ether->netif.mbps * 2*125;	/* 2ms */
	for(q = 128*1024; q < b; q <<= 1)
		;
	if(mainmem->maxsize / 8 < q)
		q = mainmem->maxsize / 8;
	return q;
}

static Ether*
etherprobe(int cardno, int ctlrno, char *conf)
{
	int i, q;
	Ether *ether;

	probecalls++;
	ether = malloc(sizeof(Ether));
	if(ether == nil){
		print("etherprobe: no memory for Ether\n");
		return nil;
	}
	memset(ether, 0, sizeof(Ether));
	ether->tbdf = BUSUNKNOWN;
	ether->isa.irq = -1;
	ether->ctlrno = ctlrno;
	ether->netif.mbps = 10;
	ether->netif.link = -1;	/* unknown state */
	ether->minmtu = ETHERMINTU;
	ether->maxmtu = ETHERMAXTU;

	if(cardno < 0){
		if(conf != nil){
			kstrdup(&ether->isa.type, conf);
			ether->isa.nopt = tokenize(ether->isa.type, ether->isa.opt, nelem(ether->isa.opt));
			if(ether->isa.nopt < 1)
				goto Nope;
			memmove(&ether->isa.opt[0], &ether->isa.opt[1], --ether->isa.nopt*sizeof(ether->isa.opt[0]));
		} else if(isaconfig("ether", ctlrno, &ether->isa) == 0)
			goto Nope;
		for(i = 0; i < ether->isa.nopt; i++){
			if(strncmp(ether->isa.opt[i], "ea=", 3) == 0){
				if(parseether(ether->ea, &ether->isa.opt[i][3]))
					memset(ether->ea, 0, Eaddrlen);
			}
		}
		for(cardno = 0; cards[cardno].type != nil; cardno++)
			if(cistrcmp(cards[cardno].type, ether->isa.type) == 0)
				break;
	}
	if(cardno >= MaxEther || cards[cardno].type == nil)
		goto Nope;
	snprint(ether->netif.name, sizeof(ether->netif.name), "ether%d", ctlrno);
	if(cards[cardno].reset(ether) < 0){
Nope:
		if(conf != nil) free(ether->isa.type);	/* see kstrdup() above */
		free(ether);
		return nil;
	}
	ether->isa.type = cards[cardno].type;

	print("#l%d: %s: %dMbps port 0x%lluX irq %d ea %E\n",
		ctlrno, ether->isa.type, ether->netif.mbps, (uvlong)ether->isa.port, ether->isa.irq, ether->ea);

	q = ethercore_oqsize(ether);
	if(ether->oq == nil){
		ether->oq = qopen(q, Qmsg|Qkick, (void (*)(void*))ether->transmit, ether);
		if(ether->oq == nil)
			panic("ethercore %s: can't allocate output queue", ether->netif.name);
	} else {
		qsetlimit(ether->oq, q);
	}
	/*
	 * A native-only profile has no Netfile fanout.  Keep the MAC/counter
	 * fields in the embedded Netif but do not allocate the legacy f[]
	 * array or call netif*.c.
	 */
	if(ethernetifinithook != nil)
		ethernetifinithook(&ether->netif, ether->netif.name, Ntypes, 8*MB);
	else {
		ether->netif.nfile = 0;
		ether->netif.f = nil;
		ether->netif.limit = 0;
	}
	ether->netif.alen = Eaddrlen;
	memmove(ether->netif.addr, ether->ea, Eaddrlen);
	memset(ether->netif.bcast, 0xFF, Eaddrlen);

	return ether;
}

/*
 * Probe every configured controller once.  Repeated calls return the
 * existing registry unchanged; they do not run controller reset callbacks
 * again.  A call before any card driver has registered is an error.
 */
int
ethercore_init(void)
{
	Ether *ether;
	int cardno, ctlrno, n;

	qlock(&coreqlock);
	initcalls++;
	ilock(&corelock);
	if(coreinited){
		iunlock(&corelock);
		for(n = 0, ctlrno = 0; ctlrno < MaxEther; ctlrno++)
			if(etherxx[ctlrno] != nil)
				n++;
		qunlock(&coreqlock);
		return n;
	}
	if(cards[0].type == nil){
		iunlock(&corelock);
		qunlock(&coreqlock);
		return EtherErrPreinit;
	}
	coreinited = 1;
	iunlock(&corelock);

	for(ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if((ether = etherprobe(-1, ctlrno, nil)) == nil)
			continue;
		coreset(ether);
	}

	if(getconf("*noetherprobe") == nil){
		cardno = ctlrno = 0;
		while(cards[cardno].type != nil && ctlrno < MaxEther){
			if(etherxx[ctlrno] != nil){
				ctlrno++;
				continue;
			}
			if((ether = etherprobe(cardno, ctlrno, nil)) == nil){
				cardno++;
				continue;
			}
			coreset(ether);
			ctlrno++;
		}
	}

	for(n = 0, ctlrno = 0; ctlrno < MaxEther; ctlrno++)
		if(etherxx[ctlrno] != nil)
			n++;
	qunlock(&coreqlock);
	return n;
}

/*
 * Legacy #l explicit attach with a controller specification.  It probes one
 * controller and adds it to the same registry; a later ethercore_init()
 * will not probe that slot again.
 */
Ether*
ethercore_probe_at(int ctlrno, char *conf)
{
	Ether *ether;

	if(ctlrno < 0 || ctlrno >= MaxEther || conf == nil)
		return nil;
	/*
	 * Serialize explicit probes with ethercore_init so a slot cannot be
	 * filled while a driver reset is in progress.  A controller that has
	 * already run cards[].reset has PAX/IRQ/DMA resources that cannot be
	 * released by freeing only its Ether*, so never probe a live slot.
	 */
	qlock(&coreqlock);
	ilock(&corelock);
	if(etherxx[ctlrno] != nil){
		iunlock(&corelock);
		qunlock(&coreqlock);
		return nil;
	}
	iunlock(&corelock);
	ether = etherprobe(-1, ctlrno, conf);
	if(ether == nil){
		qunlock(&coreqlock);
		return nil;
	}
	ilock(&corelock);
	coreset(ether);
	iunlock(&corelock);
	qunlock(&coreqlock);
	return ether;
}

int
ethercore_ncontroller(void)
{
	int ctlrno, n;

	for(n = 0, ctlrno = 0; ctlrno < MaxEther; ctlrno++)
		if(etherxx[ctlrno] != nil)
			n++;
	return n;
}

Ether*
ethercore_lookup(int ctlrno)
{
	if(ctlrno < 0 || ctlrno >= MaxEther)
		return nil;
	return etherxx[ctlrno];
}

Ether*
ethercore_byindex(int i)
{
	int ctlrno;

	if(i < 0)
		return nil;
	for(ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if(etherxx[ctlrno] == nil)
			continue;
		if(i-- == 0)
			return etherxx[ctlrno];
	}
	return nil;
}

Ether*
ethercore_byname(char *name)
{
	int ctlrno;

	if(name == nil)
		return nil;
	for(ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if(etherxx[ctlrno] != nil && strcmp(etherxx[ctlrno]->netif.name, name) == 0)
			return etherxx[ctlrno];
	}
	return nil;
}

/*
 * Attach (enable rings/queues and start driver workers) exactly once per
 * controller.  Driver attach routines are already idempotent, but the core
 * records the transition so native and legacy callers cannot race a second
 * enable sequence.
 */
int
ethercore_attach(Ether *ether)
{
	Ethercore *cs;

	cs = corestate(ether);
	if(cs == nil)
		return EtherErrNoDevice;
	/*
	 * QLock serializes attach transitions in process context.  A second
	 * caller waits here and then observes attached=1, so "ready" (and any
	 * native sink) cannot be published while rings/workers are still
	 * initializing.  On failure the state remains detached and the next
	 * caller may retry.
	 */
	qlock(&cs->attlock);
	if(cs->attached){
		qunlock(&cs->attlock);
		return EtherErrOk;
	}
	if(waserror()){
		qunlock(&cs->attlock);
		nexterror();
	}
	if(ether->attach != nil)
		ether->attach(ether);
	coherence();
	cs->attached = 1;
	poperror();
	qunlock(&cs->attlock);
	return EtherErrOk;
}

/*
 * Direct native transmit.
 *
 * Contract:
 *  - process context only; never call from interrupt context.
 *  - on EtherErrOk the core consumed bp; on every other result the caller
 *    still owns bp.
 *  - the caller supplies a complete L2 frame (EtherType preserved as is).
 *  - the core copies into the output Queue so a rejected frame cannot be
 *    freed by queue error handling behind the caller's back.  The copy is
 *    owned by the queue, then the original is released on success.
 *  - the queue enqueue is non-blocking: full backpressure is reported as
 *    EtherErrQueue, a down link as EtherErrDown.
 */
int
ethercore_tx(Ether *ether, Block *bp)
{
	Ethercore *cs;
	Block *nb;
	int len;

	if(bp == nil)
		return EtherErrBadArg;
	cs = corestate(ether);
	if(cs == nil)
		return EtherErrNoDevice;

	len = BLEN(bp);
	if(len < ether->minmtu || len > ether->maxmtu)
		return EtherErrBadLen;
	if(!cs->attached || ether->oq == nil)
		return EtherErrNotReady;
	if(ether->netif.link == 0)
		return EtherErrDown;

	nb = allocb(len);
	if(nb == nil)
		return EtherErrNoMem;
	memmove(nb->wp, bp->rp, len);
	nb->wp += len;
	nb->flag |= bp->flag & ~(BINTR|BFREE);

	/*
	 * qpasstry tests capacity and inserts under the same Queue lock, then
	 * runs the Qkick/wakeup path.  It never frees on rejection, so the
	 * caller's Block is untouched on every error return.
	 */
	len = qpasstry(ether->oq, nb);
	if(len == -1){
		freeb(nb);
		return EtherErrClosed;
	}
	if(len == -2){
		freeb(nb);
		return EtherErrQueue;
	}
	if(len <= 0){
		freeb(nb);
		return EtherErrClosed;
	}

	ether->netif.outpackets++;
	cs->txpackets++;
	freeb(bp);
	return EtherErrOk;
}

int
ethercore_rxregister(Ether *ether, EtherRxSink sink, void *arg)
{
	Ethercore *cs;

	cs = corestate(ether);
	if(cs == nil)
		return EtherErrNoDevice;
	if(sink == nil)
		return EtherErrBadArg;
	ilock(&cs->rxlock);
	if(cs->rxsink != nil || cs->rxunregistering){
		iunlock(&cs->rxlock);
		return EtherErrBusy;
	}
	cs->rxsink = sink;
	cs->rxarg = arg;
	iunlock(&cs->rxlock);
	return EtherErrOk;
}

static int
rxactivezero(void *a)
{
	Ethercore *cs = a;

	return cs->rxactive == 0;
}

int
ethercore_rxunregister(Ether *ether, EtherRxSink sink, void *arg)
{
	Ethercore *cs;

	cs = corestate(ether);
	if(cs == nil)
		return EtherErrNoDevice;
	ilock(&cs->rxlock);
	if(cs->rxsink != sink || cs->rxarg != arg){
		iunlock(&cs->rxlock);
		return EtherErrBadArg;
	}
	cs->rxunregistering = 1;
	cs->rxsink = nil;
	cs->rxarg = nil;
	iunlock(&cs->rxlock);

	/*
	 * Wait in process context until an in-flight IRQ/worker callback has
	 * returned.  The callback decrements rxactive and wakes rxr; no lock
	 * is held across the wait, and the IRQ path never sleeps.
	 */
    /* Once detached, finish the callback join even if the closing process
     * receives a note. Abandoning rxunregistering would strand the sink. */
    while(waserror())
        ;
	for(;;){
		ilock(&cs->rxlock);
		if(cs->rxactive == 0){
			cs->rxunregistering = 0;
			iunlock(&cs->rxlock);
            poperror();
			return EtherErrOk;
		}
		iunlock(&cs->rxlock);
		tsleep(&cs->rxr, rxactivezero, cs, 10);
	}
}

void
ethercore_stats(Ether *ether, EtherCoreStats *st)
{
	Ethercore *cs;

	if(st == nil)
		return;
	memset(st, 0, sizeof(*st));
	cs = corestate(ether);
	if(cs == nil)
		return;
	st->attached = cs->attached;
	st->txpackets = cs->txpackets;
	st->rxpackets = cs->rxpackets;
	st->rxdropped = cs->rxdropped;
	st->registered = cs->rxsink != nil;
}

/*
 * Driver receive entry.  A registered native sink receives a private copy;
 * the original always continues through the legacy Netfile/DMAT/multiplexer
 * path, unchanged.  The copy keeps exactly one owner for each Block.
 *
 * Call context is the driver's RX context: VirtIO calls this from rxproc
 * (worker); other inherited drivers may call it from interrupt context.
 * The sink must therefore be interrupt-safe: no sleep, no blocking lock,
 * no allocation that can sleep.  qpass/qpassnolim and freeb are acceptable.
 */
void
etheriq(Ether* ether, Block* bp)
{
	Ethercore *cs;
	EtherRxSink sink;
	void *arg;
	Block *nb;
	int len;

	if(bp == nil)
		return;
	if(ether == nil){
		freeb(bp);
		return;
	}
	cs = corestate(ether);
	if(cs != nil){
		ilock(&cs->rxlock);
		sink = cs->rxsink;
		arg = cs->rxarg;
		if(sink != nil)
			cs->rxactive++;
		iunlock(&cs->rxlock);
		if(sink != nil){
			len = BLEN(bp);
			nb = iallocb(len);
			if(nb == nil)
				cs->rxdropped++;
			else {
				memmove(nb->wp, bp->rp, len);
				nb->wp += len;
				nb->flag |= bp->flag & ~(BINTR|BFREE);
				cs->rxpackets++;
				sink(arg, ether, nb);
			}
			ilock(&cs->rxlock);
			cs->rxactive--;
			wakeup(&cs->rxr);
			iunlock(&cs->rxlock);
		}
	}
	if(etherlegacyrxhook != nil)
		etherlegacyrxhook(ether, bp);
	else
		freeb(bp);
}

static void
etherdrop(void*, Block *b)
{
	freeb(b);
}

void
ethersetspeed(Ether *ether, int mbps)
{
	if(ether->netif.mbps == mbps)
		return;
	ether->netif.mbps = mbps;
	if(mbps <= 0 || ether->netif.f == nil || ether->oq == nil || ether->netif.bypass)
		return;
	qsetlimit(ether->oq, ethercore_oqsize(ether));
}

void
ethersetlink(Ether *ether, int new)
{
	int old = ether->netif.link;

	new = !!new;
	if(old == new)
		return;
	ether->netif.link = new;
	if(ether->netif.f == nil || ether->netif.bypass)
		return;
	memset(ether->mactab, 0, sizeof(ether->mactab));
	qsetbypass(ether->oq, ether->netif.link? nil: etherdrop);

	if(up == nil || !islo())
		return;	/* should not print from interrupt */

	if(ether->netif.link)
		print("#l%d: %s: link up: %dMbps\n",
			ether->ctlrno, ether->isa.type, ether->netif.mbps);
	else if(old > 0)
		print("#l%d: %s: link down\n",
			ether->ctlrno, ether->isa.type);
}

#define POLY 0xedb88320

/* really slow 32 bit crc for ethers */
ulong
ethercrc(uchar *p, int len)
{
	int i, j;
	ulong crc, b;

	crc = 0xffffffff;
	for(i = 0; i < len; i++){
		b = *p++;
		for(j = 0; j < 8; j++){
			crc = (crc>>1) ^ (((crc^b) & 1) ? POLY : 0);
			b >>= 1;
		}
	}
	return crc;
}

void
ethercore_shutdown(void)
{
	Ether *ether;
	int i;

	for(i = 0; i < MaxEther; i++){
		ether = etherxx[i];
		if(ether == nil)
			continue;
		if(ether->shutdown == nil) {
			print("#l%d: no shutdown function\n", i);
			continue;
		}
		(*ether->shutdown)(ether);
	}
}
