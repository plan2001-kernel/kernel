#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
#include	"../drivers/usb/usbcore.h"

enum
{
	Usbmax		= 16,
	Usblistmax	= 256,
	Usbreadmax	= 64*1024,
	Usbioargmax	= 4096,
	Usbgenmax	= 0x7fffff,
};

typedef struct Usbhand Usbhand;
typedef struct Usbslot Usbslot;
typedef struct Usbhtab Usbhtab;

struct Usbhand
{
	Ref ref;
	int	kind;
	int	mode;		/* USBOREAD/USBOWRITE/USBOrdwr for data; -1 otherwise */
	Ep	*ep;
};

struct Usbslot
{
	ulong	gen;
	Usbhand	*h;
};

struct Usbhtab
{
	Lock lk;
	Usbslot	s[Usbmax];
};

static void
usbgenbump(Usbslot *s)
{
	s->gen++;
	if(s->gen == 0 || s->gen > Usbgenmax)
		s->gen = 1;
}

static Usbhtab*
usbgettab(int create)
{
	Usbhtab *ht;

	if((ht = up->usbhandles) != nil)
		return ht;
	if(!create)
		return nil;
	ht = smalloc(sizeof *ht);
	up->usbhandles = ht;
	return ht;
}

static Usbhand*
usblookup(Usbhtab *ht, int h, int kind)
{
	Usbslot *s;
	Usbhand *uh;
	ulong gen;
	int i;

	if(ht == nil || h <= 0)
		error(Ebadarg);
	i = (h & 0xff) - 1;
	gen = (ulong)h >> 8;
	if(i < 0 || i >= Usbmax || gen == 0)
		error(Ebadarg);
	lock(&ht->lk);
	s = &ht->s[i];
	uh = s->h;
	if(uh == nil || s->gen != gen || (kind >= 0 && uh->kind != kind)){
		unlock(&ht->lk);
		error(Ebadarg);
	}
	incref(&uh->ref);
	unlock(&ht->lk);
	return uh;
}

static void
usbhandput(Usbhand *uh)
{
	Ep *ep;

	if(uh == nil)
		return;
	if(decref(&uh->ref) != 0)
		return;
	ep = uh->ep;
	uh->ep = nil;
	if(ep != nil){
		if(uh->kind == USBDATA)
			usbcore_dataclose(ep);
		usbcore_putep(ep);
	}
	free(uh);
}

static int
usbinstall(Usbhtab *ht, Usbhand *uh)
{
	Usbslot *s;
	int i;

	lock(&ht->lk);
	for(i = 0; i < Usbmax; i++){
		s = &ht->s[i];
		if(s->h == nil){
			usbgenbump(s);
			s->h = uh;
			unlock(&ht->lk);
			return (int)((s->gen << 8) | (i + 1));
		}
	}
	unlock(&ht->lk);
	return -1;
}

static void
usbchecksvc(int h)
{
	Usbhand *uh;

	uh = usblookup(up->usbhandles, h, USBSVC);
	usbhandput(uh);
}

static int
usbepinstall(Ep *ep, int kind, int mode)
{
	Usbhtab *ht;
	Usbhand *uh;
	int kh, opened;

	ht = up->usbhandles;
	uh = smalloc(sizeof *uh);
	uh->ref.ref = 1;
	uh->kind = kind;
	uh->mode = kind == USBDATA ? mode : -1;
	uh->ep = ep;
	opened = 0;
	if(waserror()){
		if(opened)
			usbcore_dataclose(ep);
		usbcore_putep(ep);
		free(uh);
		nexterror();
	}
	if(kind == USBDATA){
		usbcore_dataopen(ep, mode);
		opened = 1;
	}
	if((kh = usbinstall(ht, uh)) < 0)
		error("too many USB handles");
	poperror();
	return kh;
}

static void
usbdropall(Proc *p)
{
	Usbhtab *ht;
	Usbhand *h[Usbmax];
	int i;

	if((ht = p->usbhandles) == nil)
		return;
	p->usbhandles = nil;
	lock(&ht->lk);
	for(i = 0; i < Usbmax; i++){
		h[i] = ht->s[i].h;
		ht->s[i].h = nil;
	}
	unlock(&ht->lk);
	for(i = 0; i < Usbmax; i++)
		usbhandput(h[i]);
	free(ht);
}

void
usbprocexit(Proc *p)
{
	usbdropall(p);
}

void
usbprocexec(Proc *p)
{
	usbdropall(p);
}

uintptr
sysusbopen(sysva_list list)
{
	Usbhtab *ht;
	Usbhand *uh;
	int kh;

	USED(list);
	usbcore_init();
	ht = usbgettab(1);
	uh = smalloc(sizeof *uh);
	uh->ref.ref = 1;
	uh->kind = USBSVC;
	uh->mode = -1;
	uh->ep = nil;
	if((kh = usbinstall(ht, uh)) < 0){
		usbhandput(uh);
		error("too many USB handles");
	}
	return kh;
}

uintptr
sysusblist(sysva_list list)
{
	Usbentry *ents;
	Usbhand *uh;
	void *buf;
	long n, sz;
	int h, r, total;

	h = sysva_arg(list, int);
	buf = sysva_arg(list, void*);
	n = sysva_arg(list, long);
	uh = usblookup(up->usbhandles, h, USBSVC);
	if(n < 0){
		usbhandput(uh);
		error(Ebadarg);
	}
	total = usbcore_list(nil, 0);
	if(buf == nil || n == 0){
		usbhandput(uh);
		return total;
	}
	if(n > Usblistmax)
		n = Usblistmax;
	if(n > total)
		n = total;
	if(n == 0){
		usbhandput(uh);
		return 0;
	}
	sz = n * sizeof(Usbentry);
	ents = smalloc(sz);
	if(waserror()){
		free(ents);
		usbhandput(uh);
		nexterror();
	}
	if(!okaddr((uintptr)buf, sz, 1))
		error(Ebadarg);
	r = usbcore_list(ents, n);
	if(r < 0)
		error(Eio);
	if(r > n)
		r = n;
	memmove(buf, ents, r * sizeof(Usbentry));
	poperror();
	free(ents);
	usbhandput(uh);
	return r;
}

uintptr
sysusbepopen(sysva_list list)
{
	Ep *ep;
	int h, devno, epno, kind, mode;

	h = sysva_arg(list, int);
	devno = sysva_arg(list, int);
	epno = sysva_arg(list, int);
	kind = sysva_arg(list, int);
	mode = sysva_arg(list, int);
	usbchecksvc(h);
	if(kind != USBDATA && kind != USBCTL)
		error(Ebadarg);
	if(mode != USBOREAD && mode != USBOWRITE && mode != USBOrdwr)
		error(Ebadarg);
	ep = usbcore_getep(devno, epno);
	if(ep == nil)
		error(Ebadarg);
	return usbepinstall(ep, kind, mode);
}

uintptr
sysusbepname(sysva_list list)
{
	Ep *ep;
	char *name, *s;
	int h, kind, mode;

	h = sysva_arg(list, int);
	name = sysva_arg(list, char*);
	kind = sysva_arg(list, int);
	mode = sysva_arg(list, int);
	usbchecksvc(h);
	if(kind != USBDATA && kind != USBCTL)
		error(Ebadarg);
	if(mode != USBOREAD && mode != USBOWRITE && mode != USBOrdwr)
		error(Ebadarg);
	if(name == nil || !okaddr((uintptr)name, 1, 0))
		error(Ebadarg);
	s = validnamedup(name, 0);
	if(waserror()){
		free(s);
		nexterror();
	}
	ep = usbcore_getepname(s);
	free(s);
	poperror();
	if(ep == nil)
		error(Ebadarg);
	return usbepinstall(ep, kind, mode);
}

uintptr
sysusbread(sysva_list list)
{
	Usbhand *uh;
	char *kb;
	void *buf;
	long n, r;
	int h;

	h = sysva_arg(list, int);
	buf = sysva_arg(list, void*);
	n = sysva_arg(list, long);
	uh = usblookup(up->usbhandles, h, USBDATA);
	if(uh->mode != USBOREAD && uh->mode != USBOrdwr){
		usbhandput(uh);
		error(Ebadusefd);
	}
	if(n < 0 || n > Usbreadmax){
		usbhandput(uh);
		error(Ebadarg);
	}
	if(n == 0){
		usbhandput(uh);
		return 0;
	}
	kb = smalloc(n);
	if(waserror()){
		free(kb);
		usbhandput(uh);
		nexterror();
	}
	if(!okaddr((uintptr)buf, n, 1))
		error(Ebadarg);
	r = usbcore_read(uh->ep, kb, n);
	if(r < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	if(r > n)
		r = n;
	memmove(buf, kb, r);
	poperror();
	free(kb);
	usbhandput(uh);
	return r;
}

uintptr
sysusbwrite(sysva_list list)
{
	Usbhand *uh;
	char *kb;
	void *buf;
	long n, r;
	int h;

	h = sysva_arg(list, int);
	buf = sysva_arg(list, void*);
	n = sysva_arg(list, long);
	uh = usblookup(up->usbhandles, h, USBDATA);
	if(uh->mode != USBOWRITE && uh->mode != USBOrdwr){
		usbhandput(uh);
		error(Ebadusefd);
	}
	if(n < 0 || n > Usbreadmax){
		usbhandput(uh);
		error(Ebadarg);
	}
	if(n == 0){
		usbhandput(uh);
		return 0;
	}
	kb = smalloc(n);
	if(waserror()){
		free(kb);
		usbhandput(uh);
		nexterror();
	}
	if(!okaddr((uintptr)buf, n, 0))
		error(Ebadarg);
	memmove(kb, buf, n);
	r = usbcore_write(uh->ep, kb, n);
	if(r < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	poperror();
	free(kb);
	usbhandput(uh);
	return r;
}

static long
usbioargmin(int req)
{
	switch(req){
	case USBnew:
		return sizeof(struct Usbnew);
	case USBnewdev:
		return sizeof(struct Usbnewdev);
	case USBhub:
		return sizeof(struct Usbhub);
	case USBdebug:
	case USBmaxpkt:
	case USBntds:
	case USBpollival:
	case USBtimeout:
	case USBhz:
	case USBsamplesz:
	case USBsampledelay:
	case USBuframes:
		return sizeof(int);
	case USBname:
		return 1;
	default:
		return 0;
	}
}

uintptr
sysusbioctl(sysva_list list)
{
	Usbhand *uh;
	char *kb;
	void *arg;
	long narg, r;
	int h, req;

	h = sysva_arg(list, int);
	req = sysva_arg(list, int);
	arg = sysva_arg(list, void*);
	narg = sysva_arg(list, long);
	uh = usblookup(up->usbhandles, h, -1);
	if(req < USBdebug || req > USBinfo || uh->kind == USBDATA){
		usbhandput(uh);
		error(Ebadarg);
	}
	if(uh->kind == USBSVC && req != USBdebug){
		usbhandput(uh);
		error(Ebadarg);
	}
	if(narg < 0 || narg > Usbioargmax || narg < usbioargmin(req) ||
	   (narg > 0 && arg == nil)){
		usbhandput(uh);
		error(Ebadarg);
	}
	if(narg == 0){
		if(waserror()){
			usbhandput(uh);
			nexterror();
		}
		if(uh->kind == USBSVC)
			r = usbcore_globalioctl(req, nil, 0);
		else
			r = usbcore_ioctl(uh->ep, req, nil, 0);
		if(r < 0)
			error(up->errstr[0] != 0 ? up->errstr : Eio);
		poperror();
		usbhandput(uh);
		return r;
	}
	kb = smalloc(narg);
	if(waserror()){
		free(kb);
		usbhandput(uh);
		nexterror();
	}
	if(!okaddr((uintptr)arg, narg, 1))
		error(Ebadarg);
	memmove(kb, arg, narg);
	if(uh->kind == USBSVC)
		r = usbcore_globalioctl(req, kb, narg);
	else
		r = usbcore_ioctl(uh->ep, req, kb, narg);
	if(r < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	memmove(arg, kb, narg);
	poperror();
	free(kb);
	usbhandput(uh);
	return r;
}

uintptr
sysusbclose(sysva_list list)
{
	Usbhtab *ht;
	Usbhand *uh;
	Usbslot *s;
	ulong gen;
	int h, i;

	h = sysva_arg(list, int);
	if(h <= 0)
		error(Ebadarg);
	i = (h & 0xff) - 1;
	gen = (ulong)h >> 8;
	if(i < 0 || i >= Usbmax || gen == 0)
		error(Ebadarg);
	ht = up->usbhandles;
	if(ht == nil)
		error(Ebadarg);
	lock(&ht->lk);
	s = &ht->s[i];
	uh = s->h;
	if(uh == nil || s->gen != gen){
		unlock(&ht->lk);
		error(Ebadarg);
	}
	s->h = nil;
	usbgenbump(s);
	unlock(&ht->lk);
	usbhandput(uh);
	return 0;
}
