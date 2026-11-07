/*
 * E7.3 native UDP6 datagram endpoint.
 *
 * Backed by one Conv from the inherited UDP Proto in Announced + headers=7
 * mode.  udpiput preserves peer/local metadata in the existing 52-byte
 * envelope and qpasses one Qmsg record; udpkick consumes the same envelope
 * and builds the UDP6/IPv6 checksum through the inherited output path.
 *
 * Endpoint state: U6Open -> U6Closing -> U6Closed -> U6Destroyed.
 * A CLOSED wrapper remains valid for stats and repeat close; DESTROYED is a
 * freed pointer and must not be used.  This is kernel C, not a userspace ABI.
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
#include	"nativeudp6.h"

enum {
	UDP6_USEAD7 = 52,	/* udp.c headers=7 metadata envelope */
	UDP6_HDR = 8,
	UDP6_MAXPAYLOAD = 65535 - UDP6_HDR,
	UDP6_RXPOLL = 10,	/* ms; bounded observation interval */
	UDP6_INFINITE = 0xffffffffUL,
};

static Lock alloclk;
static Udp6AllocStats allocstats;

static void
allocopen(void)
{
	lock(&alloclk);
	allocstats.opens++;
	allocstats.live++;
	if(allocstats.live > allocstats.peak)
		allocstats.peak = allocstats.live;
	unlock(&alloclk);
}

static void
allocclose(void)
{
	lock(&alloclk);
	allocstats.closes++;
	unlock(&alloclk);
}

static void
allocdestroy(void)
{
	lock(&alloclk);
	allocstats.destroys++;
	if(allocstats.live > 0)
		allocstats.live--;
	unlock(&alloclk);
}

void
udp6_allocstats(Udp6AllocStats *st)
{
	if(st == nil)
		return;
	lock(&alloclk);
	*st = allocstats;
	unlock(&alloclk);
}

static int
udp6fail(Udp6Endpoint *ep, int rc)
{
	if(ep != nil){
		ilock(&ep->st);
		ep->txerrors++;
		iunlock(&ep->st);
	}
	return rc;
}

int
udp6_open(Ipv6Native *net, uchar *local, ushort port, Udp6Endpoint **out)
{
	Udp6Endpoint *ep;
	Proto *udp;
	Conv *c;
	char buf[64], *argv[1], *eargv[2], *e;

	if(out == nil)
		return Udp6ErrBadArg;
	*out = nil;
	if(net == nil || local == nil)
		return Udp6ErrBadArg;
	if(net->state != NvRunning || net->f == nil)
		return Udp6ErrNoNet;
	if(port == 0 || isv4(local) || ipcmp(local, IPnoaddr) == 0
	|| ipismulticast(local))
		return Udp6ErrFamily;

	udp = net->f->t2p[17];
	if(udp == nil || udp->announce == nil)
		return Udp6ErrNoProto;

	qlock(&udp->qlk);
	if(lportinuse(udp, port)){
		qunlock(&udp->qlk);
		return Udp6ErrInUse;
	}
	c = Fsprotoclone(udp, "udp6");
	qunlock(&udp->qlk);
	if(c == nil)
		return Udp6ErrNoMem;

	argv[0] = "headers";
	e = udp->ctl(c, argv, 1);
	if(e != nil){
		closeconv(c);
		return Udp6ErrBind;
	}
	snprint(buf, sizeof(buf), "%I!%d", local, port);
	eargv[0] = "announce";
	eargv[1] = buf;
	c->state = Announcing;	/* announcectlmsg does this on the legacy path */
	e = udp->announce(c, eargv, 2);
	if(e != nil){
		c->state = Idle;
		closeconv(c);
		if(strstr(e, "in use") != nil)
			return Udp6ErrInUse;
		if(strstr(e, "local") != nil)
			return Udp6ErrFamily;
		return Udp6ErrBind;
	}

	ep = smalloc(sizeof(*ep));
	if(ep == nil){
		closeconv(c);
		return Udp6ErrNoMem;
	}
	ep->net = net;
	ep->f = net->f;
	ep->udp = udp;
	ep->conv = c;
	ep->state = U6Open;
	ipmove(ep->laddr, local);
	ep->lport = port;

	/*
	 * Final race-free check against ipv6native_stop: both the native
	 * network state and the live-endpoint count are changed under
	 * net->udplock.  If stop won, recycle the Conv and fail.
	 */
	ilock(&net->udplock);
	if(net->state != NvRunning){
		iunlock(&net->udplock);
		closeconv(c);
		free(ep);
		return Udp6ErrNoNet;
	}
	net->udp6active++;
	iunlock(&net->udplock);
	ep->netcounted = 1;

	allocopen();
	*out = ep;
	return Udp6Ok;
}

static int
udp6waitready(void *a)
{
	Udp6Wait *w = a;
	Udp6Endpoint *ep = w->ep;
	int s, r;

	ilock(&ep->st);
	s = ep->state;
	r = w->woken || s != U6Open || qcanread(ep->conv->rq);
	iunlock(&ep->st);
	return r;
}

static int
udp6quiesced(Udp6Endpoint *ep)
{
	int r;

	ilock(&ep->st);
	r = ep->recvactive == 0 && ep->sendactive == 0 && ep->auxactive == 0;
	iunlock(&ep->st);
	return r;
}

/* Called with ep->st held; the waiter Rendezes are per-receiver. */
static void
wakewaiters(Udp6Endpoint *ep)
{
	Udp6Wait *w;

	for(w = ep->waiters; w != nil; w = w->next){
		w->woken = 1;
		wakeup(&w->r);
	}
}

int
udp6_recv(Udp6Endpoint *ep, Udp6Msg *msg, ulong timeout_ms)
{
	Block *bp;
	Udp6Wait w;
	uchar *p;
	ulong start;
	volatile int onlist;
	int rc, s;

	if(ep == nil || msg == nil)
		return Udp6ErrBadArg;
	memset(msg, 0, sizeof(*msg));
	memset(&w, 0, sizeof(w));
	w.ep = ep;
	onlist = 0;

	ilock(&ep->st);
	if(ep->state != U6Open){
		s = ep->state;
		iunlock(&ep->st);
		return s == U6Closing || s == U6Closed ? Udp6ErrClosed : Udp6ErrDestroyed;
	}
	ep->recvactive++;
	iunlock(&ep->st);

	if(waserror()){
	ilock(&ep->st);
	if(onlist){
		Udp6Wait **l;
		for(l = &ep->waiters; *l != nil; l = &(*l)->next){
			if(*l == &w){
				*l = w.next;
				break;
			}
		}
	}
	ep->recvactive--;
	iunlock(&ep->st);
		nexterror();
	}
	start = NOW;
	for(;;){
		ilock(&ep->st);
		if(ep->state != U6Open){
			iunlock(&ep->st);
			rc = Udp6ErrClosed;
			goto out;
		}
		iunlock(&ep->st);

		/*
		 * Atomic nonblocking dequeue.  Close cannot recycle the Conv
		 * until recvactive returns to zero, so conv->rq stays valid
		 * for this receiver.  udpiput stores one single-Block Qmsg
		 * record per datagram.
		 */
		bp = qget(ep->conv->rq);
		if(bp != nil)
			break;
		if(timeout_ms == 0){
			rc = Udp6ErrTimeout;
			goto out;
		}
		if(timeout_ms != UDP6_INFINITE && (ulong)(NOW - start) >= timeout_ms){
			rc = Udp6ErrTimeout;
			goto out;
		}
		ilock(&ep->st);
		if(ep->state != U6Open){
			iunlock(&ep->st);
			rc = Udp6ErrClosed;
			goto out;
		}
		if(!onlist){
			w.next = ep->waiters;
			ep->waiters = &w;
			onlist = 1;
		}
		iunlock(&ep->st);
		tsleep(&w.r, udp6waitready, &w, UDP6_RXPOLL);
		ilock(&ep->st);
		if(onlist){
			Udp6Wait **l;
			for(l = &ep->waiters; *l != nil; l = &(*l)->next){
				if(*l == &w){
					*l = w.next;
					break;
				}
			}
			onlist = 0;
		}
		w.woken = 0;
		iunlock(&ep->st);
	}

	if(bp->next != nil){
		/* Producer invariant violation; never silently merge datagrams. */
		freeb(bp);
		rc = Udp6ErrBadArg;
		goto out;
	}
	if(BLEN(bp) < UDP6_USEAD7){
		freeb(bp);
		rc = Udp6ErrBadArg;
		goto out;
	}
	ilock(&ep->st);
	if(ep->state != U6Open){
		iunlock(&ep->st);
		freeb(bp);
		rc = Udp6ErrClosed;
		goto out;
	}
	iunlock(&ep->st);

	p = bp->rp;
	ipmove(msg->src, p); p += IPaddrlen;
	ipmove(msg->dst, p); p += IPaddrlen;
	p += IPaddrlen;			/* interface address */
	msg->sport = nhgets(p); p += 2;
	msg->dport = nhgets(p);
	bp->rp += UDP6_USEAD7;
	msg->bp = bp;
	ilock(&ep->st);
	ep->rxmsgs++;
	ep->rxbytes += BLEN(bp);
	iunlock(&ep->st);
	rc = Udp6Ok;

out:
	poperror();
	ilock(&ep->st);
	if(onlist){
		Udp6Wait **l;
		for(l = &ep->waiters; *l != nil; l = &(*l)->next){
			if(*l == &w){
				*l = w.next;
				break;
			}
		}
	}
	ep->recvactive--;
	iunlock(&ep->st);
	return rc;
}

int
udp6_sendto(Udp6Endpoint *ep, uchar *peer, ushort peerport, Block *payload)
{
	Block *nb;
	Route *r;
	uchar *p;
	int maxpayload, len, rc;

	if(ep == nil || peer == nil || payload == nil)
		return Udp6ErrBadArg;
	if(payload->next != nil)
		return udp6fail(ep, Udp6ErrBadArg);
	if(peerport == 0 || isv4(peer) || ipcmp(peer, IPnoaddr) == 0)
		return udp6fail(ep, Udp6ErrBadArg);
	len = BLEN(payload);

	qlock(&ep->sendlk);
	ilock(&ep->st);
	if(ep->state != U6Open){
		int s = ep->state;
		iunlock(&ep->st);
		qunlock(&ep->sendlk);
		return udp6fail(ep, s == U6Closing || s == U6Closed
			? Udp6ErrClosed : Udp6ErrDestroyed);
	}
	ep->sendactive++;
	iunlock(&ep->st);

 if(waserror()){
  ilock(&ep->st);
  ep->sendactive--;
  ep->txerrors++;
  iunlock(&ep->st);
  qunlock(&ep->sendlk);
  nexterror();
 }

	/*
	 * While sendactive is held, close cannot recycle the Conv, the
	 * Ipifc, its Medium or the Fs.  All pre-lock reads below are safe.
	 */
	ilock(&ep->net->udplock);
	if(ep->net->state != NvRunning){
		iunlock(&ep->net->udplock);
		rc = Udp6ErrNoNet;
		goto fail;
	}
	iunlock(&ep->net->udplock);
	if(ep->net->ifc == nil || ep->net->ifc->m == nil){
		rc = Udp6ErrNoNet;
		goto fail;
	}
	if(ep->conv->state != Announced){
		rc = Udp6ErrNotBound;
		goto fail;
	}
	maxpayload = ep->net->ifc->maxtu - ep->net->ifc->m->hsize
		- IP6HDR - UDP6_HDR;
	if(maxpayload < 0 || maxpayload > UDP6_MAXPAYLOAD)
		maxpayload = UDP6_MAXPAYLOAD;
	if(len > maxpayload){
		rc = Udp6ErrTooBig;
		goto fail;
	}

	/*
	 * Synchronous route check.  It catches no-route/legacy-medium paths
	 * before ownership transfer; udpkick still performs the real output
	 * and neighbor resolution.
	 */
	r = v6lookup(ep->f, peer, ep->laddr, nil);
	if(r == nil || (r->tree.type & Rv4) != 0 || r->tree.ifc != ep->net->ifc){
		rc = Udp6ErrNoRoute;
		goto fail;
	}

	nb = allocb(UDP6_USEAD7 + len);
	if(nb == nil){
		rc = Udp6ErrNoMem;
		goto fail;
	}
	p = nb->wp;
	ipmove(p, peer); p += IPaddrlen;
	ipmove(p, ep->laddr); p += IPaddrlen;
	ipmove(p, ep->laddr); p += IPaddrlen;
	hnputs(p, peerport); p += 2;
	hnputs(p, ep->lport); p += 2;
	memmove(p, payload->rp, len);
	nb->wp += UDP6_USEAD7 + len;

	/*
	 * qbwrite on qbypass invokes udpkick synchronously.  If that path
	 * raises, nb may already have been consumed/freed; do not double-free
	 * it.  The caller payload is a separate private copy and remains owned
	 * by the caller on every returned error.  This is an outcome-uncertain
	 * acceptance, not a NIC completion claim.
	 */
	if(waserror()){
		poperror();
		ilock(&ep->st);
		ep->sendactive--;
		ep->txerrors++;
		ep->txuncertain++;
		iunlock(&ep->st);
		qunlock(&ep->sendlk);
		return Udp6ErrSend;
	}
	rc = qbwrite(ep->conv->wq, nb);
	poperror();
	poperror();

	ilock(&ep->st);
	ep->sendactive--;
	if(rc <= 0){
		ep->txerrors++;
		ep->txuncertain++;
	}else
		ep->txaccepted++;
	iunlock(&ep->st);
	qunlock(&ep->sendlk);

	if(rc <= 0)
		return Udp6ErrSend;
	freeb(payload);
	return Udp6Ok;

fail:
	poperror();
	ilock(&ep->st);
	ep->sendactive--;
	ep->txerrors++;
	iunlock(&ep->st);
	qunlock(&ep->sendlk);
	return rc;
}

int
udp6_close(Udp6Endpoint *ep)
{
	Ipv6Native *net;
	int i, take, s, work;

	if(ep == nil)
		return Udp6ErrBadArg;
	ilock(&ep->st);
	s = ep->state;
	if(s == U6Closed){
		iunlock(&ep->st);
		return Udp6ErrClosed;
	}
	if(s == U6Destroyed){
		iunlock(&ep->st);
		return Udp6ErrDestroyed;
	}
	if(s == U6Closing){
		if(ep->closer){
			iunlock(&ep->st);
			return Udp6ErrBusy;
		}
		ep->closer = 1;
		work = 1;
	}else{
		ep->state = U6Closing;
		ep->closer = 1;
		work = 1;
	}
	wakewaiters(ep);
	iunlock(&ep->st);
	if(!work)
		return Udp6ErrBusy;

	for(i = 0; i < 300; i++){
		if(udp6quiesced(ep))
			break;
		tsleep(&up->sleep, return0, nil, 10);
	}
	if(!udp6quiesced(ep)){
		ilock(&ep->st);
		ep->closer = 0;
		iunlock(&ep->st);
		return Udp6ErrBusy;
	}
	/*
	 * No send/recv/aux user can touch the Conv now.  New operations see
	 * U6Closing and are rejected.  Only after closeconv returns is the
	 * Conv slot eligible for Fsprotoclone reuse; after that this wrapper
	 * never dereferences it again.
	 */
	closeconv(ep->conv);

	ilock(&ep->st);
	ep->state = U6Closed;
	ep->closer = 0;
	net = ep->net;
	take = ep->netcounted;
	ep->netcounted = 0;
	iunlock(&ep->st);

	if(take){
		ilock(&net->udplock);
		if(net->udp6active > 0)
			net->udp6active--;
		iunlock(&net->udplock);
	}
	allocclose();
	return Udp6Ok;
}

int
udp6_destroy(Udp6Endpoint **pep)
{
	Udp6Endpoint *ep;
	int s;

	if(pep == nil || *pep == nil)
		return Udp6ErrBadArg;
	ep = *pep;
	ilock(&ep->st);
	s = ep->state;
	if(s != U6Closed || ep->recvactive != 0 || ep->sendactive != 0
	|| ep->auxactive != 0){
		iunlock(&ep->st);
		return s == U6Destroyed ? Udp6ErrDestroyed : Udp6ErrBusy;
	}
	ep->state = U6Destroyed;
	iunlock(&ep->st);

	*pep = nil;
	allocdestroy();
	free(ep);
	return Udp6Ok;
}

void
udp6_stats(Udp6Endpoint *ep, Udp6Stats *st)
{
	int s;

	if(st == nil)
		return;
	memset(st, 0, sizeof(*st));
	if(ep == nil)
		return;
	ilock(&ep->st);
	if(ep->state == U6Destroyed){
		iunlock(&ep->st);
		return;
	}
	ep->auxactive++;
	s = ep->state;
	st->state = s;
	st->closed = s != U6Open;
	st->recvactive = ep->recvactive;
	st->sendactive = ep->sendactive;
	st->rxmsgs = ep->rxmsgs;
	st->rxbytes = ep->rxbytes;
	st->txaccepted = ep->txaccepted;
	st->txerrors = ep->txerrors;
	st->txuncertain = ep->txuncertain;
	iunlock(&ep->st);

	if(s == U6Open){
		st->bound = ep->conv->state == Announced;
		st->pending = qlen(ep->conv->rq);
		st->convstate = ep->conv->state;
	}
	ilock(&ep->st);
	ep->auxactive--;
	iunlock(&ep->st);
}

int
udp6_setlimit(Udp6Endpoint *ep, int limit)
{
	int s;

	if(ep == nil)
		return Udp6ErrBadArg;
	ilock(&ep->st);
	if(ep->state != U6Open){
		s = ep->state;
		iunlock(&ep->st);
		return s == U6Destroyed ? Udp6ErrDestroyed : Udp6ErrClosed;
	}
	ep->auxactive++;
	iunlock(&ep->st);
	qsetlimit(ep->conv->rq, limit);
	ilock(&ep->st);
	ep->auxactive--;
	iunlock(&ep->st);
	return Udp6Ok;
}

int
udp6_pending(Udp6Endpoint *ep)
{
	int n, s;

	if(ep == nil)
		return -1;
	ilock(&ep->st);
	if(ep->state != U6Open){
		s = ep->state;
		iunlock(&ep->st);
		return s == U6Destroyed ? Udp6ErrDestroyed : -1;
	}
	ep->auxactive++;
	iunlock(&ep->st);
	n = qlen(ep->conv->rq);
	ilock(&ep->st);
	ep->auxactive--;
	iunlock(&ep->st);
	return n;
}
