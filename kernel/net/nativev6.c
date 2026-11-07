/*
 * E7.2: minimum native Ethernet-to-IPv6 consumer.
 *
 * Reuses the inherited Fs/Proto/Ipifc/Medium/NDP implementation.  The only
 * new pieces are:
 *   - a direct process-context Fs/IPv6 init entrypoint (ipfsinit);
 *   - one IRQ-safe EtherRxSink that copies accepted 0x86DD frames into a
 *     bounded Queue;
 *   - one worker that hands those Blocks to ipiput6 in process context;
 *   - a native Medium.bwrite that frames IPv6 and calls ethercore_tx.
 *
 * This is a test-profile adapter, not a new IP stack and not a userspace
 * networking ABI.  Legacy #l/#I remain linked and are not called here.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"../core/netif.h"
#include	"../core/etherif.h"

#include	"ip.h"
#include	"ipv6.h"
#include	"nativev6.h"
#include "espctl.h"

enum {
	NativeRxq = 64*1024,
};



static QLock nativelk;
static Ipv6Native *nativeone;

static int
workerexited(void *a)
{
	Ipv6Native *ni = a;

	return ni->worker_exited;
}

static void
mac2ll(uchar *ll, uchar *mac)
{
	ll[0] = 0xfe;
	ll[1] = 0x80;
	memset(ll+2, 0, 6);
	ll[8] = mac[0] ^ 0x02;
	ll[9] = mac[1];
	ll[10] = mac[2];
	ll[11] = 0xff;
	ll[12] = 0xfe;
	ll[13] = mac[3];
	ll[14] = mac[4];
	ll[15] = mac[5];
}

/*
 * Native Medium.bwrite.  Neighbor resolution remains in ethermediumframe
 * (arpget/NDP); only the final transmit is native.
 */
static void
nativebwrite(Ipifc *ifc, Block *bp, int version, uchar *ip, Routehint *rh)
{
	Ipv6Native *ni = ifc->arg;
	Block *frame;
	int rc;

	ni->txcalls++;
	if(version != V6){
		freeb(bp);
		ni->txerrs++;
		return;
	}
	if(ni->ether->netif.link == 0){
		freeb(bp);
		ni->txdown++;
		return;
	}
	frame = ethermediumframe6(ni->f, ifc, bp, ip, rh);
	if(frame == nil){
		ni->txheld++;
		return;
	}
	rc = ethercore_tx(ni->ether, frame);
	if(rc == EtherErrOk)
		ni->txframes++;
	else {
		ni->txerrs++;
		freeb(frame);
	}
}

static void
nativeaddmulti(Ipifc*, uchar*, uchar*)
{
	/* The opt-in fixture enables promiscuous reception in start(). */
}

static void
nativedelmulti(Ipifc*, uchar*, uchar*)
{
}

static Medium ipv6nativemedium = {
.name=		"native-ipv6",
.hsize=		ETHERHDRSIZE,
.mintu=		ETHERMINTU,
.maxtu=		ETHERHDRSIZE+1500,
.maclen=	Eaddrlen,
.bwrite=	nativebwrite,
.addmulti=	nativeaddmulti,
.delmulti=	nativedelmulti,
};

static int
enqueue(Ipv6Native *ni, Block *bp)
{
	Etherpkt *p;
	int len, type;

	if(ni == nil || bp == nil)
		return -1;
	len = BLEN(bp);
	if(len < ETHERHDRSIZE+IP6HDR){
		ni->rxbad++;
		freeb(bp);
		return -1;
	}
	p = (Etherpkt*)bp->rp;
	type = (p->type[0]<<8) | p->type[1];
	if(type != ETIP6){
		ni->rxother++;
		freeb(bp);
		return -1;
	}
	if(memcmp(p->d, ni->mac, Eaddrlen) != 0
	&& !(p->d[0] == 0x33 && p->d[1] == 0x33)){
		ni->rxbad++;
		freeb(bp);
		return -1;
	}
	if(qpass(ni->rxq, bp) <= 0){
		/* qpass already released the Block on full/closed */
		ni->rxdrop++;
		return -1;
	}
	ni->rxframes++;
	wakeup(&ni->done);
	return 0;
}

/*
 * EtherRxSink: IRQ or driver-worker context.  Only cheap L2 checks and
 * qpass/freeb are used; ipiput6 runs in the worker.
 */
void
ipv6native_etherinput(void *arg, Ether *e, Block *bp)
{
	Ipv6Native *ni = arg;

	if(ni == nil || e != ni->ether){
		freeb(bp);
		return;
	}
	enqueue(ni, bp);
}

static void
worker(void *arg)
{
	Ipv6Native *ni = arg;
	Block *bp;

	ni->worker = up;
	for(;;){
		if(ni->stop)
			break;
		bp = qbread(ni->rxq, 65536);
		if(bp == nil)
			break;
		bp->rp += ETHERHDRSIZE;
		rlock(&ni->ifc->rwlk);
		if(waserror()){
			runlock(&ni->ifc->rwlk);
			continue;
		}
		ipiput6(ni->f, ni->ifc, bp);
		runlock(&ni->ifc->rwlk);
		poperror();
		ni->rxdelivered++;
	}
	ni->worker_exited = 1;
	wakeup(&ni->done);
}

/*
 * Terminal cleanup after a failed start.  The inherited Fs starts ARP/IP
 * kprocs, so freeing the Fs is not safe.  Keep the one-shot object reachable
 * (nativeone) in NvFailed/NvStopped so a restart fails cleanly instead of
 * leaking an unbounded number of protocol stacks.
 */
static void
nativeterminal(Ipv6Native *ni)
{
	int i;

	if(ni == nil)
		return;
	if(ni->rxreg){
		ethercore_rxunregister(ni->ether, ipv6native_etherinput, ni);
		ni->rxreg = 0;
	}
	if(ni->rxq != nil && !ni->rxclosed){
		qclose(ni->rxq);
		ni->rxclosed = 1;
	}
	ni->stop = 1;
	if(ni->worker_started){
		for(i = 0; i < 200 && !ni->worker_exited; i++)
			tsleep(&ni->done, workerexited, ni, 10);
	}
	if(ni->worker_exited && ni->ifc != nil){
		wlock(&ni->ifc->rwlk);
		if(ni->ifc->m == &ipv6nativemedium){
			ni->ifc->m = &unboundmedium;
			ni->ifc->arg = nil;
		}
		wunlock(&ni->ifc->rwlk);
		if(ni->ether != nil && ni->ether->netif.promiscuous != nil)
			ni->ether->netif.promiscuous(ni->ether->netif.arg, 0);
	}
}

/*
 * Direct Fs/IPv6 initialization plus one link-local address.  Process
 * context only; no namec/Chan/Qid/devattach.
 */
int
ipv6native_start(Ether *ether, char *tag, Ipv6Native **out)
{
	Ipv6Native *ni;
	Fs *f;
	Conv *c;
	Ipifc *ifc;
	char addr[64], *params[3], *err, *configured;
	uchar global[IPaddrlen];

	if(ether == nil || out == nil)
		return EtherErrBadArg;
	*out = nil;

	qlock(&nativelk);
	if(nativeone != nil){
		qunlock(&nativelk);
		return EtherErrBusy;
	}

	ni = smalloc(sizeof(*ni));
	if(ni == nil){
		qunlock(&nativelk);
		return EtherErrNoMem;
	}
	ni->ether = ether;
	ni->state = NvInit;
	memmove(ni->mac, ether->ea, Eaddrlen);
	mac2ll(ni->ll, ether->ea);
	nativeone = ni;

	if(waserror()){
		ni->state = NvFailed;
		nativeterminal(ni);
		qunlock(&nativelk);
		return -1;
	}

	f = smalloc(sizeof(Fs));
	ni->f = f;
	ipfsinit(f, -1);

	qlock(&f->ipifc->qlk);
	c = Fsprotoclone(f->ipifc, tag != nil ? tag : "e72");
	qunlock(&f->ipifc->qlk);
	if(c == nil){
		poperror();
		ni->state = NvFailed;
		nativeterminal(ni);
		qunlock(&nativelk);
		return -1;
	}
	ni->conv = c;
	ifc = c->ptcl;
	ni->ifc = ifc;

	wlock(&ifc->rwlk);
	ifc->m = &ipv6nativemedium;
	ifc->arg = ni;
	ifc->maxtu = ipv6nativemedium.maxtu;
	ifc->delay = 40;
	ifc->speed = 0;	/* no shaping/burst policy in the native test medium */
	ifc->rp = f->v6p->rp;
	memmove(ifc->mac, ether->ea, Eaddrlen);
	snprint(ifc->dev, sizeof(ifc->dev), "%s", ether->netif.name);
	wunlock(&ifc->rwlk);

 /* Native bind bypasses ipifcbind: initialize its existing IPv6 loopback
  * queue as well, so local TCP/UDP destinations never reach a nil queue. */
 (*loopbackmedium.bind)(ifc,0,nil);
	snprint(addr, sizeof(addr), "%I", ni->ll);
	params[0] = "add";
	params[1] = addr;
	params[2] = "/64";
	err = ipifcadd(ifc, params, 3, 0, nil);
	if(err != nil){
		print("ipv6native: add %s: %s\n", addr, err);
		poperror();
		ni->state = NvFailed;
		nativeterminal(ni);
		qunlock(&nativelk);
		return -1;
	}

	/* Optional static IPv6 unicast address; ordinary boot configuration,
	 * no runtime namespace or filesystem. Link-local NDP remains unchanged. */
	configured = getconf("ip6addr");
	if(configured != nil){
		if(parseip(global, configured) < 0 || isv4(global)
		|| ipismulticast(global) || ipcmp(global, IPnoaddr) == 0)
			error("invalid ip6addr");
		params[1] = configured;
		params[2] = getconf("ip6mask");
		if(params[2] == nil)
			params[2] = "/64";
		err = ipifcadd(ifc, params, 3, 0, nil);
		if(err != nil)
			error(err);
	}

	ni->rxq = qopen(NativeRxq, Qmsg, nil, nil);
	if(ni->rxq == nil){
		poperror();
		ni->state = NvFailed;
		nativeterminal(ni);
		qunlock(&nativelk);
		return -1;
	}

	kproc("*ipv6native*", worker, ni);
	ni->worker_started = 1;
	if(ethercore_rxregister(ether, ipv6native_etherinput, ni) != EtherErrOk){
		poperror();
		ni->state = NvFailed;
		nativeterminal(ni);
		qunlock(&nativelk);
		return -1;
	}
	ni->rxreg = 1;
	if(ether->netif.promiscuous != nil)
		ether->netif.promiscuous(ether->netif.arg, 1);

	poperror();
	ni->state = NvRunning;
	*out = ni;
	qunlock(&nativelk);
	return EtherErrOk;
}

/*
 * E11 boot owner for the native NORMAL IPv6 interface.  Controller discovery
 * is owned by ethercore; this direct path attaches the first discovered
 * controller and starts IPv6 without a Dev reset callback or Chan lookup.
 */
static void
ipv6nativebootidle(void)
{
	/*
	 * main starts this parentless kproc before schedinit.  proc.c treats
	 * exit of any parentpid==0 process as a boot failure, so retain the
	 * startup process after init; the native RX worker owns packet service.
	 */
	for(;;)
		tsleep(&up->sleep, return0, nil, 60000);
}

static void
ipv6nativebootproc(void *arg)
{
	Ether *ether;
	Ipv6Native *ni;
	int n, rc;

	USED(arg);
	print("E11NATIVE: startup begin\n");
	ipfmtinstall();
	print("E11NATIVE: probing Ethernet\n");
	n = ethercore_init();
	print("E11NATIVE: controllers=%d\n", n);
	if(n <= 0){
		print("E11NATIVE: no Ethernet controller; IPv6 inactive\n");
		ipv6nativebootidle();
	}
	ether = ethercore_byindex(0);
	if(ether == nil){
		print("E11NATIVE: no Ethernet controller; IPv6 inactive\n");
		ipv6nativebootidle();
	}
	rc = ethercore_attach(ether);
	if(rc != EtherErrOk){
		print("E11NATIVE: Ethernet attach rc=%d\n", rc);
		ipv6nativebootidle();
	}
	rc = ipv6native_start(ether, "e11", &ni);
	if(rc != EtherErrOk){
		print("E11NATIVE: IPv6 start rc=%d\n", rc);
		ipv6nativebootidle();
	}
	print("E11NATIVE: IPv6 ready ctlrno=%d lladdr=%I\n",
		ether->ctlrno, ipv6native_lladdr(ni));
	if(espctl_init() < 0)
		print("esp: initialization failed\n");
	ipv6nativebootidle();
}

int
ipv6native_boot_start(void)
{
	/* Controller probing and Fs setup require process-context error handling. */
	kproc("*e11ipv6*", ipv6nativebootproc, nil);
	return EtherErrOk;
}

Ipv6Native*
ipv6native_current(void)
{
	Ipv6Native *ni;

	qlock(&nativelk);
	ni = nativeone;
	if(ni == nil || ni->state != NvRunning)
		ni = nil;
	qunlock(&nativelk);
	return ni;
}

int
ipv6native_stop(Ipv6Native *ni)
{
	int i, wasrunning;

	if(ni == nil)
		return EtherErrBadArg;
	qlock(&nativelk);
	if(nativeone != ni){
		qunlock(&nativelk);
		return EtherErrBadArg;
	}
	if(ni->state == NvRunning){
		ilock(&ni->udplock);
		if((ni->udp6active != 0 || ni->tcp6active != 0)){
			iunlock(&ni->udplock);
			qunlock(&nativelk);
			return EtherErrBusy;	/* live UDP6 endpoints still use Fs */
		}
		ni->state = NvStopping;
		ni->stop = 1;
		wasrunning = 1;
		iunlock(&ni->udplock);
	}else if(ni->state == NvStopping){
		ilock(&ni->udplock);
		if((ni->udp6active != 0 || ni->tcp6active != 0)){
			iunlock(&ni->udplock);
			qunlock(&nativelk);
			return EtherErrBusy;
		}
		iunlock(&ni->udplock);
		wasrunning = 0;
	}else {
		qunlock(&nativelk);
		return EtherErrBadArg;
	}
	qunlock(&nativelk);

	if(wasrunning){
		if(ni->rxreg){
			ethercore_rxunregister(ni->ether, ipv6native_etherinput, ni);
			ni->rxreg = 0;
		}
		if(ni->rxq != nil && !ni->rxclosed){
			qclose(ni->rxq);
			ni->rxclosed = 1;
		}
	}
	for(i = 0; i < 500 && !ni->worker_exited; i++)
		tsleep(&ni->done, workerexited, ni, 10);
	if(!ni->worker_exited)
		return EtherErrBusy;	/* still Stopping; caller may retry */

	qlock(&nativelk);
	ni->state = NvStopped;
	qunlock(&nativelk);
	wlock(&ni->ifc->rwlk);
	if(ni->ifc->m == &ipv6nativemedium){
		ni->ifc->m = &unboundmedium;
		ni->ifc->arg = nil;
	}
	wunlock(&ni->ifc->rwlk);
	if(ni->ether->netif.promiscuous != nil)
		ni->ether->netif.promiscuous(ni->ether->netif.arg, 0);
	return EtherErrOk;
}

int
ipv6native_inject(Ipv6Native *ni, Block *bp)
{
	if(ni == nil || bp == nil)
		return -1;
	if(ni->state != NvRunning || ni->rxclosed){
		freeb(bp);
		return -1;
	}
	return enqueue(ni, bp);
}

void
ipv6native_stats(Ipv6Native *ni, Ipv6NativeStats *st)
{
	if(st == nil)
		return;
	memset(st, 0, sizeof(*st));
	if(ni == nil)
		return;
	st->started = ni->state == NvRunning;
	st->state = ni->state;
	st->worker_exited = ni->worker_exited;
	st->rxframes = ni->rxframes;
	st->rxbad = ni->rxbad;
	st->rxother = ni->rxother;
	st->rxdrop = ni->rxdrop;
	st->rxdelivered = ni->rxdelivered;
	st->txframes = ni->txframes;
	st->txcalls = ni->txcalls;
	st->txheld = ni->txheld;
	st->txerrs = ni->txerrs;
	st->txdown = ni->txdown;
}

void
ipv6native_setlimit(Ipv6Native *ni, int limit)
{
	if(ni != nil && ni->rxq != nil && !ni->rxclosed)
		qsetlimit(ni->rxq, limit);
}

uchar*
ipv6native_lladdr(Ipv6Native *ni)
{
	return ni != nil ? ni->ll : nil;
}

Fs*
ipv6native_fs(Ipv6Native *ni)
{
	return ni != nil ? ni->f : nil;
}

Ipifc*
ipv6native_ifc(Ipv6Native *ni)
{
	return ni != nil ? ni->ifc : nil;
}
