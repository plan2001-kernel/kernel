/*
 * Native IP core shared by the legacy #I Dev adapter and the E7 native
 * path.  No Chan/Qid/Dirtab/devattach here: Proto registration, Conv
 * cloning/teardown, address/port helpers and Fs initialization only.
 * Protocol and conversation identities are plain indices in Fs/Proto,
 * with no directory-number encoding.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

#include	"ip.h"
#include	"ipv6.h"

static char network[] = "network";

/*
 * The inherited #I/#l Dev resets install these formats.  A native-only
 * profile has neither, so install them from the shared IP core instead.
 * fmtinstall is idempotent for a repeated letter.
 */
void
ipfmtinstall(void)
{
	fmtinstall('i', eipfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('E', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('M', eipfmt);
}

IPaux*
newipaux(char *owner, char *tag)
{
	IPaux *a;

	a = smalloc(sizeof(*a));
	kstrdup(&a->owner, owner);
	strncpy(a->tag, tag, sizeof(a->tag));
	return a;
}

/*
 * Direct Fs/IPv6 initialization shared by the legacy #I attach and the
 * native IPv6 entrypoint.  No Chan/Qid/devattach is involved in this
 * function; callers in process context may run it after the scheduler has
 * started.  The caller owns the zeroed Fs and must call it exactly once.
 */
void
ipfsinit(Fs *f, int dev)
{
	extern void (*ipprotoinit[])(Fs*);
	int i;

	ipfmtinstall();
	ip_init(f);
	arpinit(f);
	netloginit(f);
	for(i = 0; ipprotoinit[i]; i++)
		ipprotoinit[i](f);
	f->dev = dev;
}

void
closeconv(Conv *cv)
{
	Conv *nc;
	Ipmulti *mp;

	qlock(&cv->qlk);

	if(--cv->inuse > 0) {
		qunlock(&cv->qlk);
		return;
	}

	/* close all incoming calls since no listen will ever happen */
	for(nc = cv->incall; nc != nil; nc = cv->incall){
		cv->incall = nc->next;
		closeconv(nc);
	}
	cv->incall = nil;

	kstrdup(&cv->owner, network);
	cv->perm = 0660;

	while((mp = cv->multi) != nil)
		ipifcdelmulti(cv, mp->ma, mp->ia);

	if(cv->p->close != nil)
		(*cv->p->close)(cv);

	cv->state = Idle;
	qunlock(&cv->qlk);
}

static void
setladdr(Conv* c)
{
	findlocalip(c->p->f, c->hash.laddr, c->hash.raddr);
}

char*
setluniqueport(Conv* c, int lport)
{
	Translation *q;
	Proto *p;
	Conv *xp;
	int x;

	p = c->p;

	qlock(&p->qlk);
	for(x = 0; x < p->nc; x++){
		xp = p->conv[x];
		if(xp == nil)
			break;
		if(xp == c)
			continue;
		if((xp->state == Connected || xp->state == Announced)
		&& xp->hash.lport == lport
		&& xp->hash.rport == c->hash.rport
		&& ipcmp(xp->hash.raddr, c->hash.raddr) == 0
		&& ipcmp(xp->hash.laddr, c->hash.laddr) == 0)
			goto Inuse;
	}
	for(q = p->translations; q != nil; q = q->next){
		if(q->backward.lport == lport
		&& q->backward.rport == c->hash.rport
		&& ipcmp(q->backward.raddr, c->hash.raddr) == 0
		&& ipcmp(q->backward.laddr, c->hash.laddr) == 0)
			goto Inuse;
	}
	c->hash.lport = lport;
	qunlock(&p->qlk);
	return nil;
Inuse:
	qunlock(&p->qlk);
	return "address in use";
}

int
lportinuse(Proto *p, ushort lport)
{
	Translation *q;
	int x;

	for(x = 0; x < p->nc && p->conv[x]; x++)
		if(p->conv[x]->hash.lport == lport)
			return 1;
	for(q = p->translations; q != nil; q = q->next)
		if(q->backward.lport == lport)
			return 1;
	return 0;
}

int
unusedlport(Proto *p)
{
	ushort port;
	int i;

	/*
	 * Unrestricted ports are chosen randomly
	 * between 2^15 and 2^16.  There are at most
	 * 4*Nchan = 4096 ports in use at any given time,
	 * so even in the worst case, a random probe has a
	 * 1 - 4096/2^15 = 87% chance of success.
	 * If 64 successive probes fail, there is a bug somewhere
	 * (or a once in 10^58 event has happened, but that's
	 * less likely than a venti collision).
	 */
	for(i=0; i<64; i++){
		port = (1<<15) + nrand(1<<15);
		if(!lportinuse(p, port))
			return port;
	}
	return -1;
}

static char *
setlport(Conv* c)
{
	Proto *p;
	int i, port;

	p = c->p;
	qlock(&p->qlk);
	if(c->restricted){
		/* Restricted ports cycle between 600 and 1024. */
		for(i=0; i<1024-600; i++){
			if(p->nextrport >= 1024 || p->nextrport < 600)
				p->nextrport = 600;
			port = p->nextrport++;
			if(!lportinuse(p, port))
				goto chosen;
		}
	}else{
		port = unusedlport(p);
		if(port > 0)
			goto chosen;
	}
	qunlock(&p->qlk);
	return "no ports available";

chosen:
	c->hash.lport = port;
	qunlock(&p->qlk);
	return nil;
}

char*
setladdrport(Conv* c, char* str, int announcing)
{
	char *p;
	char *rv;
	ushort lport;
	uchar addr[IPaddrlen];

	/*
	 *  ignore restricted part if it exists.  it's
	 *  meaningless on local ports.
	 */
	p = strchr(str, '!');
	if(p != nil){
		*p++ = 0;
		if(strcmp(p, "r") == 0)
			p = nil;
	}

	c->hash.lport = 0;
	if(p == nil){
		if(announcing)
			ipmove(c->hash.laddr, IPnoaddr);
		else
			setladdr(c);
		p = str;
	} else {
		if(strcmp(str, "*") == 0)
			ipmove(c->hash.laddr, IPnoaddr);
		else {
			if(parseip(addr, str) == -1)
				return Ebadip;
			if(ipforme(c->p->f, addr) != 0 || ipismulticast(addr))
				ipmove(c->hash.laddr, addr);
			else
				return "not a local IP address";
		}
	}

	/* one process can get all connections */
	if(announcing && strcmp(p, "*") == 0){
		if(!iseve())
			error(Eperm);
		return setluniqueport(c, 0);
	}

	str = p;
	lport = strtol(str, &p, 10);
	if(p <= str || strchr("!", *p) == nil)
		return "bad numeric port";

	if(lport <= 0)
		rv = setlport(c);
	else
		rv = setluniqueport(c, lport);
	return rv;
}

static char*
setraddrport(Conv* c, char* str)
{
	char *p;

	p = strchr(str, '!');
	if(p == nil)
		return "malformed address";
	*p++ = 0;
	if(parseip(c->hash.raddr, str) == -1)
		return Ebadip;

	str = p;
	c->hash.rport = strtol(str, &p, 10);
	if(p <= str || strchr("!", *p) == nil)
		return "bad numeric port";

	if(strstr(p, "!r") != nil)
		c->restricted = 1;

	return nil;
}

char*
Fsstdconnect(Conv *c, char *argv[], int argc)
{
	char *p;

	switch(argc) {
	default:
		return "bad args to connect";
	case 2:
		p = setraddrport(c, argv[1]);
		if(p != nil)
			return p;
		setladdr(c);
		p = setlport(c);
		if (p != nil)
			return p;
		break;
	case 3:
		p = setraddrport(c, argv[1]);
		if(p != nil)
			return p;
		p = setladdrport(c, argv[2], 0);
		if(p != nil)
			return p;
	}

	c->ipversion = convipvers(c);

	return nil;
}

char*
Fsstdannounce(Conv* c, char* argv[], int argc)
{
	memset(c->hash.raddr, 0, sizeof(c->hash.raddr));
	c->hash.rport = 0;
	switch(argc){
	default:
		break;
	case 2:
		return setladdrport(c, argv[1], 1);
	}
	return "bad args to announce";
}

char*
Fsstdbind(Conv* c, char* argv[], int argc)
{
	switch(argc){
	default:
		break;
	case 2:
		return setladdrport(c, argv[1], 0);
	}
	return "bad args to bind";
}

int
Fsproto(Fs *f, Proto *p)
{
	if(f->np >= Maxproto)
		return -1;

	p->f = f;

	if(p->ipproto >= 0){
		if(f->t2p[p->ipproto] != nil)
			return -1;
		f->t2p[p->ipproto] = p;
	}

	if(p->nc > Maxconv){
		print("Fsproto: %s nc %d > %d\n", p->name, p->nc, Maxconv);
		p->nc = Maxconv;
	}
	p->conv = malloc(sizeof(Conv*)*(p->nc+1));
	if(p->conv == nil)
		panic("Fsproto");

	p->x = f->np;
	p->nextrport = 600;
	f->p[f->np++] = p;

	return 0;
}

int
Fsbuiltinproto(Fs* f, uchar proto)
{
	return f->t2p[proto] != nil;
}

Conv*
Fsprotoclone(Proto *p, char *user)
{
	Conv *c, **pp, **ep;

retry:
	c = nil;
	ep = &p->conv[p->nc];
	for(pp = p->conv; pp < ep; pp++) {
		c = *pp;
		if(c == nil){
			c = malloc(sizeof(Conv));
			if(c == nil)
				return nil;
			if(waserror()){
				qfree(c->rq);
				qfree(c->wq);
				qfree(c->eq);
				qfree(c->sq);
				free(c->ptcl);
				free(c);
				return nil;
			}
			c->p = p;
			c->x = pp - p->conv;
			if(p->ptclsize != 0){
				c->ptcl = malloc(p->ptclsize);
				if(c->ptcl == nil)
					error(Enomem);
			}
			c->eq = qopen(1024, Qmsg, 0, 0);
			if(c->eq == nil)
				error(Enomem);
			(*p->create)(c);
			if(c->rq == nil || c->wq == nil)
				error(Enomem);
			poperror();
			qlock(&c->qlk);
			*pp = c;
			p->ac++;
			break;
		}
		if(canqlock(&c->qlk)){
			/*
			 *  make sure both processes and protocol
			 *  are done with this Conv
			 */
			if(c->inuse == 0 && (p->inuse == nil || (*p->inuse)(c) == 0))
				break;

			qunlock(&c->qlk);
		}
	}
	if(pp >= ep) {
		if(p->gc != nil){
			print("Fsprotoclone: garbage collecting %s Convs\n", p->name);
			if((*p->gc)(p))
				goto retry;
		}
		return nil;
	}

	c->inuse = 1;
	kstrdup(&c->owner, user);
	c->perm = 0660;
	c->state = Idle;
	ipmove(c->hash.laddr, IPnoaddr);
	ipmove(c->hash.raddr, IPnoaddr);
	c->rh.a = nil;
	c->rh.r = nil;
	c->rh.rgen = 0;
	c->hash.lport = 0;
	c->hash.rport = 0;
	c->restricted = 0;
	c->ignoreadvice = 0;
	c->ttl = MAXTTL;
	c->tos = 0;
	c->ctime = seconds();
	qreopen(c->rq);
	qreopen(c->wq);
	qreopen(c->eq);

	qunlock(&c->qlk);
	return c;
}

int
Fsconnected(Conv* c, char* msg)
{
	if(msg != nil && *msg != '\0')
		strncpy(c->cerr, msg, ERRMAX-1);

	switch(c->state){

	case Announcing:
		c->state = Announced;
		break;

	case Connecting:
		c->state = Connected;
		break;
	}

	wakeup(&c->cr);
	return 0;
}

Proto*
Fsrcvpcol(Fs* f, uchar proto)
{
	if(f->ipmux)
		return f->ipmux;
	else
		return f->t2p[proto];
}

Proto*
Fsrcvpcolx(Fs *f, uchar proto)
{
	return f->t2p[proto];
}

Conv*
Fsnewcall(Conv *c, uchar *raddr, ushort rport, uchar *laddr, ushort lport, uchar version)
{
	Conv *nc;
	Conv **l;
	int i;

	qlock(&c->qlk);
	i = 0;
	for(l = &c->incall; *l; l = &(*l)->next)
		i++;
	if(i >= Maxincall) {
		static int beenhere;

		qunlock(&c->qlk);
		if (!beenhere) {
			beenhere = 1;
			print("Fsnewcall: incall queue full (%d) on port %d\n",
				i, c->hash.lport);
		}
		return nil;
	}

	/* find a free conversation */
	nc = Fsprotoclone(c->p, c->owner);
	if(nc == nil) {
		qunlock(&c->qlk);
		return nil;
	}
	ipmove(nc->hash.raddr, raddr);
	nc->hash.rport = rport;
	ipmove(nc->hash.laddr, laddr);
	nc->hash.lport = lport;
	nc->next = nil;
	*l = nc;
	nc->state = Connected;
	nc->ipversion = version;

	qunlock(&c->qlk);

	wakeup(&c->listenr);

	return nc;
}

ulong
scalednconv(void)
{
	if(cpuserver && conf.npage*BY2PG >= 128*MB)
		return Nchans*4;
	return Nchans;
}
