/*
 * USB kernel core.
 *
 * Owns the live endpoint table, device allocation, root-hub emulation,
 * endpoint controls and HCI controller probing.  The legacy #u Chan/Qid
 * adapter has been removed; native syscalls and HCI drivers use this core
 * directly.
 *
 * A separate user program (usbd) still enumerates the bus, configures
 * endpoints and starts devices.  Nothing here enumerates USB.
 */

#include	"u.h"
#include	"../../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../../core/error.h"
#include	"../drivers/usb/usbcore.h"

typedef struct Hcitype Hcitype;
typedef struct Epctl Epctl;

enum
{
	/* Hub feature selectors */
	Rportenable	= 1,
	Rportreset	= 4,
	Rportpower	= 8,
	Rbhportreset	= 28,

	/* newdev reply: "epN.M" */
	Repnamelen	= 16,
	/* endpoint names are limited as before (up->genbuf) */
	Namelen		= 128,
};

struct Hcitype
{
	char*	type;
	int	(*reset)(Hci*);
};

struct Epctl
{
	int	cmd;
	int	nb;
	int	tt;
	int	mode;
	int	speed;
	int	port;
	int	ttt;
	int	mtt;
	int	has_port;
	int	has_tt;
	int	val;
	char*	s;
	long	ns;
	char*	reply;
};

static char Edetach[] = "device is detached";
static char Edisabled[] = "device is not enabled";
static char Enotconf[] = "endpoint not configured";
static char Ebadtype[] = "bad endpoint type";
static char Ebadport[] = "bad hub port number";
static char Enotahub[] = "not a hub";

char Estalled[] = "endpoint stalled";

char *usbmodename[] =
{
	[OREAD]	"r",
	[OWRITE]	"w",
	[ORDWR]	"rw",
};

static char *spname[] =
{
	[Superspeed]	"super",
	[Fullspeed]	"full",
	[Lowspeed]	"low",
	[Highspeed]	"high",
	[Nospeed]	"no",
};

static int	debug;
static Hcitype	hcitypes[Nhcis];
static Hci*	hcis[Nhcis];
static QLock	epslck;		/* add, del, lookup endpoints */
static Ep*	eps[Neps];	/* all endpoints known */
static int	epmax;		/* 1 + last endpoint index used  */

static void	usbinit(void);
static Ep*	getep(int);
static void	putep(Ep*);
static long	epctlrun(Ep*, Epctl*);
static void	setglobaldebug(int);

/*
 * Is there something like this in a library? should it be?
 */
char*
seprintdata(char *s, char *se, uchar *d, int n)
{
	int i, l;

	s = seprint(s, se, " %#p[%d]: ", d, n);
	l = n;
	if(l > 10)
		l = 10;
	for(i=0; i<l; i++)
		s = seprint(s, se, " %2.2ux", d[i]);
	if(l < n)
		s = seprint(s, se, "...");
	return s;
}

void
addhcitype(char* t, int (*r)(Hci*))
{
	static int ntype;

	if(ntype == Nhcis)
		panic("too many USB host interface types");
	hcitypes[ntype].type = t;
	hcitypes[ntype].reset = r;
	ntype++;
}

static int
rootport(Ep *ep, int port)
{
	Hci *hp;
	Udev *hub;
	uint mask;
	int rootport;

	hp = ep->hp;
	hub = ep->dev;
	if(hub->depth >= 0)
		return hub->rootport;

	mask = hp->superspeed;
	if(hub->speed != Superspeed)
		mask = (1<<hp->nports)-1 & ~mask;

	for(rootport = 1; mask != 0; rootport++){
		if(mask & 1){
			if(--port == 0)
				return rootport;
		}
		mask >>= 1;
	}

	return 0;
}

static Ep*
epalloc(Hci *hp)
{
	Ep *ep;
	int i;

	for(i = 0; i < Neps; i++)
		if(eps[i] == nil)
			break;
	if(i == Neps)
		error("out of endpoints");

	ep = malloc(sizeof(Ep));
	if(ep == nil)
		error(Enomem);
	ep->ref.ref = 1;
	ep->idx = i;
	if(epmax <= i)
		epmax = i+1;
	eps[i] = ep;
	ep->hp = hp;
	ep->maxpkt = 8;
	ep->ntds = 1;
	ep->uframes = ep->samplesz = ep->pollival = ep->hz = 0; /* make them void */
	ep->toggle[0] = ep->toggle[1] = 0;
	return ep;
}

static Ep*
getep(int i)
{
	Ep *ep;

	if(i < 0 || i >= epmax || eps[i] == nil)
		return nil;
	qlock(&epslck);
	ep = eps[i];
	if(ep != nil && incref(&ep->ref) == 1){
		/* race with putep() below, its dead. */
		decref(&ep->ref);
		ep = nil;
	}
	qunlock(&epslck);
	return ep;
}

static void
putep(Ep *ep)
{
	Udev *d;
	Ep *next;

	for(; ep != nil; ep = next){
		if(decref(&ep->ref) > 0)
			return;
		assert(ep->inuse == 0);
		d = ep->dev;
		deprint("usb: ep%d.%d %#p released\n", d->nb, ep->nb, ep);
		qlock(&ep->ep0->qlk);
		qlock(&epslck);
		assert(d->eps[ep->nb] == ep);
		d->eps[ep->nb] = nil;
		assert(eps[ep->idx] == ep);
		eps[ep->idx] = nil;
		if(ep->idx == epmax-1){
			while(epmax > 0 && eps[epmax-1] == nil)
				epmax--;
		}
		qunlock(&epslck);
		qunlock(&ep->ep0->qlk);
		if(ep->ep0 != ep)
			next = ep->ep0;
		else {
			next = d->hub != nil? d->hub->eps[0]: nil;
			if(ep->hp->hci.devclose != nil)
				(*ep->hp->hci.devclose)(d);
			free(d);
		}
		free(ep->info);
		free(ep->name);
		free(ep);
	}
}

static int
newusbid(Hci *)
{
	Ep *ep;
	Udev *d;
	int i, id;
	char map[128];

	memset(map, 0, sizeof(map));
	map[0] = 1;	/* dont use */

	id = 1;
	for(i = 0; i < epmax; i++) {
		if((ep = eps[i]) == nil)
			continue;

		d = ep->dev;
		map[d->nb % nelem(map)] = 1;

		/* try use largest id + 1 */
		if(d->nb >= id)
			id = d->nb + 1;
	}

	/* once we ran out, use lowest possible free id */
	if(id >= nelem(map)){
		for(i = 0; i < nelem(map); i++)
			if(map[i] == 0)
				return i;
	}

	return id;
}

/*
 * Create endpoint 0 for a new device on hub.
 * hub must be locked.
 */
static Ep*
newdev(Hci *hp, Ep *hub, int port, int speed)
{
	Ep *ep;
	Udev *d;

	d = malloc(sizeof(Udev));
	if(d == nil)
		error(Enomem);
	if(waserror()){
		free(d);
		nexterror();
	}

	d->state = Dconfig;		/* address not yet set */
	d->addr = 0;
	d->port = port;
	d->speed = speed;

	d->nports = 0;
	d->rootport = d->routestr = 0;
	d->ttport = d->ttt = d->mtt = 0;
	d->tthub = nil;

	d->rhrepl = -1;
	d->rhresetport = 0;

	if(hub != nil){
		d->hub = hub->dev;
		d->depth = d->hub->depth+1;
		if(d->depth > 0){
			assert(d->depth <= 5);
			d->routestr = d->hub->routestr | (d->port<15? d->port: 15) << 4*(d->depth-1);
			if(speed < Highspeed){
				if(d->hub->speed == Highspeed){
					d->tthub = d->hub;
					d->ttport = port;
				}else {
					d->tthub = d->hub->tthub;
					d->ttport = d->hub->ttport;
				}
			}
		}
		d->rootport = rootport(hub, port);
		if(d->rootport <= 0)
			error(Ebadport);
	} else {
		d->hub = nil;
		d->depth = -1;
	}

	eqlock(&epslck);
	if(waserror()){
		qunlock(&epslck);
		nexterror();
	}
	d->nb = newusbid(hp);

	ep = epalloc(hp);
	ep->mode = ORDWR;
	ep->ttype = Tctl;
	ep->tmout = Xfertmout;

	if(speed == Superspeed)
		ep->maxpkt = 512;
	else if(speed != Lowspeed)
		ep->maxpkt = 64;	/* assume full speed */
	else
		ep->maxpkt = 8;

	ep->nb = 0;
	ep->ep0 = ep;			/* no ref counted here */
	ep->dev = d;
	d->eps[0] = ep;
	if(hub != nil)
		incref(&hub->ref);
	incref(&ep->ref);
	qunlock(&epslck);
	poperror();
	poperror();

	dprint("newdev %#p ep%d.%d %#p\n", d, d->nb, ep->nb, ep);
	return ep;
}

/*
 * Create a new endpoint for the device
 * accessed via the given endpoint 0.
 * ep0 must be locked.
 */
static Ep*
newdevep(Ep *ep0, int nb, int tt, int mode)
{
	Ep *ep;
	Udev *d;

	eqlock(&epslck);
	if(waserror()){
		qunlock(&epslck);
		nexterror();
	}
	d = ep0->dev;
	if(d->eps[nb] != nil)
		error(Einuse);
	ep = epalloc(ep0->hp);
	ep->mode = mode;
	ep->ttype = tt;
	/* set defaults */
	switch(tt){
	case Tctl:
		ep->tmout = Xfertmout;
		break;
	case Tintr:
		ep->pollival = 10;
		break;
	case Tiso:
		ep->tmout = Xfertmout;
		ep->pollival = 10;
		ep->samplesz = 1;
		break;
	}
	ep->nb = nb;
	ep->ep0 = ep0;
	ep->dev = d;
	d->eps[nb] = ep;
	incref(&ep0->ref);
	incref(&ep->ref);
	qunlock(&epslck);
	poperror();

	deprint("newdevep ep%d.%d %#p\n", d->nb, ep->nb, ep);
	return ep;
}

/*
 * µs for the given transfer, for bandwidth allocation.
 * This is a very rough worst case for what 5.11.3
 * of the usb 2.0 spec says.
 * Also, we are using maxpkt and not actual transfer sizes.
 * Only when we are sure we
 * are not exceeding b/w might we consider adjusting it.
 */
static ulong
usbload(int speed, int maxpkt)
{
	enum{ Hostns = 1000, Hubns = 333 };
	ulong l;
	ulong bs;

	l = 0;
	bs = 10UL * maxpkt;
	switch(speed){
	case Highspeed:
		l = 55*8*2 + 2 * (3 + bs) + Hostns;
		break;
	case Fullspeed:
		l = 9107 + 84 * (4 + bs) + Hostns;
		break;
	case Lowspeed:
		l = 64107 + 2 * Hubns + 667 * (3 + bs) + Hostns;
		break;
	default:
		print("usbload: bad speed %d\n", speed);
		/* let it run */
	}
	return l / 1000UL;	/* in µs */
}

static void
isotiming(Ep *ep)
{
	long spp, max;

	switch(ep->dev->speed){
	case Fullspeed:
		max = 1024;
		break;
	case Highspeed:
		max = 3*1024;
		break;
	case Superspeed:
		max = 48*1024;
		break;
	default:
		error(Egreg);
	}

	if(ep->ntds <= 0)
		error(Egreg);
	max /= ep->ntds;
	if(max < ep->samplesz)
		error(Egreg);
	if(ep->pollival <= 0)
		error(Egreg);

	if(ep->hz <= 0){
		spp = ep->maxpkt / ep->samplesz;
		spp *= ep->ntds;
		if(ep->dev->speed == Fullspeed || ep->dev->speed == Lowspeed)
			ep->hz = (1000 * spp) / ep->pollival;
		else
			ep->hz = (8000 * spp) / ep->pollival;
		if(ep->hz <= 0)
			error(Egreg);
	}
	if(ep->dev->speed == Fullspeed || ep->dev->speed == Lowspeed)
		spp = (ep->hz * ep->pollival + 999) / 1000;
	else
		spp = (ep->hz * ep->pollival + 7999) / 8000;
	spp /= ep->ntds;
	ep->maxpkt = spp * ep->samplesz;
	if(ep->maxpkt > max){
		print("ep%d.%d: maxpkt %ld > %ld for %s, truncating\n",
			ep->dev->nb, ep->nb,
			ep->maxpkt, max, spname[ep->dev->speed]);
		ep->maxpkt = max;
	}
}

/*
 * Fake root hub emulation.
 */
static long
rhubread(Ep *ep, void *a, long n)
{
	uchar b[8];
	Udev *dev;

	dev = ep->dev;

	if(dev->depth >= 0 || ep->nb != 0 || n < 2 || dev->rhrepl == -1)
		return -1;

	b[0] = dev->rhrepl;
	b[1] = dev->rhrepl>>8;
	b[2] = dev->rhrepl>>16;
	b[3] = dev->rhrepl>>24;
	b[4] = dev->rhrepl>>32;
	b[5] = dev->rhrepl>>40;
	b[6] = dev->rhrepl>>48;
	b[7] = dev->rhrepl>>56;

	dev->rhrepl = -1;

	if(n > sizeof(b))
		n = sizeof(b);
	memmove(a, b, n);

	return n;
}

static long
rhubwrite(Ep *ep, void *a, long n)
{
	uchar *s;
	int cmd;
	int feature;
	int port;
	Hci *hp;
	Udev *dev;

	hp = ep->hp;
	dev = ep->dev;

	if(dev->depth >= 0 || ep->nb != 0)
		return -1;
	if(n != Rsetuplen)
		error("root hub is a toy hub");
	s = a;
	if(s[Rtype] != (Rh2d|Rclass|Rother) && s[Rtype] != (Rd2h|Rclass|Rother))
		error("root hub is a toy hub");

	/* terminate previous port reset */
	port = dev->rhresetport;
	if(port > 0){
		dev->rhresetport = 0;

		/*
		 * Some controllers have to clear reset and set enable manually.
		 * We assume that clearing reset will transition the bus from
		 * SE0 to idle state, and setting enable starts transmitting
		 * SOF packets (keep alive).
		 *
		 * The delay between clearing reset and setting enable must
		 * not exceed 3ms as this makes devices suspend themselfs.
		 */
		if(hp->hci.portreset != nil){
			(*hp->hci.portreset)(hp, port, 0);
			tsleep(&up->sleep, return0, nil, 2);
		}
		if(hp->hci.portenable != nil){
			(*hp->hci.portenable)(hp, port, 1);
			tsleep(&up->sleep, return0, nil, 2);
		}
	}

	cmd = s[Rreq];
	feature = GET2(s+Rvalue);
	port = rootport(ep, GET2(s+Rindex));
	if(port <= 0)
		error(Ebadport);

	dev->rhrepl = 0;
	switch(feature){
	case Rportpower:
		if(hp->hci.portpower == nil)
			break;
		(*hp->hci.portpower)(hp, port, cmd == Rsetfeature);
		break;
	case Rportenable:
		if(cmd != Rclearfeature || hp->hci.portenable == nil)
			break;
		(*hp->hci.portenable)(hp, port, 0);
		break;
	case Rbhportreset:
		if(cmd != Rsetfeature || hp->hci.bhportreset == nil)
			break;
		(*hp->hci.bhportreset)(hp, port, 1);
		break;
	case Rportreset:
		if(cmd != Rsetfeature || hp->hci.portreset == nil)
			break;
		(*hp->hci.portreset)(hp, port, 1);
		/* port reset in progress */
		dev->rhresetport = port;
		break;
	case Rgetstatus:
		if(hp->hci.portstatus == nil)
			break;
		dev->rhrepl = (*hp->hci.portstatus)(hp, port);
		break;
	}
	return n;
}

Ep*
usbcore_getep(int devno, int epno)
{
	Ep *ep;
	int i;

	qlock(&epslck);
	for(i = 0; i < epmax; i++){
		ep = eps[i];
		if(ep == nil || ep->dev->nb != devno || ep->nb != epno)
			continue;
		if(incref(&ep->ref) == 1){
			/* racing with putep() */
			decref(&ep->ref);
			ep = nil;
		}
		qunlock(&epslck);
		return ep;
	}
	qunlock(&epslck);
	return nil;
}

Ep*
usbcore_getepname(char *name)
{
	Ep *ep;
	int i;

	if(name == nil)
		return nil;
	for(i = 0; i < Neps; i++){
		ep = getep(i);
		if(ep == nil)
			continue;
		eqlock(&ep->qlk);
		if(ep->name != nil && strcmp(ep->name, name) == 0){
			qunlock(&ep->qlk);
			return ep;
		}
		qunlock(&ep->qlk);
		putep(ep);
	}
	return nil;
}

void
usbcore_putep(Ep *ep)
{
	putep(ep);
}

int
usbcore_dataopen(Ep *ep, int mode)
{
	eqlock(&ep->qlk);
	if(waserror()){
		qunlock(&ep->qlk);
		nexterror();
	}
	if(ep->ttype == Tnone)
		error(Enotconf);
	if(mode != OREAD && ep->mode == OREAD)
		error(Eperm);
	if(mode != OWRITE && ep->mode == OWRITE)
		error(Eperm);
	if(ep->inuse)
		error(Einuse);
	if(ep->dev->state == Ddetach)
		error(Edetach);
	ep->clrhalt = 0;
	if(ep->ttype == Tctl)
		ep->dev->rhrepl = -1;
	if(ep->ttype == Tiso)
		isotiming(ep);
	if(ep->load == 0 && ep->dev->speed != Superspeed)
		ep->load = usbload(ep->dev->speed, ep->maxpkt);
	(*ep->hp->hci.epopen)(ep);
	ep->inuse = 1;
	qunlock(&ep->qlk);
	poperror();
	return 0;
}

void
usbcore_dataclose(Ep *ep)
{
	qlock(&ep->qlk);
	if(!ep->inuse){
		qunlock(&ep->qlk);
		return;
	}
	if(!waserror()){
		if(ep->hp->hci.epstop != nil)
			(*ep->hp->hci.epstop)(ep);
		if(ep->hp->hci.epclose != nil)
			(*ep->hp->hci.epclose)(ep);
		poperror();
	}
	ep->inuse = 0;
	ep->aux = nil;
	qunlock(&ep->qlk);
}

long
usbcore_read(Ep *ep, void *a, long n)
{
	int nr;

	if(ep->mode == OWRITE || ep->inuse == 0)
		error(Ebadusefd);
	if(ep->dev->state == Ddetach)
		error(Edetach);

	switch(ep->ttype){
	case Tnone:
		error(Enotconf);
	case Tctl:
		nr = rhubread(ep, a, n);
		if(nr >= 0)
			return nr;
		/* else fall */
	default:
	Again:
		nr = (*ep->hp->hci.epread)(ep, a, n);
		if(nr == 0 && ep->ttype == Tiso && ep->dev->state != Ddetach){
			tsleep(&up->sleep, return0, nil, 2*ep->pollival);
			goto Again;
		}
		break;
	}
	return nr;
}

long
usbcore_write(Ep *ep, void *a, long n)
{
	int nr;

	if(ep->mode == OREAD || ep->inuse == 0)
		error(Ebadusefd);
	if(ep->dev->state == Ddetach)
		error(Edetach);

	switch(ep->ttype){
	case Tnone:
		error(Enotconf);
	case Tctl:
		nr = rhubwrite(ep, a, n);
		if(nr >= 0)
			return nr;
		/* else fall */
	default:
		(*ep->hp->hci.epwrite)(ep, a, n);
	}
	return n;
}

int
usbcore_list(Usbentry *e, int n)
{
	Ep *ep;
	int i, total;

	total = 0;
	for(i = 0; i < Neps; i++){
		ep = getep(i);
		if(ep == nil)
			continue;
		if(e != nil && total < n){
			eqlock(&ep->qlk);
			e[total].idx = ep->idx;
			e[total].devno = ep->dev->nb;
			e[total].epno = ep->nb;
			e[total].ttype = ep->ttype;
			e[total].mode = ep->mode;
			e[total].speed = ep->dev->speed;
			e[total].state = ep->dev->state;
			e[total].inuse = ep->inuse;
			e[total].maxpkt = ep->maxpkt;
			if(ep->name != nil)
				snprint(e[total].name, sizeof e[total].name, "%s", ep->name);
			else
				e[total].name[0] = 0;
			qunlock(&ep->qlk);
		}
		total++;
		usbcore_putep(ep);
	}
	return total;
}

/*
 * Many endpoint ctls. simply update the portable representation
 * of the endpoint. The actual controller driver will look
 * at them to setup the endpoints as dictated.
 */
static long
epctlrun(Ep *ep, Epctl *r)
{
	int i, l;
	char *s, *b;
	Ep *nep;
	Udev *d;

	switch(r->cmd){
	case USBnew:
	case USBnewdev:
	case USBhub:
	case USBreset:
	case USBaddress:
		if(ep != ep->ep0)
			error("allowed only on a setup endpoint");
		break;
	}
	eqlock(&ep->qlk);
	if(waserror()){
		qunlock(&ep->qlk);
		nexterror();
	}
	d = ep->dev;
	if(d->state == Ddetach)
		error(Edetach);
	switch(r->cmd){
	case USBnew:
		if(r->nb < 0 || r->nb >= Ndeveps)
			error("bad endpoint number");
		if(r->tt <= Tnone || r->tt >= Nttypes)
			error(Ebadtype);
		if(r->mode != OREAD && r->mode != OWRITE && r->mode != ORDWR)
			error("unknown i/o mode");
		nep = newdevep(ep, r->nb, r->tt, r->mode);
		nep->debug = ep->debug;
		putep(nep);
		break;
	case USBnewdev:
		if(d->state != Denabled)
			error(Edisabled);
		l = r->speed;
		if(l != Fullspeed && l != Lowspeed && l != Highspeed && l != Superspeed)
			error("speed must be full|low|high|super");
		if(l != d->speed)
			switch(d->speed){
			case Highspeed:
				if(l == Fullspeed)
					break;
				/* no break */
			case Fullspeed:
				if(l == Lowspeed)
					break;
				/* no break */
			default:
				error("wrong speed for hub/device");
			}
		if(r->port < 1 || r->port > d->nports)
			error(Ebadport);
		nep = newdev(ep->hp, ep, r->port, l);
		/* next read request will read
		 * the name for the new endpoint
		 */
		if(r->reply != nil)
			snprint(r->reply, Repnamelen, "ep%d.%d", nep->dev->nb, nep->nb);
		putep(nep);
		break;
	case USBdetach:
		if(d->depth < 0)
			error("can't detach a root hub");
		d->state = Ddetach;
		qunlock(&ep->qlk);
		poperror();
		for(i = 0; i < nelem(d->eps); i++){
			ep = d->eps[i];
			if(ep != nil){
				qlock(&ep->qlk);
				if(ep->inuse && ep->hp->hci.epstop != nil && !waserror()){
					(*ep->hp->hci.epstop)(ep);
					poperror();
				}
				qunlock(&ep->qlk);
				putep(ep);
			}
		}
		return 0;
	case USBreset:
		if(d->state != Denabled)
			error(Edisabled);
		d->state = Dreset;
		break;
	case USBdebug:
	case USBdebugep:
		ep->debug = r->val;
		print("usb: ep%d.%d debug %d\n", d->nb, ep->nb, ep->debug);
		break;
	case USBclrhalt:
		ep->clrhalt = 1;
		break;
	case USBaddress:
		if(d->state != Dconfig)
			error("address already set");
		if(d->addr == 0)
			d->addr = d->nb & Devmax;
		d->state = Denabled;
		break;
	case USBhub:
		if(!r->has_port)
			error(Ebadctl);
		if(ep->inuse)
			error(Einuse);
		if(d->depth >= 5)
			error("hub depth exceeded");
		if(r->port < 1 || r->port > 127
		|| (r->port > 15 && d->speed == Superspeed))
			error(Ebadport);
		d->nports = r->port;
		if(d->speed == Highspeed && r->has_tt){
			d->ttt = r->ttt & 3;
			d->mtt = r->mtt != 0;
		}
		break;
	case USBmaxpkt:
		if(ep->inuse)
			error(Einuse);
		l = r->val;
		if(l < 1 || l > 1024)
			error("maxpkt not in [1:1024]");
		ep->maxpkt = l;
		ep->hz = 0; /* recalculate */
		break;
	case USBntds:
		if(ep->inuse)
			error(Einuse);
		l = r->val;
		if(l < 1 || l > 3)
			error("ntds not in [1:3]");
		ep->ntds = l;
		ep->hz = 0; /* recalculate */
		break;
	case USBpollival:
		if(ep->ttype != Tintr && ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = r->val;
		if(d->speed == Fullspeed || d->speed == Lowspeed){
			if(l < 1 || l > 255)
				error("pollival not in [1:255]");
		} else {
			if(l < 1 || l > 16)
				error("pollival power not in [1:16]");
			l = 1 << l-1;
		}
		ep->pollival = l;
		ep->hz = 0; /* recalculate */
		break;
	case USBtimeout:
		if(ep->ttype == Tiso || ep->ttype == Tctl)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = r->val;
		if(l != 0 && l < Xfertmout)
			l = Xfertmout;
		ep->tmout = l;
		break;
	case USBhz:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = r->val;
		if(l <= 0 || l > 1000000000)
			error("hz not in [1:1000000000]");
		ep->hz = l;
		break;
	case USBsamplesz:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = r->val;
		if(l <= 0 || l > 8)
			error("samplesz not in [1:8]");
		ep->samplesz = l;
		break;
	case USBsampledelay:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		ep->sampledelay = r->val;
		break;
	case USBuframes:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = r->val;
		if(l != 0 && l != 1)
			error("uframes not in [0:1]");
		ep->uframes = l;
		break;
	case USBname:
		if(ep->inuse)
			error(Einuse);
		s = r->s;
		if(s == nil || r->ns <= 0 || s[r->ns-1] != 0)
			error(Ebadctl);
		if(strlen(s) >= Namelen)
			error(Etoolong);
		validname(s, 0);
		kstrdup(&ep->name, s);
		break;
	case USBinfo:
		if(r->ns < 0)
			error(Ebadctl);
		b = smalloc(r->ns+1);
		if(waserror()){
			free(b);
			nexterror();
		}
		if(r->ns > 0)
			memmove(b, r->s, r->ns);
		poperror();
		b[r->ns] = 0;
		s = strchr(b, '\n');
		if(s != nil)
			*s = 0;
		free(ep->info);
		ep->info = b;
		break;
	default:
		error(Ebadctl);
	}
	qunlock(&ep->qlk);
	poperror();
	return 0;
}

long
usbcore_ioctl(Ep *ep, int req, void *arg, long narg)
{
	Epctl r;
	struct Usbnew *un;
	struct Usbnewdev *und;
	struct Usbhub *uh;
	int *ip;

	memset(&r, 0, sizeof(r));
	r.cmd = req;

	switch(req){
	case USBnew:
		un = arg;
		if(un == nil)
			error(Ebadctl);
		r.nb = un->nb;
		r.tt = un->type;
		r.mode = un->mode;
		break;
	case USBnewdev:
		und = arg;
		if(und == nil)
			error(Ebadctl);
		r.speed = und->speed;
		r.port = und->port;
		r.reply = und->reply;
		break;
	case USBhub:
		uh = arg;
		if(uh == nil)
			error(Ebadctl);
		r.port = uh->nports;
		r.ttt = uh->ttt;
		r.mtt = uh->mtt;
		r.has_port = 1;
		r.has_tt = 1;
		break;
	case USBname:
		if(arg == nil || narg <= 0 || ((char*)arg)[narg-1] != 0)
			error(Ebadctl);
		r.s = arg;
		r.ns = narg;
		break;
	case USBinfo:
		if(narg < 0 || (narg > 0 && arg == nil))
			error(Ebadctl);
		r.s = arg;
		r.ns = narg;
		break;
	case USBdebug:
	case USBdebugep:
	case USBmaxpkt:
	case USBntds:
	case USBpollival:
	case USBtimeout:
	case USBhz:
	case USBsamplesz:
	case USBsampledelay:
	case USBuframes:
		ip = arg;
		if(ip == nil)
			error(Ebadctl);
		r.val = *ip;
		break;
	case USBdetach:
	case USBreset:
	case USBclrhalt:
	case USBaddress:
		break;
	default:
		error(Ebadctl);
	}
	return epctlrun(ep, &r);
}

long
usbcore_globalioctl(int req, void *arg, long narg)
{
	if(req != USBdebug || arg == nil)
		error(Ebadctl);
	USED(narg);
	setglobaldebug(*(int*)arg);
	return 0;
}

static void
setglobaldebug(int d)
{
	Ep *ep;
	int i;

	debug = d;
	for(i = 0; i < epmax; i++)
		if((ep = getep(i)) != nil){
			if(ep->hp->hci.debug != nil)
				(*ep->hp->hci.debug)(ep->hp, debug);
			putep(ep);
		}
}

static Hci*
hciprobe(int cardno, int ctlrno)
{
	Hci *hp;
	char *type;

	ddprint("hciprobe %d %d\n", cardno, ctlrno);
	hp = malloc(sizeof(Hci));
	if(hp == nil){
		print("hciprobe: out of memory\n");
		return nil;
	}

	hp->ctlrno = ctlrno;
	hp->tbdf = BUSUNKNOWN;

	if(cardno < 0){
		if(isaconfig("usb", ctlrno, &hp->isa) == 0){
			free(hp);
			return nil;
		}
		for(cardno = 0; cardno < Nhcis; cardno++){
			if(hcitypes[cardno].type == nil)
				break;
			type = hp->isa.type;
			if(type==nil || *type==0)
				type = "uhci";
			if(cistrcmp(hcitypes[cardno].type, type) == 0)
				break;
		}
	}

	if(cardno >= Nhcis || hcitypes[cardno].type == nil){
		free(hp);
		return nil;
	}
	dprint("%s...", hcitypes[cardno].type);
	if((*hcitypes[cardno].reset)(hp) < 0){
		free(hp);
		return nil;
	}
	return hp;
}

void
usbcore_reset(void)
{
	static QLock resetlck;
	static int inited;
	int cardno, ctlrno;
	Hci *hp;

	qlock(&resetlck);
	if(inited){
		qunlock(&resetlck);
		return;
	}
	if(getconf("*nousbprobe")){
		qunlock(&resetlck);
		return;
	}
	/*
	 * This runs from main() before procinit0()'s first up exists.
	 * waserror()/poperror() need up->errlab, so the lock-releasing
	 * handler is only installed once a Proc is available; the probe
	 * itself is the same at boot as it was before the guard.
	 */
	if(up != nil && waserror()){
		qunlock(&resetlck);
		nexterror();
	}
	dprint("usbreset\n");

	for(ctlrno = 0; ctlrno < Nhcis; ctlrno++)
		if((hp = hciprobe(-1, ctlrno)) != nil)
			hcis[ctlrno] = hp;
	cardno = ctlrno = 0;
	while(cardno < Nhcis && ctlrno < Nhcis && hcitypes[cardno].type != nil)
		if(hcis[ctlrno] != nil)
			ctlrno++;
		else{
			hp = hciprobe(cardno, ctlrno);
			if(hp == nil)
				cardno++;
			hcis[ctlrno++] = hp;
		}
	if(hcis[Nhcis-1] != nil)
		print("usbreset: bug: Nhcis (%d) too small\n", Nhcis);
	inited = 1;
	if(up != nil)
		poperror();
	qunlock(&resetlck);
}

static int
numbits(uint n)
{
	int c = 0;
	while(n != 0){
		c++;
		n = (n-1) & n;
	}
	return c;
}

static void
usbinit(void)
{
	Hci *hp;
	Ep *d;
	int ctlrno, n;
	char buf[Namelen];

	dprint("usbinit\n");
	for(ctlrno = 0; ctlrno < Nhcis; ctlrno++){
		hp = hcis[ctlrno];
		if(hp == nil)
			continue;

		if(hp->hci.init != nil){
			if(waserror()){
				print("usbinit: %s %d: %s\n", hp->isa.type, ctlrno, up->errstr);
				continue;
			}
			(*hp->hci.init)(hp);
			poperror();
		}

		hp->superspeed &= (1<<hp->nports)-1;
		n = hp->nports - numbits(hp->superspeed);
		if(n > 0){
			d = newdev(hp, nil, 0, hp->highspeed?Highspeed:Fullspeed);		/* new LS/FS/HS root hub */
			d->dev->nports = n;
			d->dev->state = Denabled;	/* although addr == 0 */
			snprint(buf, sizeof(buf), "roothub ports %d", n);
			kstrdup(&d->info, buf);
			putep(d);
		}
		n = numbits(hp->superspeed);
		if(n > 0){
			d = newdev(hp, nil, 0, Superspeed);	/* new SS root hub */
			d->dev->nports = n;
			d->dev->state = Denabled;	/* although addr == 0 */
			snprint(buf, sizeof(buf), "roothub ports %d", n);
			kstrdup(&d->info, buf);
			putep(d);
		}
	}
}

void
usbcore_init(void)
{
	static QLock initlck;
	static int inited;

	qlock(&initlck);
	if(inited){
		qunlock(&initlck);
		return;
	}
	if(waserror()){
		qunlock(&initlck);
		nexterror();
	}
	usbinit();
	inited = 1;
	poperror();
	qunlock(&initlck);
}

void
usbcore_shutdown(void)
{
	Hci *hp;
	int i;

	for(i = 0; i < Nhcis; i++){
		hp = hcis[i];
		if(hp == nil)
			continue;
		if(hp->hci.shutdown == nil)
			print("usb: no shutdown function for %s\n", hp->isa.type);
		else
			(*hp->hci.shutdown)(hp);
	}
}
