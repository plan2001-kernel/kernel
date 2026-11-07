/*
 * Native IPv6 TCP provider for the generic object layer.
 *
 * A listener or connection is a Vobj (VkindTcp) whose only private state is
 * one Tendpoint.  A Vobj reference, not a Proc slot, owns the Tendpoint, so a
 * connection object can outlive the process that accepted it and can be
 * transferred through an ObjectFIFO as an authorized object reference.
 * The old TCP6* syscalls remain compatibility shims over this same core.
 *
 * The TCP engine and its stream semantics are unchanged.  No Chan, path,
 * descriptor or TCP-specific FIFO structure is introduced.
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
#include	"../net/ip.h"
#include	"../net/nativev6.h"

extern int net6allowed(Proc*);

enum {
	Perproc		= 8,
	TcpObjMax	= 64,
	TcpReadMax	= 65536,
	TcpWriteMax	= 4096,
	TcpTimeoutMax	= 30000,
};

typedef struct Tendpoint Tendpoint;
struct Tendpoint {
	Ref	ref;
	QLock	gate;
	Conv	*c;
	Ipv6Native *net;
	int	listener;
};

static Lock tcpobjlock;
static int tcpobjs;

static void
tput(Tendpoint *e)
{
	if(e == nil)
		return;
	if(decref(&e->ref) == 0){
		closeconv(e->c);
		ilock(&e->net->udplock);
		e->net->tcp6active--;
		iunlock(&e->net->udplock);
		free(e);
	}
}

static Proto*
protocol(void)
{
	Ipv6Native *net;

	if(!net6allowed(up))
		error(Eperm);
	net = ipv6native_current();
	if(net == nil || net->f == nil || net->f->t2p[6] == nil)
		error("tcp6: unavailable");
	return net->f->t2p[6];
}

static Tendpoint*
newendpoint(Conv *c, int listener)
{
	Tendpoint *e;

	e = smalloc(sizeof *e);
	e->ref.ref = 1;
	e->c = c;
	e->listener = listener;
	e->net = ipv6native_current();
	if(e->net == nil){
		free(e);
		error("tcp6: network stopped");
	}
	ilock(&e->net->udplock);
	if(e->net->state != NvRunning){
		iunlock(&e->net->udplock);
		free(e);
		error("tcp6: network stopped");
	}
	e->net->tcp6active++;
	iunlock(&e->net->udplock);
	return e;
}

static Tendpoint*
openconv(int listener, uchar *addr, int port)
{
	Proto *p;
	Conv *c;
	Tendpoint *e;
	char dest[80], *args[2], *err;

	p = protocol();
	qlock(&p->qlk);
	c = Fsprotoclone(p, "tcp6");
	qunlock(&p->qlk);
	if(c == nil)
		error(Enomem);
	if(waserror()){
		closeconv(c);
		nexterror();
	}
	snprint(dest, sizeof dest, "%I!%d", addr, port);
	args[0] = listener ? "announce" : "connect";
	args[1] = dest;
	qlock(&c->qlk);
	if(waserror()){
		qunlock(&c->qlk);
		nexterror();
	}
	c->cerr[0] = 0;
	c->state = listener ? Announcing : Connecting;
	err = listener ? p->announce(c, args, 2) : p->connect(c, args, 2);
	poperror();
	qunlock(&c->qlk);
	if(err)
		error(err);
	if(!listener){
		int ticks = 0;
		while(c->state == Connecting && ticks++ < 500)
			tsleep(&up->sleep, return0, nil, 10);
		if(c->state != Connected || c->cerr[0])
			error("tcp6: connect failed/timed out");
	}
	e = newendpoint(c, listener);
	poperror();
	return e;
}

static int
reserveobj(void)
{
	lock(&tcpobjlock);
	if(tcpobjs >= TcpObjMax){
		unlock(&tcpobjlock);
		return -1;
	}
	tcpobjs++;
	unlock(&tcpobjlock);
	return 0;
}

static void
releaseobj(void)
{
	lock(&tcpobjlock);
	if(tcpobjs > 0)
		tcpobjs--;
	unlock(&tcpobjlock);
}

Vobj*
vtcp_open(int listener, uchar *addr, int port)
{
	Tendpoint *e;
	Vobj *o;

	if(port <= 0 || port > 65535 || isv4(addr) || ipcmp(addr, IPnoaddr) == 0
	|| ipismulticast(addr))
		error(Ebadarg);
	if(vobjhandlesbykind(up, VkindTcp) >= Perproc)
		error("tcp6: endpoint limit");
	if(reserveobj() != 0)
		error("tcp6: endpoint limit");
	if(waserror()){
		releaseobj();
		nexterror();
	}
	e = openconv(listener, addr, port);
	o = vobjnewkind(VkindTcp, 0);
	if(o == nil){
		tput(e);
		error(Enomem);
	}
	o->provider = listener ? VopenTcpListen : VopenTcpConnect;
	vobjsetpriv(o, e);
	poperror();
	return o;
}

int
vtcp_accept(Vobj *lo, int timeout, Vobj **out)
{
	Tendpoint *le, *e = nil;
	Conv *c = nil;
	Vobj *o = nil;
	int ticks = 0, reserved = 0;

	if(out == nil || lo == nil || lo->kind != VkindTcp)
		error(Ebadarg);
	le = lo->priv;
	if(le == nil || !le->listener || timeout < 0 || timeout > TcpTimeoutMax)
		error(Ebadarg);
	if(vobjhandlesbykind(up, VkindTcp) >= Perproc)
		error("tcp6: endpoint limit");
	if(waserror()){
		if(o != nil)
			vobjput(o);
		if(e != nil)
			tput(e);
		else if(c != nil)
			closeconv(c);
		if(reserved)
			releaseobj();
		nexterror();
	}
	while(c == nil){
		qlock(&le->c->qlk);
		c = le->c->incall;
		if(c){
			le->c->incall = c->next;
			c->next = nil;
		}
		qunlock(&le->c->qlk);
		if(c)
			break;
		if(ticks++ >= timeout/10)
			error("tcp6: accept timed out");
		tsleep(&up->sleep, return0, nil, 10);
	}
	e = newendpoint(c, 0);
	c = nil;
	if(reserveobj() != 0)
		error("tcp6: endpoint limit");
	reserved = 1;
	o = vobjnewkind(VkindTcp, 0);
	if(o == nil)
		error(Enomem);
	o->provider = VopenTcpConnect;
	vobjsetpriv(o, e);
	e = nil;
	poperror();
	*out = o;
	return 0;
}

long
vtcp_read(Vobj *o, void *buf, long n, long timeout)
{
	Tendpoint *e;
	long result;
	int ticks = 0;

	if(o == nil || o->kind != VkindTcp)
		error(Ebadarg);
	if(n < 0 || n > TcpReadMax || timeout < 0 || timeout > TcpTimeoutMax)
		error(Ebadarg);
	e = o->priv;
	if(e == nil || e->listener)
		error(Ebadarg);
	validaddr((uintptr)buf, n, 1);
	qlock(&e->gate);
	if(waserror()){
		qunlock(&e->gate);
		nexterror();
	}
	if(n == 0){
		result = 0;
		goto out;
	}
	while(!qcanread(e->c->rq) && !qisclosed(e->c->rq)){
		if(ticks++ >= timeout/10)
			error("tcp6: read timed out");
		tsleep(&up->sleep, return0, nil, 10);
	}
	result = qread(e->c->rq, buf, n);
out:
	poperror();
	qunlock(&e->gate);
	return result;
}

long
vtcp_write(Vobj *o, void *buf, long n)
{
	Tendpoint *e;
	long result;

	if(o == nil || o->kind != VkindTcp)
		error(Ebadarg);
	if(n < 0 || n > TcpWriteMax)
		error(Ebadarg);
	e = o->priv;
	if(e == nil || e->listener)
		error(Ebadarg);
	validaddr((uintptr)buf, n, 0);
	qlock(&e->gate);
	if(waserror()){
		qunlock(&e->gate);
		nexterror();
	}
	result = qwrite(e->c->wq, buf, n);
	poperror();
	qunlock(&e->gate);
	return result;
}

void
vtcp_release(Vobj *o)
{
	if(o != nil){
		tput(o->priv);
		o->priv = nil;
		releaseobj();
	}
}

/* Compatibility: handle cleanup is now owned by vobjprocexit(). */
void
tcp6procexit(Proc *p)
{
	USED(p);
}

int
tcp6endpointcount(void)
{
	int n;
	lock(&tcpobjlock);
	n = tcpobjs;
	unlock(&tcpobjlock);
	return n;
}
