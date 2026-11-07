/*
 * E7.4 experimental native IPv6 Next Header consumer.
 *
 * Registers one Proto for Next Header 253 in the existing Fs protocol table
 * (Fsrcvpcol/Fsproto), so ordinary ipiput6 version/length validation,
 * extension-header processing and routing are reused.  The receive callback
 * runs in the native IPv6 worker (process context) with explicit Block
 * ownership.  Sending is fixed to the same Next Header and the inherited
 * ipoput6/native Medium path.
 *
 * This is a test seam for a future userspace transport, not that transport:
 * no encryption, authentication, SEALED mode or generic raw-IP injection.
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
#include	"nativeinh6.h"

/*
 * Bounded allocation kept after unregister: the one-shot Fs is retained by
 * E7.3, and not freeing the Proto closes the lookup/callback race with the
 * IPv6 worker.  It is one record, not a per-packet leak.
 */
enum {
	NipUnreg = 0,
	NipRegistered,
	NipDraining,
};

struct NativeIp6 {
	Ipv6Native	*net;
	Proto		*proto;
	NativeIp6Rx	rx;
	void		*arg;
	Lock		lk;
	Rendez		r;
	int		state;
	int		drainwait;
	int		protolinked;
	int		delayms;
	int		active;
	uvlong		rxframes;
	uvlong		rxbytes;
	uvlong		rxreject;
	uvlong		rxbad;
	uvlong		txaccepted;
	uvlong		txerrors;
	uvlong		txbytes;
};

static QLock natlk;
static NativeIp6 *natip;

static int
activezero(void *a)
{
	NativeIp6 *c = a;
	int z;

	ilock(&c->lk);
	z = c->active == 0;
	iunlock(&c->lk);
	return z;
}

/*
 * Proto.rcv: called by ipiput6 in the native worker with the whole IPv6
 * packet after inherited validation and extension/fragment processing.
 * Takes ownership of bp.
 */
static void
nativeip6_rcv(Proto *p, Ipifc *ifc, Block *bp)
{
	NativeIp6 *c;
	NativeIp6Msg m;
	NativeIp6Rx rx;
	void *arg;
	Ip6hdr *h;
	int delay, len, rc;

	c = p->priv;
	if(c == nil || bp == nil){
		freeblist(bp);
		return;
	}
	if(ifc != c->net->ifc){
		/* The raw endpoint is bound to the native medium only. */
		freeblist(bp);
		return;
	}
	if(bp->next != nil){
		bp = concatblock(bp);
		if(bp == nil)
			return;
	}
	if(BLEN(bp) < IP6HDR)
		goto bad;
	h = (Ip6hdr*)bp->rp;
	len = nhgets(h->ip6hdr.ploadlen);
	if((h->ip6hdr.vcf[0] & 0xF0) != IP_VER6 || h->ip6hdr.proto != NativeIp6Proto
	|| len != BLEN(bp) - IP6HDR || len > IP_MAX - IP6HDR
	|| BLEN(bp) > IP_MAX)
		goto bad;

	ilock(&c->lk);
	if(c->state != NipRegistered || c->rx == nil){
		iunlock(&c->lk);
		freeb(bp);
		return;
	}
	/*
	 * Capture the admission's callback pair while incrementing active.
	 * Later unregister/re-register may change c->rx/c->arg; this call
	 * must use the pair that admitted it.
	 */
	rx = c->rx;
	arg = c->arg;
	c->active++;
	c->rxframes++;
	c->rxbytes += len;
	delay = c->delayms;
	iunlock(&c->lk);

	/* Test-only observation window; real callbacks should stay bounded. */
	if(delay > 0)
		tsleep(&up->sleep, return0, nil, delay);

	memset(&m, 0, sizeof(m));
	ipmove(m.src, h->ip6hdr.src);
	ipmove(m.dst, h->ip6hdr.dst);
	m.ifc = ifc;
	m.proto = h->ip6hdr.proto;
	bp->rp += IP6HDR;
	m.bp = bp;

	if(waserror()){
		ilock(&c->lk);
		c->active--;
		iunlock(&c->lk);
		wakeup(&c->r);
		nexterror();
	}
	rc = rx(arg, &m);
	poperror();

	/* m.bp is consumed by the callback; do not touch bp here. */
	ilock(&c->lk);
	c->active--;
	if(rc != 0)
		c->rxreject++;
	iunlock(&c->lk);
	wakeup(&c->r);
	return;

bad:
	ilock(&c->lk);
	c->rxbad++;
	iunlock(&c->lk);
	freeblist(bp);
}

int
nativeip6_register(Ipv6Native *net, NativeIp6Rx rx, void *arg)
{
	NativeIp6 *c;
	Proto *p;

	if(net == nil || rx == nil)
		return NativeIp6ErrBadArg;
	if(net->state != NvRunning || net->f == nil || net->ifc == nil)
		return NativeIp6ErrNoNet;

	qlock(&natlk);
	c = natip;
	if(c != nil && c->net != net){
		qunlock(&natlk);
		return NativeIp6ErrBusy;	/* one consumer/net in this PoC */
	}
	if(c != nil){
		ilock(&c->lk);
		if(c->state != NipUnreg){
			iunlock(&c->lk);
			qunlock(&natlk);
			return NativeIp6ErrBusy;
		}
		iunlock(&c->lk);
	}else{
		c = smalloc(sizeof(*c));
		if(c == nil){
			qunlock(&natlk);
			return NativeIp6ErrNoMem;
		}
		c->proto = smalloc(sizeof(Proto));
		if(c->proto == nil){
			free(c);
			qunlock(&natlk);
			return NativeIp6ErrNoMem;
		}
		c->net = net;
	}

	if(net->f->t2p[NativeIp6Proto] != nil
	&& net->f->t2p[NativeIp6Proto] != c->proto){
		if(!c->protolinked){
			free(c->proto);
			free(c);
			if(natip == c)
				natip = nil;
		}
		qunlock(&natlk);
		return NativeIp6ErrBusy;
	}

	if(!c->protolinked){
		p = c->proto;
		memset(p, 0, sizeof(*p));
		p->name = "nativeip6";
		p->ipproto = NativeIp6Proto;
		p->rcv = nativeip6_rcv;
		p->priv = c;
		p->nc = 1;
		p->ptclsize = 0;
		if(Fsproto(net->f, p) != 0){
			free(c->proto);
			free(c);
			qunlock(&natlk);
			return NativeIp6ErrBusy;
		}
		c->protolinked = 1;
	}else{
		net->f->t2p[NativeIp6Proto] = c->proto;
	}

	ilock(&c->lk);
	c->rx = rx;
	c->arg = arg;
	c->state = NipRegistered;
	c->drainwait = 0;
	iunlock(&c->lk);
	natip = c;
	qunlock(&natlk);
	return NativeIp6Ok;
}

int
nativeip6_unregister(Ipv6Native *net)
{
	NativeIp6 *c;
	int i, active;

	if(net == nil)
		return NativeIp6ErrBadArg;
	qlock(&natlk);
	c = natip;
	if(c == nil || c->net != net){
		qunlock(&natlk);
		return NativeIp6ErrNotReg;
	}
	ilock(&c->lk);
	if(c->state == NipUnreg){
		iunlock(&c->lk);
		qunlock(&natlk);
		return NativeIp6ErrNotReg;
	}
	if(c->state == NipDraining){
		if(c->drainwait){
			iunlock(&c->lk);
			qunlock(&natlk);
			return NativeIp6ErrBusy;	/* another caller owns teardown */
		}
		c->drainwait = 1;
	}else{
		c->state = NipDraining;
		c->drainwait = 1;
		if(net->f != nil && net->f->t2p[NativeIp6Proto] == c->proto)
			net->f->t2p[NativeIp6Proto] = nil;
	}
	iunlock(&c->lk);
	qunlock(&natlk);

	/*
	 * Wait for admitted callbacks.  natlk is not held: an in-flight
	 * callback may call nativeip6_send/stats and must be able to finish.
	 * On timeout DRAINING stays latched; a later call is the finalizer.
	 */
	for(i = 0; i < 500; i++){
		if(activezero(c))
			break;
		tsleep(&c->r, activezero, c, 10);
	}
	ilock(&c->lk);
	active = c->active;
	iunlock(&c->lk);
	if(active != 0){
		ilock(&c->lk);
		c->drainwait = 0;	/* still NipDraining */
		iunlock(&c->lk);
		return NativeIp6ErrBusy;
	}

	/*
	 * Quiesce ipiput6 on the native medium before declaring the old
	 * demux lookup drained.  active==0 alone does not cover a dispatch
	 * that obtained the retained Proto just before t2p was unlinked.
	 * No admitted callback remains, so a writer cannot deadlock with a
	 * callback's nativeip6_send here; the endpoint is bound to this ifc.
	 *
	 * This proves demux lookups already inside ipiput6 are drained.  A
	 * frame still waiting in net->rxq has not been admitted to the old
	 * registration: t2p is nil, so it is dropped if demuxed before rearm,
	 * and after rearm it is new input.  No queue-wide drain is claimed;
	 * the E7.4.3 fixture explicitly drains its stale frame before retry.
	 */
	if(net->ifc != nil)
		wlock(&net->ifc->rwlk);

	qlock(&natlk);
	c = natip;
	if(c == nil || c->net != net){
		if(net->ifc != nil)
			wunlock(&net->ifc->rwlk);
		qunlock(&natlk);
		return NativeIp6ErrNotReg;
	}
	ilock(&c->lk);
	if(c->state != NipDraining || !c->drainwait){
		iunlock(&c->lk);
		if(net->ifc != nil)
			wunlock(&net->ifc->rwlk);
		qunlock(&natlk);
		return NativeIp6ErrBusy;
	}
	c->state = NipUnreg;
	c->drainwait = 0;
	c->rx = nil;
	c->arg = nil;
	iunlock(&c->lk);
	if(net->f != nil && net->f->t2p[NativeIp6Proto] == c->proto)
		net->f->t2p[NativeIp6Proto] = nil;
	qunlock(&natlk);
	if(net->ifc != nil)
		wunlock(&net->ifc->rwlk);
	return NativeIp6Ok;
}

/*
 * Build one IPv6 packet with the fixed experimental Next Header and send it
 * through the inherited route/NDP/TX path.  Success consumes payload;
 * failure leaves it to the caller.  This is not a raw-IP injection function.
 */
int
nativeip6_send(Ipv6Native *net, uchar *dst, uchar nexthdr, Block *payload, int ttl)
{
	NativeIp6 *c;
	Block *nb;
	Ip6hdr *h;
	Route *r;
	int len, max, rc;

	if(net == nil || dst == nil || payload == nil)
		return NativeIp6ErrBadArg;
	if(payload->next != nil)
		return NativeIp6ErrBadArg;
	if(nexthdr != NativeIp6Proto)
		return NativeIp6ErrBadArg;
	if(isv4(dst) || ipcmp(dst, IPnoaddr) == 0 || ipismulticast(dst))
		return NativeIp6ErrFamily;
	if(ttl <= 0 || ttl > MAXTTL)
		return NativeIp6ErrBadArg;
	if(net->state != NvRunning || net->f == nil || net->ifc == nil
	|| net->ifc->m == nil)
		return NativeIp6ErrNoNet;

	qlock(&natlk);
	c = natip;
	if(c == nil || c->net != net){
		qunlock(&natlk);
		return NativeIp6ErrNotReg;
	}
	ilock(&c->lk);
	if(c->state != NipRegistered){
		iunlock(&c->lk);
		qunlock(&natlk);
		return NativeIp6ErrNotReg;
	}
	iunlock(&c->lk);
	len = BLEN(payload);
	max = net->ifc->maxtu - net->ifc->m->hsize - IP6HDR;
	if(max < 0 || len > max){
		qunlock(&natlk);
		return NativeIp6ErrTooBig;
	}
	r = v6lookup(net->f, dst, net->ll, nil);
	if(r == nil || (r->tree.type & Rv4) != 0 || r->tree.ifc != net->ifc){
		qunlock(&natlk);
		return NativeIp6ErrNoRoute;
	}
	nb = allocb(IP6HDR + len);
	if(nb == nil){
		qunlock(&natlk);
		return NativeIp6ErrNoMem;
	}
	nb->wp += IP6HDR + len;
	h = (Ip6hdr*)nb->rp;
	memset(h, 0, IP6HDR);
	h->ip6hdr.vcf[0] = IP_VER6;
	hnputs(h->ip6hdr.ploadlen, len);
	h->ip6hdr.proto = nexthdr;
	h->ip6hdr.ttl = ttl;
	ipmove(h->ip6hdr.src, net->ll);
	ipmove(h->ip6hdr.dst, dst);
	memmove(h->payload, payload->rp, len);

	rc = ipoput6(net->f, nb, nil, ttl, DFLTTOS, nil);
	if(rc < 0){
		ilock(&c->lk);
		c->txerrors++;
		iunlock(&c->lk);
		qunlock(&natlk);
		return NativeIp6ErrSend;	/* payload is still caller-owned */
	}
	ilock(&c->lk);
	c->txaccepted++;
	c->txbytes += len;
	iunlock(&c->lk);
	qunlock(&natlk);
	freeb(payload);
	return NativeIp6Ok;
}

int
nativeip6_registered(Ipv6Native *net)
{
	NativeIp6 *c;
	int r;

	qlock(&natlk);
	c = natip;
	if(c != nil && c->net == net){
		ilock(&c->lk);
		r = c->state == NipRegistered;
		iunlock(&c->lk);
	}else
		r = 0;
	qunlock(&natlk);
	return r;
}

void
nativeip6_setdelay(Ipv6Native *net, int ms)
{
	NativeIp6 *c;

	qlock(&natlk);
	c = natip;
	if(c != nil && c->net == net && ms >= 0){
		ilock(&c->lk);
		c->delayms = ms;
		iunlock(&c->lk);
	}
	qunlock(&natlk);
}

void
nativeip6_stats(Ipv6Native *net, NativeIp6Stats *st)
{
	NativeIp6 *c;

	if(st == nil)
		return;
	memset(st, 0, sizeof(*st));
	qlock(&natlk);
	c = natip;
	if(c != nil && c->net == net){
		ilock(&c->lk);
		st->registered = c->state == NipRegistered;
		st->draining = c->state == NipDraining;
		st->active = c->active;
		st->rxframes = c->rxframes;
		st->rxbytes = c->rxbytes;
		st->rxreject = c->rxreject;
		st->rxbad = c->rxbad;
		st->txaccepted = c->txaccepted;
		st->txerrors = c->txerrors;
		st->txbytes = c->txbytes;
		iunlock(&c->lk);
	}
	qunlock(&natlk);
}
