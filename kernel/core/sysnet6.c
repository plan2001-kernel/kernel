/*
 * Native UDP6 endpoint provider for the universal object ABI.
 *
 * The datagram engine is still nativeudp6.c; this file owns the object
 * capability around one Udp6Endpoint.  OBJOPEN(ObjOpenUdp6) binds it and
 * returns a process-local Vhand with Vread/Vwrite/Vmeta/Vdelegate rights.
 * OBJINVOKE(SendTo/RecvFrom) is the only datagram data path.  The historical
 * NET6ADDR/OPEN/RECVFROM/SENDTO/CLOSE numbers are reserved and have no
 * dispatch entry; the address lookup is OBJINVOKE(ObjOpNet6Addr).
 *
 * Every UDP6 open and address lookup keeps the non-transferable boot/permit
 * gate.  An OBJ UDP6 Handle is an explicit capability: it is not inherited by
 * rfork or exec, and only OSPAWN's explicit handle list can delegate it.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"../core/netif.h"
#include	"../core/etherif.h"
#include	"../core/vobjint.h"
#include	"../../runtime/abi/obj.h"
#include	"../net/ip.h"
#include	"../net/nativev6.h"
#include	"../net/nativeudp6.h"

enum {
	Net6max		= 8,		/* compatibility per-process endpoint limit */
	Net6payload	= OBJ_UDP6_MAXPAYLOAD,
	Net6rxbytes	= 16 * 1400,
	Net6infinite	= 0xffffffffUL,
	Net6ErrTooBig	= -100,		/* receive buffer smaller than datagram */
};

int
net6allowed(Proc *p)
{
	return obootstrap(p) || p->net6permit;
}

static char*
net6errstr(int rc)
{
	switch(rc){
	case Udp6ErrNoNet:	return "net6: IPv6 is unavailable";
	case Udp6ErrNoProto:	return "net6: UDP6 is unavailable";
	case Udp6ErrFamily:	return "net6: invalid IPv6 bind address or port";
	case Udp6ErrBind:	return "net6: bind failed";
	case Udp6ErrInUse:	return "net6: local port is already in use";
	case Udp6ErrClosed:	return "net6: endpoint is closed";
	case Udp6ErrTooBig:	return "net6: datagram exceeds the IPv6 minimum-MTU limit";
	case Udp6ErrNoMem:	return "net6: out of memory";
	case Udp6ErrTimeout:	return "net6: receive timed out";
	case Udp6ErrBusy:	return "net6: endpoint is busy";
	case Udp6ErrNoRoute:	return "net6: no IPv6 route";
	case Udp6ErrDestroyed:	return "net6: endpoint is no longer valid";
	case Udp6ErrBadArg:	return "net6: invalid endpoint argument";
	case Net6ErrTooBig:	return "net6: datagram exceeds receive buffer";
	default:		return "net6: UDP6 operation failed";
	}
}

/* OBJINVOKE(ObjOpNet6Addr): fill addr with the active interface's IPv6
 * link-local address.  This is the authorized metadata replacement for the
 * retired NET6ADDR call: it keeps the same non-transferable boot/permit gate
 * but needs no endpoint or port.  addr is a kernel-validated user buffer. */
long
vudp6addr(void *addr, long len)
{
	Ipv6Native *net;

	if(!net6allowed(up))
		error(Eperm);
	if(addr == nil || len != IPaddrlen)
		error(Ebadarg);
	net = ipv6native_current();
	if(net == nil)
		error(net6errstr(Udp6ErrNoNet));
	memmove(addr, net->ll, IPaddrlen);
	return 0;
}

/* Release one endpoint owned by a Vobj.  Close can briefly race an in-flight
 * send/receive, so wait for it to quiesce before destroying the wrapper. */
void
vudp6_release(Vobj *o)
{
	Udp6Endpoint *ep;
	int rc;

	if(o == nil || o->priv == nil)
		return;
	ep = o->priv;
	o->priv = nil;
	for(;;){
		rc = udp6_close(ep);
		if(rc != Udp6ErrBusy)
			break;
		tsleep(&up->sleep, return0, nil, 10);
	}
	if(rc != Udp6Ok && rc != Udp6ErrClosed)
		print("udp6: endpoint close failed (%d)\n", rc);
	rc = udp6_destroy(&ep);
	if(rc != Udp6Ok)
		print("udp6: endpoint destroy failed (%d)\n", rc);
}

/* Shared datagram cores.  The caller owns and has validated all user
 * pointers; these functions only build/consume the native Block. */
static int
vudp6_sendcore(Vobj *o, uchar *addr, unsigned int port, void *data, ulong n)
{
	Block *bp;
	int rc;

	if(o == nil || o->kind != VkindUdp6 || o->priv == nil
	|| n > Net6payload || port < 1 || port > 65535)
		return Udp6ErrBadArg;
	bp = allocb(n == 0 ? 1 : n);
	if(bp == nil)
		return Udp6ErrNoMem;
	if(n != 0){
		memmove(bp->wp, data, n);
		bp->wp += n;
	}
	rc = udp6_sendto(o->priv, addr, (ushort)port, bp);
	if(rc != Udp6Ok)
		freeb(bp);	/* on success udp6_sendto consumed it */
	return rc;
}

/* Returns the payload byte count, a negative Udp6Err* value or Net6ErrTooBig.
 * The datagram is consumed on every non-negative and too-big result. */
static long
vudp6_recvcore(Vobj *o, void *data, long cap, uchar *peer,
		unsigned int *port, ulong timeout)
{
	Udp6Msg msg;
	long n;
	int rc;

	if(o == nil || o->kind != VkindUdp6 || o->priv == nil
	|| cap < 1 || cap > Net6payload)
		return Udp6ErrBadArg;
	memset(&msg, 0, sizeof msg);
	rc = udp6_recv(o->priv, &msg, timeout);
	if(rc != Udp6Ok)
		return rc;
	n = BLEN(msg.bp);
	if(n > cap){
		freeb(msg.bp);
		return Net6ErrTooBig;
	}
	if(n != 0)
		memmove(data, msg.bp->rp, n);
	ipmove(peer, msg.src);
	*port = msg.sport;
	freeb(msg.bp);
	return n;
}

/* Bind one endpoint and return its unpublished Vobj.  The caller owns the
 * returned reference and installs or releases it.  All addresses here are
 * kernel-validated, never user pointers. */
static Vobj*
vudp6_new(uchar *addr, unsigned int port)
{
	Udp6Endpoint *ep = nil;
	Vobj *o;
	Ipv6Native *net;
	int rc;

	if(!net6allowed(up))
		error(Eperm);
	if(addr == nil || port < 1 || port > 65535)
		error(Ebadarg);
	if(vobjhandlesbykind(up, VkindUdp6) >= Net6max)
		error("udp6: endpoint limit");
	net = ipv6native_current();
	if(net == nil)
		error(net6errstr(Udp6ErrNoNet));
	rc = udp6_open(net, addr, (ushort)port, &ep);
	if(rc != Udp6Ok)
		error(net6errstr(rc));
	rc = udp6_setlimit(ep, Net6rxbytes);
	if(rc != Udp6Ok){
		udp6_close(ep);
		udp6_destroy(&ep);
		error(net6errstr(rc));
	}
	o = vobjnewkind(VkindUdp6, 0);
	if(o == nil){
		udp6_close(ep);
		udp6_destroy(&ep);
		error(Enomem);
	}
	o->provider = VopenUdp6;
	vobjsetpriv(o, ep);
	return o;
}

/* OBJOPEN(ObjOpenUdp6): bind one endpoint and return its Vhand. */
int
vobjopenudp6(void *ureq, long len, uvlong rights, VobjId *id)
{
	ObjOpenUdp6Req req;
	Vobj *o;
	int defrights, handle;

	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	if(ureq == nil || len != sizeof req)
		error(Ebadarg);
	validaddr((uintptr)ureq, sizeof req, 0);
	memmove(&req, ureq, sizeof req);
	if(req.flags != 0)
		error(Ebadarg);
	if(rights & ~(uvlong)Vall)
		error(Ebadarg);
	defrights = Vread|Vwrite|Vmeta|Vdelegate;
	if(rights == 0)
		rights = defrights;
	if((rights & ~(uvlong)defrights) != 0)
		error(Eperm);
	o = vudp6_new(req.addr, req.port);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	handle = vobjinstallobj(o, (int)rights);
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	poperror();
	vobjput(o);
	return handle;
}

/* OBJINVOKE(ObjOpSendTo).  req is the user's header+payload image. */
long
vobjudp6send(int handle, void *ureq, long len)
{
	ObjUdp6SendReq req;
	Vobj *o;
	ulong n;
	int rc;

	if(ureq == nil || len < sizeof req)
		error(Ebadarg);
	validaddr((uintptr)ureq, len, 0);
	memmove(&req, ureq, sizeof req);
	if(req.flags != 0 || req.reserved != 0 || req.len > Net6payload
	|| (uvlong)sizeof req + req.len != (uvlong)len)
		error(Ebadarg);
	n = req.len;
	o = vobjholdobj(handle, Vwrite);
	if(o->kind != VkindUdp6 || o->priv == nil){
		vobjput(o);
		error(Ebadarg);
	}
	rc = vudp6_sendcore(o, req.addr, req.port,
		(uchar*)ureq + sizeof req, n);
	vobjput(o);
	if(rc != Udp6Ok)
		error(net6errstr(rc));
	return (long)n;
}

/* OBJINVOKE(ObjOpRecvFrom).  out is the user's header+payload image. */
long
vobjudp6recv(int handle, void *out, long outlen, uvlong timeout)
{
	ObjUdp6RecvHdr hdr;
	uchar peer[IPaddrlen];
	unsigned int port;
	Vobj *o;
	long cap, n;

	if(out == nil || outlen < sizeof hdr)
		error(Ebadarg);
	cap = outlen - sizeof hdr;
	if(cap < 1 || cap > Net6payload || timeout > Net6infinite)
		error(Ebadarg);
	validaddr((uintptr)out, outlen, 1);
	o = vobjholdobj(handle, Vread);
	if(o->kind != VkindUdp6 || o->priv == nil){
		vobjput(o);
		error(Ebadarg);
	}
	n = vudp6_recvcore(o, (uchar*)out + sizeof hdr, cap, peer, &port,
		(ulong)timeout);
	vobjput(o);
	if(n < 0)
		error(net6errstr((int)n));
	memset(&hdr, 0, sizeof hdr);
	ipmove(hdr.addr, peer);
	hdr.port = port;
	hdr.len = (unsigned int)n;
	memmove(out, &hdr, sizeof hdr);
	return n;
}

extern void tcp6procexit(Proc*);
void
net6procexit(Proc *p)
{
	tcp6procexit(p);
	p->net6permit = 0;
	/* UDP6 Handles live in the process Vhtab and are released by
	 * vobjprocexit(); there is no second endpoint table to clean up. */
}

void
net6procexec(Proc *p)
{
	net6procexit(p);
}


