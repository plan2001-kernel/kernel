/*
 * Volatile objects and ObjectFIFO.
 *
 * A volatile object has RAM-only state and a scope-qualified identity
 * (incarnation, space, oid).  Creating one never reads or writes GE/O.  The
 * identity is allocated monotonically for this boot; the incarnation changes
 * on every boot, so a stale identity can never resolve to a new object.
 *
 * ObjectFIFO is a general message-framed channel of object references.  A
 * published message is one authorized reference plus an optional opaque
 * envelope; the core does not parse IP, TCP, USB or application payloads.
 * Each subscription has an independent bounded queue (TEE/fanout), so one
 * slow subscriber cannot block a fast one or the sender.  Overflow drops the
 * oldest queued reference for that subscription and is counted.
 *
 * Handles are process-local, generation-tagged and rights-bearing.  They are
 * not OIDs, pointers or ambient authority.  Process exec/exit releases all
 * handles.  A FIFO can be shut down explicitly; this marks every subscription
 * closed and wakes blocked receivers.  FIFO shutdown is a transport
 * operation, not capability revocation of a GE/O grant.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
#include	"../core/pipe/pipecore.h"
#include	"../drivers/uart/uartnative.h"
#include	"../drivers/usb/usbcore.h"
#include	"../amd64/kbdapi.h"
#include	"../../runtime/abi/sys.h"
#include	"../../runtime/abi/vobj.h"
#include	"../../runtime/abi/obj.h"
#include	"../../runtime/abi/namespace.h"
#include	"../../runtime/abi/ioctl.h"
#include	"../core/vobjint.h"
#include	"lifediag.h"
#include	"ocontext.h"
#ifdef	PLAN2001_LIFECYCLE_DIAG
#include	"env/envapi.h"
#endif

#ifdef	PLAN2001_LIFECYCLE_DIAG
static int
injectfault(char *point)
{
	return envfaulttake("PLAN2001_FAULT", point);
}
#endif

enum {
	Vobjmax		= 4096,
	Vobjbytesmax	= 32*1024*1024,
	Vfifomax	= 256,
	Vfifosubsmax	= 64,
	Vglobalmsgs	= 65536,
	Venvmax		= 64,
	Vsubcapdef	= 64,
	Vgenmax		= 0x7fffff,
	Vnamemax	= 32,
	/* Native-family compatibility limits, now enforced over the one
	 * process-local Vhand table instead of separate tables. */
	Vpipeendmax	= 32,
	Vuartslotmax	= 16,
	Vkbdslotmax	= 4,
	Vusbslotmax	= 4,
	Vusbepslotmax	= 16,
	Vconsoleslotmax = 4,
};

_Static_assert(OBJ_USB_EP_DATA == USBDATA && OBJ_USB_EP_CTL == USBCTL,
	"device option kinds must match the usbcore values");
_Static_assert(OREAD == USBOREAD && OWRITE == USBOWRITE && ORDWR == USBOrdwr,
	"device option modes must match the usbcore values");
_Static_assert(sizeof(ObjDeviceEntry) == 36, "device discovery ABI");
_Static_assert(sizeof(ObjDeviceOpenOptions) == 16, "device options ABI");
_Static_assert(sizeof(ObjOpenDeviceReq) == 32 + OBJ_DEVICE_OPTMAX,
	"device open ABI");
_Static_assert(OBJ_DEVICE_OPTMAX >= sizeof(ObjDeviceOpenOptions),
	"device option bound");

typedef struct Vobj	Vobj;
typedef struct Vmsg	Vmsg;
typedef struct Vsub	Vsub;
typedef struct Vfifo	Vfifo;
typedef struct Vhand	Vhand;
typedef struct Vhslot	Vhslot;
typedef struct Vhtab	Vhtab;
typedef struct Vpipe	Vpipe;
typedef struct Vuart	Vuart;
typedef struct Vkbd	Vkbd;
typedef struct Vusbep	Vusbep;

struct Vmsg {
	Vmsg	*next;
	Vobj	*obj;
	int rights; /* immutable sender authority ceiling */
	ulong	envlen;
	uchar	env[Venvmax];
};

struct Vsub {
	Ref	ref;
	Lock	lk;		/* protects queue, counters and closed */
	QLock	recvlk;	/* one receiver per subscription */
	Rendez	rd;
	Vsub	*next;		/* fifo attachment under Vfifo.lk */
	Vfifo	*fifo; /* immutable after attachment; a Vhand pins its FIFO */
	int	capacity;
	int	len;
	int	closed;
	ulong	dropped;
	Vmsg	*head;
	Vmsg	*tail;
};

struct Vfifo {
	QLock	lk;		/* protects subs list and closed */
	Vsub	*subs;
	int	nsubs;
	int	closed;
	int	capacity;
	uvlong	sends;
	uvlong	drops;
};

struct Vhand {
	Ref	ref;
	Vobj	*obj;
	Vsub	*sub;
	int	rights;
};

/* One endpoint of a native Pipe pair.  pipecorepair() accounts two endpoint
 * closes; the peer Vobj takes the other close when its Handle is installed,
 * or is released here if end 0 is closed first. */
struct Vpipe {
	Pipe	*p;
	int	end;
	Vobj	*peer;		/* end 0 only: not-yet-opened peer object */
	Lock	lk;
};

struct Vuart {
	int	id;
	int	mode;
	int	opened;
};

struct Vkbd {
	int	held;
};

/* One opened USB endpoint.  DATA owns the usbcore data-open; CTL only holds
 * the referenced Ep, matching the legacy usbepinstall() semantics. */
struct Vusbep {
	Ep	*ep;
	int	kind;
	int	mode;
};

typedef struct Vdevice Vdevice;
struct Vdevice {
    int family;
    Veth *ether;
    QLock lk;              /* serialize one complete request/completion */
    Ep *ep;
    int opened;
    int failed;            /* hardware outcome uncertain: close/reopen */
    Vobj *reply;           /* one immutable completion; not a FIFO */
    uvlong sequence;
};

struct Vhslot {
	ulong	gen;
	Vhand	*h;
};

struct Vhtab {
	Lock	lk;
	Vhslot	s[Vhandmax];
};

typedef struct Vstats Vstats;
struct Vstats {
	Lock	lk;
	int	nobj;
	uvlong	nbytes;
	int	nfifo;
	int	nmsg;
};

static Vstats vstats;
static Lock vidlock;
static uvlong vincarnation;
static uvlong vnextoid;
static Lock vgenlock;
static ulong vnextgen;

static uvlong
vincarnationget(void)
{
	uvlong inc;
	vlong ns;

	lock(&vidlock);
	inc = vincarnation;
	unlock(&vidlock);
	if(inc != 0)
		return inc;
	ns = todget(nil, nil);
	lock(&vidlock);
	if(vincarnation == 0)
		vincarnation = (uvlong)ns == 0 ? 1 : (uvlong)ns;
	inc = vincarnation;
	unlock(&vidlock);
	return inc;
}

static uvlong
voidget(void)
{
	uvlong o;

	lock(&vidlock);
	o = ++vnextoid;
	if(o == 0)
		o = ++vnextoid;
	unlock(&vidlock);
	return o;
}

static int
vreserve(ulong bytes, int fifo)
{
	lock(&vstats.lk);
	if(vstats.nobj >= Vobjmax || vstats.nbytes + bytes > Vobjbytesmax
	|| (fifo && vstats.nfifo >= Vfifomax)){
		unlock(&vstats.lk);
		return -1;
	}
	vstats.nobj++;
	vstats.nbytes += bytes;
	if(fifo)
		vstats.nfifo++;
	unlock(&vstats.lk);
	return 0;
}

static void
vrelease(ulong bytes, int fifo)
{
	lock(&vstats.lk);
	vstats.nobj--;
	vstats.nbytes -= bytes;
	if(fifo)
		vstats.nfifo--;
	unlock(&vstats.lk);
}

static int
vmsgreserve(int n)
{
	int rc = 0;

	lock(&vstats.lk);
	if(vstats.nmsg + n > Vglobalmsgs)
		rc = -1;
	else
		vstats.nmsg += n;
	unlock(&vstats.lk);
	return rc;
}

static void
vmsgrelease(int n)
{
	lock(&vstats.lk);
	vstats.nmsg -= n;
	if(vstats.nmsg < 0)
		vstats.nmsg = 0;
	unlock(&vstats.lk);
}

static Vobj*
vobjalloc(int kind, uvlong space, ulong size)
{
	Vobj *o;

	if(vreserve(size, kind == VkindFifo) != 0)
		return nil;
	o = mallocz(sizeof *o, 1);
	if(o == nil){
		vrelease(size, kind == VkindFifo);
		return nil;
	}
	if(size != 0){
		o->data = mallocz(size, 0);
		if(o->data == nil){
			free(o);
			vrelease(size, kind == VkindFifo);
			return nil;
		}
	}
	if(kind == VkindFifo){
		Vfifo *f = mallocz(sizeof *f, 1);
		if(f == nil){
			free(o->data);
			free(o);
			vrelease(size, 1);
			return nil;
		}
		f->capacity = Vsubcapdef;
		o->fifo = f;
	}
	o->ref.ref = 1;
	o->kind = kind;
	o->flags = VflagReadonly;
	o->provider = kind;
	o->size = size;
	o->space = space;
	o->incarnation = vincarnationget();
	o->oid = voidget();
	return o;
}

static void
vsubput(Vsub*);
static void
vsubshutd(Vsub*);
static int
vinstall(Vhtab*, Vhand*);
static Vhand*
vlookup(int, int);
static void
vgecaplimit(Ohand*, int);

void
vobjput(Vobj *o)
{
	if(o == nil || decref(&o->ref) != 0)
		return;
	if(o->kind == VkindFifo && o->fifo != nil){
		Vfifo *f = o->fifo;
		Vsub *s, *next;

		/* The creator is the last holder; detach any surviving subs. */
		qlock(&f->lk);
		s = f->subs;
		f->subs = nil;
		f->nsubs = 0;
		f->closed = 1;
		for(next = s; next != nil; next = next->next)
			next->fifo = nil;	/* detach from the FIFO being freed */
		qunlock(&f->lk);
		while(s != nil){
			next = s->next;
			s->next = nil;
			vsubshutd(s);
			vsubput(s);
			s = next;
		}
		free(f);
	}else if(o->kind == VkindPipe && o->priv != nil){
		Vpipe *vp = o->priv;
		Vobj *peer;

		o->priv = nil;
		lock(&vp->lk);
		peer = vp->peer;
		vp->peer = nil;
		unlock(&vp->lk);
		if(peer != nil)
			vobjput(peer);
		if(vp->p != nil)
			pipecoreclose(vp->p, vp->end);
		free(vp);
	}else if(o->kind == VkindUart && o->priv != nil){
		Vuart *vu = o->priv;

		o->priv = nil;
		if(vu->opened){
			vu->opened = 0;
			if(waserror())
				;
			else{
				uartnclose(vu->id);
				poperror();
			}
		}
		free(vu);
	}else if(o->kind == VkindKbd && o->priv != nil){
		Vkbd *vk = o->priv;

		o->priv = nil;
		if(vk->held){
			vk->held = 0;
			kbdscancoderelease();
		}
		free(vk);
	}else if(o->kind == VkindDevice && o->priv != nil){
        Vdevice *d = o->priv;

        o->priv = nil;
        if(d->ether != nil)
            vetherclose(d->ether);
        if(d->reply != nil)
            vobjput(d->reply);
        if(d->ep != nil){
            if(d->opened)
                usbcore_dataclose(d->ep);
            usbcore_putep(d->ep);
        }
        free(d);
    }else if(o->kind == VkindUsbEp && o->priv != nil){
		Vusbep *ve = o->priv;

		o->priv = nil;
		if(ve->ep != nil){
			if(ve->kind == USBDATA)
				usbcore_dataclose(ve->ep);
			usbcore_putep(ve->ep);
			ve->ep = nil;
		}
		free(ve);
	}else if(o->kind == VkindNamespaceControl && o->priv != nil){
		lifecontrol(o->priv, -1);
		closepgrp(o->priv);
	}else if(o->kind == VkindGe || o->kind == VkindSpace || o->kind == VkindNamespaceView){
		if(o->priv != nil)
			oshandput(o->priv);
	}
	{
		ulong size = o->size;
		int fifo = o->kind == VkindFifo;
		free(o->data);
		free(o);
		vrelease(size, fifo);
	}
}

static void
vhandinit(Vhand *h, Vobj *o, Vsub *s, int rights)
{
	h->ref.ref = 1;
	h->obj = o;
	incref(&o->ref);
	h->sub = s;
	if(s != nil)
		incref(&s->ref);
	h->rights = rights;
}

static Vhand*
vhandalloc(Vobj *o, Vsub *s, int rights)
{
	Vhand *h;

	h = smalloc(sizeof *h);
	vhandinit(h, o, s, rights);
	return h;
}

static void
vsubshutd(Vsub *s)
{
	Vmsg *m, *next;

	if(s == nil)
		return;
	lock(&s->lk);
	if(s->closed){
		unlock(&s->lk);
		return;
	}
	s->closed = 1;
	m = s->head;
	s->head = s->tail = nil;
	s->len = 0;
	unlock(&s->lk);
	while(m != nil){
		next = m->next;
		vobjput(m->obj);
		free(m);
		vmsgrelease(1);
		m = next;
	}
	wakeup(&s->rd);
}

static void
vsudetach(Vsub *s)
{
 Vfifo *f;
 Vsub **l;
 int removed = 0;
 /* The caller's Vhand pins its FIFO object until this returns. */
 if(s == nil || (f = s->fifo) == nil) return;
 qlock(&f->lk);
 if(s->fifo == f){
  for(l=&f->subs; *l; l=&(*l)->next) if(*l==s){
   *l=s->next;s->next=nil;f->nsubs--;removed=1;break;
  }
 }
 qunlock(&f->lk);
 if(removed) vsubput(s); /* exactly the attachment reference */
}

static void
vsubput(Vsub *s)
{
	if(s == nil || decref(&s->ref) != 0)
		return;
	vsubshutd(s);
	free(s);
}

static void
vhandput(Vhand *h)
{
	if(h == nil)
		return;
	if(decref(&h->ref) != 0)
		return;
	if(h->sub != nil){
		vsudetach(h->sub);
		vsubshutd(h->sub);
		vsubput(h->sub);
	}
	vobjput(h->obj);
	free(h);
}

static Vhtab*
vtabfor(Proc *p, int create)
{
	Vhtab *t;

	if((t = p->vobjhandles) != nil || !create)
		return t;
	t = smalloc(sizeof *t);
	if(t != nil)
		p->vobjhandles = t;
	return t;
}

static Vhtab*
vgettab(int create)
{
	return vtabfor(up, create);
}

Vobj*
vobjnewkind(int kind, ulong size)
{
	return vobjalloc(kind, 0, size);
}

void
vobjsetpriv(Vobj *o, void *priv)
{
	if(o != nil)
		o->priv = priv;
}

int
vobjkindof(Vobj *o)
{
	return o == nil ? -1 : o->kind;
}

ulong
vobjsize(Vobj *o)
{
	return o == nil ? 0 : o->size;
}

int
vobjinstallobj(Vobj *o, int rights)
{
	Vhtab *t;
	Vhand *h;
	int handle;

	if(o == nil)
		error(Ebadarg);
	if((t = vgettab(1)) == nil)
		error(Enomem);
	h = vhandalloc(o, nil, rights);
	handle = vinstall(t, h);
	if(handle < 0){
		vhandput(h);
		error("vobj: too many handles");
	}
	return handle;
}

Vobj*
vobjholdobj(int handle, int rights)
{
	Vhand *h;
	Vobj *o;

	h = vlookup(handle, rights);
	o = h->obj;
	incref(&o->ref);
	vhandput(h);
	return o;
}

int
vobjhandlesbykind(Proc *p, int kind)
{
	Vhtab *t;
	int i, n = 0;

	if(p == nil || (t = p->vobjhandles) == nil)
		return 0;
	lock(&t->lk);
	for(i = 0; i < Vhandmax; i++)
		if(t->s[i].h != nil && t->s[i].h->obj->kind == kind)
			n++;
	unlock(&t->lk);
	return n;
}

static ulong
vnewgen(void)
{
	ulong g;

	lock(&vgenlock);
	if(vnextgen == Vgenmax){
		unlock(&vgenlock);
		return 0; /* vinstall failure uses the normal ownership unwind */
	}
	g = ++vnextgen;
	unlock(&vgenlock);
	return g;
}

static int
vinstall(Vhtab *t, Vhand *h)
{
	int i;
	ulong g = vnewgen();
 if(g == 0) return -1;

	lock(&t->lk);
	for(i = 0; i < Vhandmax; i++)
		if(t->s[i].h == nil)
			break;
	if(i == Vhandmax){
		unlock(&t->lk);
		return -1;
	}
	t->s[i].h = h;
	t->s[i].gen = g;
	unlock(&t->lk);
	return (int)((g << 8) | (i + 1));
}

static Vhand*
vlookupin(Proc *p, int handle, int need)
{
	Vhtab *t;
	Vhslot *s;
	Vhand *h;
	ulong gen;
	int i, bad;

	if(handle <= 0)
		error(Ebadarg);
	i = (handle & 0xff) - 1;
	gen = (ulong)handle >> 8;
	if(i < 0 || i >= Vhandmax || gen == 0)
		error(Ebadarg);
	if(p == nil || (t = p->vobjhandles) == nil)
		error(Ebadarg);
	lock(&t->lk);
	s = &t->s[i];
	h = s->h;
	bad = h == nil || s->gen != gen;
	if(!bad && (h->rights & need) != need)
		bad = 2;
	if(!bad)
		incref(&h->ref);
	unlock(&t->lk);
	if(bad == 2)
		error(Eperm);
	if(bad)
		error(Ebadarg);
  if(h->obj->kind==VkindGe || h->obj->kind==VkindSpace){
   if(waserror()){vhandput(h);nexterror();}
   oscheckhand(h->obj->priv);
   poperror();
  }
	return h;
}

static Vhand*
vlookup(int handle, int need)
{
	return vlookupin(up, handle, need);
}

static Vfifo*
vgetfifo(Vhand *h)
{
	if(h->obj->kind != VkindFifo || h->obj->fifo == nil)
		return nil;
	return h->obj->fifo;
}

void
vobjprocexit(Proc *p)
{
	Vhtab *t;
	Vhand *h[Vhandmax];
	int i;

	if((t = p->vobjhandles) == nil)
		return;
	p->vobjhandles = nil;
	lock(&t->lk);
	for(i = 0; i < Vhandmax; i++){
		h[i] = t->s[i].h;
		t->s[i].h = nil;
	}
	unlock(&t->lk);
	for(i = 0; i < Vhandmax; i++)
		vhandput(h[i]);
	free(t);
}

void
vobjprocexec(Proc *p)
{
	vobjprocexit(p);
}

/*
 * Shared process-creation Handle delegation.  RFORKEX and SPAWN both build a
 * bounded Vinhand request list; this validates the complete plan against the
 * parent before any child is created and pins every source Vhand so neither
 * its rights nor its object lifetime can change between validation and commit.
 * The parent's handle table is never mutated.
 */
int
vobjinheritprepare(Vinherit *vi, Vinhand *list, int n)
{
	int i, slot, used[Vhandmax];
	uvlong rights;
	Vhand *h;

	if(vi == nil || n < 0 || n > Vhandmax || (n > 0 && list == nil))
		error(Ebadarg);
	memset(vi, 0, sizeof *vi);
	memset(used, 0, sizeof used);
	/*
	 * One function-wide cleanup path.  H holds at most the Handle currently
	 * being validated; ownership moves to VI (and H is cleared) only after
	 * every check passes.  On any error the current Handle is released once
	 * and every previously pinned entry is released by vi release, which
	 * also zeroes the plan so a caller's own error path cannot double-free.
	 */
	h = nil;
	if(waserror()){
		if(h != nil)
			vhandput(h);
		vobjinheritrelease(vi);
		nexterror();
	}
	for(i = 0; i < n; i++){
		h = vlookup(list[i].handle, Vdelegate);
		if(h->sub != nil)
			error("object: subscriptions cannot be inherited");
		rights = list[i].rights == 0 ? (uvlong)h->rights : list[i].rights;
		if((rights & ~(uvlong)h->rights) != 0 ||
		   (rights & ~(uvlong)Vall) != 0)
			error(Eperm);
		if(list[i].dstslot != 0){
			slot = list[i].dstslot - 1;
			if(slot < 0 || slot >= Vhandmax)
				error(Ebadarg);
		}else{
			for(slot = 0; slot < Vhandmax; slot++)
				if(!used[slot])
					break;
			if(slot == Vhandmax)
				error("vobj: too many inherited handles");
		}
		if(used[slot])
			error(Ebadarg);
		used[slot] = 1;
		vi->e[i].src = h;
		vi->e[i].srchandle = list[i].handle;
		vi->e[i].rights = (int)rights;
		vi->e[i].slot = slot;
		vi->e[i].gen = 0;
		vi->n = i + 1;
		h = nil;		/* VI owns the reference from here */
	}
	poperror();
	return vi->n;
}

/* Install the validated plan into a child whose Handle table is empty.
 * Fresh child-local generations are assigned so a parent Handle number never
 * becomes valid in the child merely by coinciding with a destination slot. */
void
vobjinheritcommit(Proc *child, Vinherit *vi)
{
	Vhtab *t;
	Vhand *h;
	Vobj *src, *o;
	Ohand *doh;
	ulong gen;
	int i;

	if(child == nil)
		error(Ebadarg);
	if(vi == nil || vi->n == 0)
		return;
	if(child->vobjhandles != nil)
		error(Ebadarg);
	t = smalloc(sizeof *t);
	child->vobjhandles = t;
	for(i = 0; i < vi->n; i++){
		gen = vnewgen();
		if(gen == 0)
			error("vobj: handle generation exhausted");
		src = vi->e[i].src->obj;
		if(src->kind == VkindSpace){
			/*
			 * A bootstrap Space Ohand belongs to the boot owner.
			 * An explicitly inherited Vexecute capability gets a
			 * delegated caps=2 Space copy instead, so the child can
			 * OEXECUTE under the rights it was actually granted.
			 *
			 * DOH and O change ownership through this block: the
			 * error path releases whichever objects are still owned
			 * here, exactly once, before the child Vhand exists.
			 */
			doh = nil;
			o = nil;
			doh = oscopycapdelegate((Ohand*)src->priv,
				((Ohand*)src->priv)->rights);
			if(waserror()){
				if(o != nil){
					o->priv = nil;
					vobjput(o);
				}
				oshandput(doh);
				nexterror();
			}
#ifdef	PLAN2001_LIFECYCLE_DIAG
			if(injectfault("inherit-space-ohand"))
				error("vobj: injected delegated Ohand failure");
#endif
			vgecaplimit(doh, vi->e[i].rights);
			o = vobjalloc(VkindSpace, src->space, 0);
			if(o == nil)
				error(Enomem);
#ifdef	PLAN2001_LIFECYCLE_DIAG
			if(injectfault("inherit-space-vobj"))
				error("vobj: injected delegated Vobj failure");
#endif
			o->provider = VopenSpace;
			o->priv = doh;
			o->space = ossid(doh);
			o->oid = 0;
			doh = nil;		/* O owns the Ohand now */
#ifdef	PLAN2001_LIFECYCLE_DIAG
			if(injectfault("inherit-space-vhand"))
				error("vobj: injected delegated Vhand failure");
#endif
			poperror();
			h = vhandalloc(o, nil, vi->e[i].rights);
			vobjput(o);
		}else
			h = vhandalloc(src, nil, vi->e[i].rights);
		vi->e[i].gen = gen;
		t->s[vi->e[i].slot].h = h;
		t->s[vi->e[i].slot].gen = gen;
	}
}

/* Child Handle assigned to plan entry INDEX, or 0 when INDEX is out of range.
 * The mapping is per entry, not per source, so a source listed more than once
 * yields a distinct child Handle for each destination. */
int
vobjinheritchildhandle(Vinherit *vi, int index)
{
	if(vi == nil || index < 0 || index >= vi->n)
		return 0;
	return (int)((vi->e[index].gen << 8) | (vi->e[index].slot + 1));
}

/* Resolve a named parent Handle to its unique plan destination.  Returns the
 * child Handle, 0 when the source is absent, or -1 when it appears more than
 * once (ambiguous).  SPAWN's stdoutpipe uses this and rejects both absent and
 * ambiguous sources instead of silently choosing the first match. */
int
vobjinheritsrchandle(Vinherit *vi, int srchandle)
{
	int i, found;

	found = -1;
	for(i = 0; vi != nil && i < vi->n; i++){
		if(vi->e[i].srchandle != srchandle)
			continue;
		if(found >= 0)
			return -1;
		found = i;
	}
	if(found < 0)
		return 0;
	return (int)((vi->e[found].gen << 8) | (vi->e[found].slot + 1));
}

void
vobjinheritrelease(Vinherit *vi)
{
	int i;

	if(vi == nil)
		return;
	for(i = 0; i < vi->n; i++)
		vhandput(vi->e[i].src);
	memset(vi, 0, sizeof *vi);
}

int
vobjcreat(int kind, uvlong space, void *buf, long n, VobjId *id)
{
	Vobj *o;
	Vhtab *t;
	Vhand *h;
	int handle, rights;

	if(kind != VkindBytes && kind != VkindFifo)
		error(Ebadarg);
	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	if(kind == VkindFifo){
		if(buf != nil || n < 0 || n > Vsubcapmax)
			error(Ebadarg);
	}else if(n < 0 || n > Vobjpaymax || (n > 0 && buf == nil))
		error(Ebadarg);
	if((t = vgettab(1)) == nil)
		error(Enomem);
	o = vobjalloc(kind, space, kind == VkindFifo ? 0 : n);
	if(o == nil)
		error(Enomem);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	if(kind == VkindFifo){
		((Vfifo*)o->fifo)->capacity = n == 0 ? Vsubcapdef : (int)n;
		rights = Vsend|Vsubscribe|Vmeta|Vcontrol|Vdelegate;
	}else{
		if(n > 0)
			memmove(o->data, buf, n);
		rights = Vread|Vmeta|Vsend|Vdelegate;
	}
	h = vhandalloc(o, nil, rights);
	handle = vinstall(t, h);
	if(handle < 0){
		vhandput(h);
		error("vobj: too many handles");
	}
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	poperror();
	vobjput(o);			/* handle owns the remaining reference */
	return handle;
}

/*
 * Explicit persistent creation behind OBJCREATE(kind=Ge).  The scope
 * argument is an authorized process-local VkindSpace Handle, never a
 * volatile incarnation tag.  The new object gets its own capability copy so
 * later READ/WRITE/COMMIT use the same Ohand authority and gate.
 */
int
vobjrightsfromo(int o)
{
 int r = Vmeta;
 if(o & Oread) r |= Vread;
 if(o & Owrite) r |= Vwrite;
 if(o & Ocreate) r |= Vcreate;
 if(o & Ocommit) r |= Vcommit;
 if(o & Oexecute) r |= Vexecute;
 if(o & Odelegate) r |= Vdelegate;
 return r;
}

static int
vgerights(Ohand *cap)
{
 return vobjrightsfromo(oshandrights(cap));
}

/* Defense in depth for a newly opened native provider, before publication.
 * Later Vhand copies also check their own independent attenuation ceiling. */
static void
vgecaplimit(Ohand *cap, int rights)
{
 int r=0;
 if(rights & Vread) r|=Oread;
 if(rights & Vwrite) r|=Owrite;
 if(rights & Vcreate) r|=Ocreate;
 if(rights & Vcommit) r|=Ocommit;
 if(rights & Vexecute) r|=Oexecute;
 if(rights & Vdelegate) r|=Odelegate;
 cap->rights &= r;
}

/* Namespace and GE share the OBJ handle table and its generation/rights checks. */
int
vobjinstallohand(Ohand *oh, int kind)
{
 Vobj *o;
 int handle, rights;
 if(oh == nil || (kind != VkindSpace && kind != VkindGe && kind != VkindNamespaceView)) error(Ebadarg);
 rights = kind == VkindNamespaceView ? Vmeta|Vcontrol : vgerights(oh);
 o = vobjalloc(kind, kind == VkindNamespaceView ? 0 : ossid(oh), 0);
 if(o == nil) error(Enomem);
 o->priv = oh;
 o->oid = oh->oid;
 o->provider = kind == VkindNamespaceView ? 0 : (kind == VkindSpace ? VopenSpace : VopenGe);
 if(waserror()){o->priv=nil;vobjput(o);nexterror();}
 handle = vobjinstallobj(o, rights);
 poperror();
 vobjput(o);
 return handle;
}

Ohand*
vobjholdohand(int handle, int kind, int rights)
{
 Vhand *h;
 Ohand *oh;
 h = vlookup(handle, rights);
 if(h->obj->kind != kind || h->obj->priv == nil){vhandput(h);error(Ebadarg);}
 oh = h->obj->priv;
 incref(&oh->ref);
 vhandput(h);
 return oh;
}

int
vobjcreatege(int spacehandle, void *buf, long n, uvlong *oidp, uvlong *spaceid)
{
 Vhand *sh, *h;
 Vobj *o = nil;
 Ohand *cap = nil;
 uvlong oid;
 int handle = -1, rights;
 if(n<0 || n>Vobjpaymax || (n>0 && buf==nil)) error(Ebadarg);
 if(n>0) validaddr((uintptr)buf,n,0);
 sh=vlookup(spacehandle,Vcreate|Vcommit);
 if(waserror()){
  if(handle>=0) vobjclosehandle(handle);
  vobjput(o);oshandput(cap);vhandput(sh);nexterror();
 }
 if(sh->obj->kind!=VkindSpace || sh->obj->priv==nil) error(Ebadarg);
 /* Reserve the returned handle and provider before durable allocation.
  * An exhausted table therefore does not create an unreachable object. */
 cap=oscopycap(sh->obj->priv,0);
 rights=vgerights(cap) & sh->rights & ~Vcreate;
 vgecaplimit(cap,rights);
 o=vobjalloc(VkindGe,ossid(cap),0);
 if(o==nil) error(Enomem);
 o->provider=VopenGe;o->priv=cap;cap=nil;
 h=vhandalloc(o,nil,rights);
 if(vgettab(1)==nil){vhandput(h);error(Enomem);}
 handle=vinstall(vgettab(1),h);
 if(handle<0){vhandput(h);error("vobj: too many handles");}
 oscreatehand(sh->obj->priv,buf,n,&oid); /* allocator publishes+syncs */
 o->oid=oid;((Ohand*)o->priv)->oid=oid;
 if(oidp) *oidp=oid;
 if(spaceid) *spaceid=o->space;
 poperror();vobjput(o);vhandput(sh);return handle;
}

/* Kernel-side identity lookup: no user address validation. */
int
vobjidentity(int handle, VobjId *id)
{
	Vhand *h;

	if(id == nil)
		error(Ebadarg);
	h = vlookup(handle, 0);
	id->incarnation = h->obj->incarnation;
	id->space = h->obj->space;
	id->oid = h->obj->oid;
	vhandput(h);
	return 0;
}

/* Commit pending GE/O writes through the handle's Ohand authority. */
int
vobjcommit(int handle, uvlong *ver)
{
	Vobj *o;
	int kind;

	o = vobjholdobj(handle, Vcommit);
	kind = o->kind;
	if(kind != VkindGe && kind != VkindSpace){
		vobjput(o);
		error(Ebadarg);
	}
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	oscommithand(o->priv, ver);
	poperror();
	vobjput(o);
	return 0;
}

/* Replace one explicitly named persistent OID through a VkindSpace Handle.
 * It uses the same Ohand Owrite authority and Space gate as OSPACEPUT, has
 * no allocation and does not commit: the pending write joins the Space
 * transaction until a separate ObjOpCommit.  A GE Handle cannot name an
 * arbitrary OID, so this is the Space-scoped form used by the normal
 * MicroPython persistence path. */
int
vobjputoid(int handle, uvlong oid, void *buf, long n)
{
	Vobj *o;

	if(n < 0 || n > 64*1024*1024 || (n > 0 && buf == nil))
		error(Ebadarg);
	o = vobjholdobj(handle, Vwrite);
	if(o->kind != VkindSpace || o->priv == nil){
		vobjput(o);
		error(Ebadarg);
	}
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	oswritehand(o->priv, oid, buf, n);
	poperror();
	vobjput(o);
	return 0;
}

int
vobjinfo(int handle, VobjInfo *out)
{
	Vhand *h;
	Vobj *o;

	if(out == nil)
		error(Ebadarg);
	validaddr((uintptr)out, sizeof *out, 1);
	h = vlookup(handle, Vmeta);
	o = h->obj;
	memset(out, 0, sizeof *out);
	out->incarnation = o->incarnation;
	out->space = o->space;
	out->oid = o->oid;
	out->size = o->size;
	out->kind = o->kind;
	out->flags = o->flags;
	out->provider = o->provider;
	out->abi_version = VOBJ_ABI_VERSION;
	out->rights = h->rights;
	out->holds = o->ref.ref;
	if((o->kind == VkindGe || o->kind == VkindSpace) && o->priv != nil){
		out->space = ossid((Ohand*)o->priv);
		out->oid = o->oid;
		osvolumeid((Ohand*)o->priv, out->volume);
	}
	/* Generic metadata only.  FIFO/subscription counters are the separate
	 * VobjFifostat contract (ObjOpStat); Info never duplicates them. */
	vhandput(h);
	return 0;
}

int
vobjclosehandle(int handle)
{
	Vhtab *t;
	Vhand *h;
	int i;
	ulong gen;

	if(handle <= 0)
		error(Ebadarg);
	i = (handle & 0xff) - 1;
	gen = (ulong)handle >> 8;
	if(i < 0 || i >= Vhandmax || gen == 0)
		error(Ebadarg);
	t = vgettab(0);
	if(t == nil)
		error(Ebadarg);
	lock(&t->lk);
	h = t->s[i].h;
	if(h == nil || t->s[i].gen != gen){
		unlock(&t->lk);
		error(Ebadarg);
	}
	t->s[i].h = nil;
	unlock(&t->lk);
	vhandput(h);
	return 0;
}

int
vobjfifosub(int fifoh, uvlong rights, long capacity)
{
	Vhand *fh;
	Vfifo *f;
	Vsub *s;
	Vhand *h;
	Vhtab *t;
	int handle, allow;

	fh = vlookup(fifoh, Vsubscribe);
	f = vgetfifo(fh);
	if(f == nil){
		vhandput(fh);
		error(Ebadarg);
	}
	if(rights == 0)
		rights = Vrecv|Vmeta;
	allow = Vrecv|Vmeta|Vsend|Vread|Vwrite|Vaccept|Vconnect|Vdelegate|Vclose|Vcreate|Vcommit;
	if(rights & ~(uvlong)allow || !(rights & (Vrecv|Vsend))){
		vhandput(fh);
		error(Ebadarg);
	}
	if((rights & Vsend) && (fh->rights & Vsend) == 0){
		vhandput(fh);
		error(Eperm);
	}
	if(capacity == 0)
		capacity = f->capacity;
	if(capacity < 1 || capacity > Vsubcapmax){
		vhandput(fh);
		error(Ebadarg);
	}
	if((t = vgettab(1)) == nil){
		vhandput(fh);
		error(Enomem);
	}
	s = mallocz(sizeof *s, 1);
	if(s == nil){
		vhandput(fh);
		error(Enomem);
	}
	s->ref.ref = 1;
	s->capacity = (int)capacity;
	h = vhandalloc(fh->obj, s, (int)rights);
	qlock(&f->lk);
	if(f->closed){
		qunlock(&f->lk);
		vhandput(h);
		vsubput(s);
		vhandput(fh);
		error("vobj: FIFO is closed");
	}
	if(f->nsubs >= Vfifosubsmax){
		qunlock(&f->lk);
		vhandput(h);
		vsubput(s);
		vhandput(fh);
		error("vobj: FIFO subscription limit");
	}
	s->next = f->subs;
	f->subs = s;
	f->nsubs++;
	incref(&s->ref);		/* fifo attachment reference */
	s->fifo = f;
	qunlock(&f->lk);
	/* The handle owns one reference; the fifo list owns the other. */
	vsubput(s);
	handle = vinstall(t, h);
	if(handle < 0){
		vhandput(h);
		vhandput(fh);
		error("vobj: too many handles");
	}
	vhandput(fh);
	return handle;
}

int
vobjfifopublish(Vobj *fifo, Vobj *object, int rights, void *env, long envlen)
{
    Vfifo *f;
    Vsub *s;
    Vmsg *nodes[Vfifosubsmax];
    int n, i, active;
    uchar envbuf[Venvmax];

    if(fifo == nil || fifo->kind != VkindFifo || fifo->fifo == nil
    || object == nil || object->kind == VkindFifo || envlen < 0
    || envlen > Venvmax || (envlen != 0 && env == nil))
        error(Ebadarg);
    f = fifo->fifo;
    if(envlen != 0)
        memmove(envbuf, env, envlen);
	/* Reserve all message nodes before publishing anything. */
	qlock(&f->lk);
	if(f->closed){
		qunlock(&f->lk);
		error("vobj: FIFO is closed");
	}
	active = f->nsubs;
	if(active > Vfifosubsmax)
		active = Vfifosubsmax;
	for(n = 0; n < active; n++){
		nodes[n] = mallocz(sizeof **nodes, 0);
		if(nodes[n] == nil){
			for(i = 0; i < n; i++){
    vobjput(nodes[i]->obj);free(nodes[i]);
   }
			qunlock(&f->lk);
			error(Enomem);
		}
		nodes[n]->next = nil;
		nodes[n]->obj = object;
 nodes[n]->rights = rights;
		incref(&object->ref);
		nodes[n]->envlen = (ulong)envlen;
		if(envlen > 0)
			memmove(nodes[n]->env, envbuf, envlen);
	}
	if(vmsgreserve(active) != 0){
		for(i = 0; i < active; i++)
			vobjput(nodes[i]->obj), free(nodes[i]);
		qunlock(&f->lk);
		error(Enomem);
	}
	f->sends++;
	i = 0;
	for(s = f->subs; s != nil && i < active; s = s->next, i++){
		Vmsg *m = nodes[i];
		lock(&s->lk);
		if(s->closed){
			/* Queue is drained; this message would be invisible. */
			unlock(&s->lk);
			vobjput(m->obj);
			free(m);
			vmsgrelease(1);
			continue;
		}
		while(s->len >= s->capacity && s->head != nil){
			Vmsg *old = s->head;
			s->head = old->next;
			if(s->head == nil)
				s->tail = nil;
			s->len--;
			s->dropped++;
			f->drops++;
			unlock(&s->lk);
			vobjput(old->obj);
			free(old);
			vmsgrelease(1);
			lock(&s->lk);
		}
		if(s->tail != nil)
			s->tail->next = m;
		else
			s->head = m;
		s->tail = m;
		s->len++;
		unlock(&s->lk);
		wakeup(&s->rd);
	}
	/* If a sub vanished while we held the fifo lock, free unused nodes. */
	for(; i < active; i++){
		vobjput(nodes[i]->obj);
		free(nodes[i]);
		vmsgrelease(1);
	}
	qunlock(&f->lk);
	return 0;
}

int
vobjfifosend(int fifoh, int objh, void *env, long envlen)
{
    Vhand *fh, *oh;
    uchar envelope[Venvmax];

    if(envlen < 0 || envlen > Venvmax || (envlen != 0 && env == nil))
        error(Ebadarg);
    if(envlen != 0){
        validaddr((uintptr)env, envlen, 0);
        memmove(envelope, env, envlen);
    }
    fh = vlookup(fifoh, Vsend);
    if(waserror()){
        vhandput(fh);
        nexterror();
    }
    if(vgetfifo(fh) == nil)
        error(Ebadarg);
    oh = vlookup(objh, Vdelegate);
    if(waserror()){
        vhandput(oh);
        nexterror();
    }
    if(oh->obj == fh->obj)
        error(Ebadarg);
    if(oh->obj->kind == VkindFifo)
        error("vobj: FIFO references are not transferable");
    vobjfifopublish(fh->obj, oh->obj, oh->rights, envelope, envlen);
    poperror();
    vhandput(oh);
    poperror();
    vhandput(fh);
    return 0;
}

static int
vsubready(void *a)
{
	Vsub *s = a;
	int ready;
 lock(&s->lk);ready=s->len>0 || s->closed;unlock(&s->lk);
 return ready;
}

int
vobjfiforecv(int subh, void *env, long envlen, int timeout, long *got)
{
	Vhand *h;
	Vsub *s;
	Vmsg *m;
	Vhtab *t;
	Vhand *nh;
	ulong start, now;
	int handle = -1, grant;
 Vobj *placeholder;

	if(envlen < 0 || envlen > Venvmax || (envlen > 0 && env == nil))
		error(Ebadarg);
	if(envlen > 0)
		validaddr((uintptr)env, envlen, 1);
	if((t = vgettab(0)) == nil)
		error(Ebadarg);
	h = vlookup(subh, Vrecv);
	if((s = h->sub) == nil){
		vhandput(h);
		error(Ebadarg);
	}
	/* One receiver per subscription: no publish can be missed by a
	 * second sleeper, and TEE separation is expressed by subscriptions. */
	qlock(&s->recvlk);
	if(waserror()){
		if(handle>=0) vobjclosehandle(handle);
  qunlock(&s->recvlk);
		vhandput(h);
		nexterror();
	}
	/* Reserve resources BEFORE removing a message. Exhaustion never needs
  * to reinsert into a queue that may have been revoked or overflowed. */
 nh = vhandalloc(h->obj, nil, 0);
 handle = vinstall(t, nh);
 if(handle<0){vhandput(nh);error("vobj: too many handles");}
 start = TK2MS(MACHP(0)->pmach.ticks);
	for(;;){
		lock(&s->lk);
		if(s->head != nil){
			m = s->head;
			s->head = m->next;
			if(s->head == nil)
				s->tail = nil;
			s->len--;
			unlock(&s->lk);
			break;
		}
		if(s->closed){
			unlock(&s->lk);
			error(Ehungup);
		}
		unlock(&s->lk);
		if(timeout == 0)
			error("vobj: receive would block");
		if(timeout > 0){
			now = TK2MS(MACHP(0)->pmach.ticks);
			if((long)(now - start) >= timeout)
				error("vobj: receive timed out");
			tsleep(&s->rd, vsubready, s, timeout - (long)(now - start));
		}else
			tsleep(&s->rd, vsubready, s, 0x7fffffff);
	}
 /* Legacy default subscriptions receive readable metadata. Explicit
  * provider rights only restrict a sender's ceiling; they never add it. */
 grant = m->rights & (Vread|Vmeta| (h->rights &
  (Vwrite|Vaccept|Vconnect|Vdelegate|Vcreate|Vcommit)));

 /* Transfer the existing message reference into the reserved slot. No
  * allocations, blocking calls or fallible publication after dequeue. */
 placeholder = nh->obj;
 lock(&t->lk);nh->obj=m->obj;nh->rights=grant;unlock(&t->lk);
 m->obj=nil;
 vobjput(placeholder);

	if(envlen > 0 && m->envlen > 0)
		memmove(env, m->env, m->envlen < (ulong)envlen ? m->envlen : (ulong)envlen);
	if(got != nil){
		long gl = (long)m->envlen;
		memmove(got, &gl, sizeof gl);
	}
	poperror();
	qunlock(&s->recvlk);
	vobjput(m->obj);		/* message reference transferred to the handle */
	vmsgrelease(1);
	free(m);
	vhandput(h);
	return handle;
}

int
vobjfifostat(int handle, VobjFifostat *out)
{
	Vhand *h;
	Vsub *s;
	Vfifo *f;

	if(out == nil)
		error(Ebadarg);
	validaddr((uintptr)out, sizeof *out, 1);
	h = vlookup(handle, Vmeta);
	memset(out, 0, sizeof *out);
	f = vgetfifo(h);
	if(f == nil){
		vhandput(h);
		error(Ebadarg);
	}
	qlock(&f->lk);
	out->subscribers = f->nsubs;
	out->capacity = f->capacity;
	out->fifo_closed = f->closed;
	out->sends = f->sends;
	out->drops = f->drops;
	qunlock(&f->lk);
	if((s = h->sub) != nil){
		lock(&s->lk);
		out->queued = s->len;
		out->capacity = s->capacity;
		out->sub_closed = s->closed;
		out->sub_dropped = s->dropped;
		unlock(&s->lk);
	}
	vhandput(h);
	return 0;
}

static void
vfifoclose(Vobj *o, int strict)
{
    Vfifo *f;
    Vsub *s, *next;

    if(o == nil || o->kind != VkindFifo || o->fifo == nil)
        error(Ebadarg);
    f = o->fifo;
    qlock(&f->lk);
    if(f->closed){
        qunlock(&f->lk);
        if(strict) error("vobj: FIFO is already closed");
        return;
    }
    f->closed = 1;
    s = f->subs;
    f->subs = nil;
    f->nsubs = 0;
    qunlock(&f->lk);
    for(; s != nil; s = next){
        next = s->next;
        s->next = nil;
        vsubshutd(s);
        vsubput(s);
    }
}

void
vobjfifoclose(Vobj *o)
{
    vfifoclose(o, 0);
}

/* GE/O capability revocation only.  A FIFO channel shutdown is explicitly
 * vobjfifoshutdown(); closing one Handle just releases its reference. */
int
vobjrevoke(int handle)
{
    Vhand *h;

    h = vlookup(handle, Vdelegate);
    if(waserror()){vhandput(h);nexterror();}
    if(h->obj->kind != VkindGe || h->obj->priv == nil)
        error(Ebadarg);
    osrevokehand(h->obj->priv);
    poperror();
    vhandput(h);
    return 0;
}

/* Close the FIFO transport for every subscriber.  This does not revoke any
 * GE/O capability and does not invalidate unrelated Handles. */
int
vobjfifoshutdown(int handle)
{
    Vhand *h;

    h = vlookup(handle, Vcontrol);
    if(waserror()){vhandput(h);nexterror();}
    if(h->obj->kind != VkindFifo)
        error(Ebadarg);
    vfifoclose(h->obj, 1);
    poperror();
    vhandput(h);
    return 0;
}

/* Drop only a Handle that still names the expected provider kind. */
int
vobjclosekind(int handle, int kind)
{
	Vhtab *t;
	Vhand *h;
	int i;
	ulong gen;

	if(handle <= 0)
		error(Ebadarg);
	i = (handle & 0xff) - 1;
	gen = (ulong)handle >> 8;
	if(i < 0 || i >= Vhandmax || gen == 0)
		error(Ebadarg);
	if((t = vgettab(0)) == nil)
		error(Ebadarg);
	lock(&t->lk);
	h = t->s[i].h;
	if(h == nil || t->s[i].gen != gen || h->obj->kind != kind){
		unlock(&t->lk);
		error(Ebadarg);
	}
	t->s[i].h = nil;
	unlock(&t->lk);
	vhandput(h);
	return 0;
}

static Vobj*
vpipealloc(Pipe *p, int end)
{
	Vpipe *vp;
	Vobj *o;

	vp = mallocz(sizeof *vp, 1);
	if(vp == nil)
		return nil;
	o = vobjnewkind(VkindPipe, 0);
	if(o == nil){
		free(vp);
		return nil;
	}
	vp->p = p;
	vp->end = end;
	o->provider = ObjOpenPipe;
	vobjsetpriv(o, vp);
	return o;
}

static int
vpipedefrights(void)
{
	return Vread|Vwrite|Vmeta|Vdelegate;
}

/* Create a pair and install only end 0.  The peer stays pinned by end 0 and
 * is released by ObjOpPipePeer, or closed with end 0 if never requested. */
int
vobjpipecreate(VobjId *id)
{
	Pipe *p;
	Vobj *o0, *o1;
	Vpipe *vp;
	int handle;

	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	if(vobjhandlesbykind(up, VkindPipe) + 2 > Vpipeendmax)
		error("Pipe: too many handles");
	p = pipecorepair();
	o0 = vpipealloc(p, 0);
	if(o0 == nil){
		pipecoreclose(p, 0);
		pipecoreclose(p, 1);
		error(Enomem);
	}
	o1 = vpipealloc(p, 1);
	if(o1 == nil){
		vobjput(o0);		/* closes end 0 */
		pipecoreclose(p, 1);
		error(Enomem);
	}
	vp = o0->priv;
	vp->peer = o1;
	if(waserror()){
		lock(&vp->lk);
		vp->peer = nil;
		unlock(&vp->lk);
		vobjput(o0);
		vobjput(o1);
		nexterror();
	}
	handle = vobjinstallobj(o0, vpipedefrights());
	poperror();
	if(id != nil){
		id->incarnation = o0->incarnation;
		id->space = o0->space;
		id->oid = o0->oid;
	}
	vobjput(o0);			/* creation reference */
	return handle;
}

int
vobjpipepeer(int handle)
{
	Vhand *h, *nh;
	Vhtab *t;
	Vpipe *vp;
	Vobj *peer;
	int r;

	h = vlookup(handle, Vdelegate);
	if(h->obj->kind != VkindPipe || (vp = h->obj->priv) == nil
	|| vp->end != 0){
		vhandput(h);
		error(Ebadarg);
	}
	if((t = vgettab(1)) == nil){
		vhandput(h);
		error(Enomem);
	}
	/*
	 * smalloc() may sleep in resrcwait(), so allocate the Vhand before
	 * taking vp->lk.  vinstall() only takes non-sleeping locks, which
	 * keeps the pending-peer check and the handle publication atomic
	 * without blocking while vp->lk is held.
	 */
	nh = smalloc(sizeof *nh);
	lock(&vp->lk);
	if(vp->peer == nil){
		unlock(&vp->lk);
		free(nh);
		vhandput(h);
		error("Pipe: peer already opened");
	}
	peer = vp->peer;
	vhandinit(nh, peer, nil, h->rights);
	r = vinstall(t, nh);
	if(r < 0){
		unlock(&vp->lk);
		vhandput(nh);
		vhandput(h);
		error("vobj: too many handles");
	}
	vp->peer = nil;
	unlock(&vp->lk);
	vobjput(peer);			/* drop the pending-peer reference */
	vhandput(h);
	return r;
}

/* Staged byte I/O keeps the kernel from holding a user pointer across a
 * blocking native queue operation.  The staging copy is wiped on all paths. */
static long
pipestream(Vpipe *vp, int op, void *buf, long n)
{
	char *kb;
	long r;

	if(n == 0)
		return 0;
	kb = smalloc(n);
	if(waserror()){
		memset(kb, 0, n);
		free(kb);
		nexterror();
	}
	if(op == VioRead){
		validaddr((uintptr)buf, n, 1);
		r = pipecoreread(vp->p, vp->end, kb, n);
		if(r < 0)
			error(up->errstr[0] != 0 ? up->errstr : Eio);
		if(r > n)
			r = n;
		memmove(buf, kb, r);
	}else{
		validaddr((uintptr)buf, n, 0);
		memmove(kb, buf, n);
		r = pipecorewrite(vp->p, vp->end, kb, n);
		if(r < 0)
			error(up->errstr[0] != 0 ? up->errstr : Eio);
	}
	memset(kb, 0, n);
	free(kb);
	poperror();
	return r;
}

void
vobjpipeoutputset(Proc *p, int handle)
{
	Vhand *h;

	h = vlookupin(p, handle, 0);
	if(h->obj->kind != VkindPipe || (h->rights & Vwrite) == 0){
		vhandput(h);
		error(Ebadarg);
	}
	p->stdoutpipe = handle;
	vhandput(h);
}

long
vobjpipeoutputwrite(int handle, void *data, long n)
{
	Vhand *h;
	Vpipe *vp;
	long r;

	h = vlookup(handle, 0);
	if(h->obj->kind != VkindPipe || (vp = h->obj->priv) == nil
	|| (h->rights & Vwrite) == 0){
		vhandput(h);
		error(Ebadarg);
	}
	if(n < 0 || n > OBJ_STREAM_MAXIO){
		vhandput(h);
		error(Ebadarg);
	}
	if(waserror()){
		vhandput(h);
		nexterror();
	}
	r = n == 0 ? 0 : pipecorewrite(vp->p, vp->end, data, n);
	if(r < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	poperror();
	vhandput(h);
	return r;
}

/* Trusted bootstrap install of one already-open native endpoint.  Ownership
 * transfers on success; on failure the caller still owns the Pipe. */
int
vobjpipekernelinstall(Proc *proc, void *pipe, int end)
{
	Vobj *o;
	Vpipe *vp;
	int h;

	if(proc != up || !obootstrap(proc) || (end != 0 && end != 1))
		error(Eperm);
	o = vpipealloc(pipe, end);
	if(o == nil)
		error(Enomem);
	if(waserror()){
		vp = o->priv;
		vp->p = nil;		/* caller keeps its endpoint ownership */
		vp->peer = nil;
		vobjput(o);
		nexterror();
	}
	h = vobjinstallobj(o, vpipedefrights());
	poperror();
	vobjput(o);
	return h;
}

static long
uartstream(Vuart *vu, int op, void *buf, long n)
{
	char *kb;
	long r;

	if(n == 0)
		return 0;
	kb = smalloc(n);
	if(waserror()){
		free(kb);
		nexterror();
	}
	if(op == VioRead){
		validaddr((uintptr)buf, n, 1);
		r = uartnread(vu->id, kb, n);
		if(r < 0)
			error(up->errstr[0] != 0 ? up->errstr : Eio);
		if(r > n)
			r = n;
		memmove(buf, kb, r);
	}else{
		validaddr((uintptr)buf, n, 0);
		memmove(kb, buf, n);
		r = uartnwrite(vu->id, kb, n);
		if(r < 0)
			error(up->errstr[0] != 0 ? up->errstr : Eio);
	}
	free(kb);
	poperror();
	return r;
}

static int
uartmodebits(int mode)
{
	switch(mode){
	case OREAD:
		return Vread|Vmeta|Vcontrol|Vdelegate;
	case OWRITE:
		return Vwrite|Vmeta|Vcontrol|Vdelegate;
	case ORDWR:
		return Vread|Vwrite|Vmeta|Vcontrol|Vdelegate;
	}
	return 0;
}

/* The catalog has already validated authority, selector, mode and rights.
 * Acquire the port only after those checks so a refused open has no effect. */
static int
vobjuartdevice(int port, int mode, uvlong rights, VobjId *id)
{
	Vobj *o;
	Vuart *vu;
	int h, hrights;

	uartnopen(port, mode);
	vu = smalloc(sizeof *vu);
	if(vu == nil){
		if(waserror())
			;
		else{
			uartnclose(port);
			poperror();
		}
		error(Enomem);
	}
	vu->id = port;
	vu->mode = mode;
	vu->opened = 1;
	o = vobjnewkind(VkindUart, 0);
	if(o == nil){
		free(vu);
		if(waserror())
			;
		else{
			uartnclose(port);
			poperror();
		}
		error(Enomem);
	}
	o->provider = ObjOpenDevice;
	vobjsetpriv(o, vu);
	hrights = (int)rights;
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	h = vobjinstallobj(o, hrights);
	poperror();
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);
	return h;
}

int
vobjuartctl(int handle, void *cfg)
{
	Vhand *h;
	Vuart *vu;
	Uartcfg c;

	if(cfg == nil || !okaddr((uintptr)cfg, sizeof c, 0))
		error(Ebadarg);
	memmove(&c, cfg, sizeof c);
	h = vlookup(handle, Vcontrol);
	if(h->obj->kind != VkindUart || (vu = h->obj->priv) == nil){
		vhandput(h);
		error(Ebadarg);
	}
	if(waserror()){
		vhandput(h);
		nexterror();
	}
	uartnctl(vu->id, &c);
	poperror();
	vhandput(h);
	return 0;
}

long
vobjuartstatus(int handle, void *buf, long n)
{
	Vhand *h;
	Vuart *vu;
	char *kb;
	long r;

	if(n < 0 || n > OBJ_UART_STATMAX)
		error(Ebadarg);
	h = vlookup(handle, Vmeta);
	if(h->obj->kind != VkindUart || (vu = h->obj->priv) == nil){
		vhandput(h);
		error(Ebadarg);
	}
	if(n == 0){
		vhandput(h);
		return 0;
	}
	kb = smalloc(n);
	if(waserror()){
		free(kb);
		vhandput(h);
		nexterror();
	}
	if(!okaddr((uintptr)buf, n, 1))
		error(Ebadarg);
	r = uartnstatus(vu->id, kb, n);
	if(r < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	if(r > n)
		r = n;
	memmove(buf, kb, r);
	free(kb);
	poperror();
	vhandput(h);
	return r;
}

/* The catalog has already validated the exclusive-claim option.  The rights
 * mask is the validated attenuation of Vread|Vmeta|Vcontrol|Vdelegate. */
static int
vobjkbddevice(uvlong rights, VobjId *id)
{
	Vobj *o;
	Vkbd *vk;
	Vhand *nh;
	Vhtab *t;
	int h, r;

	/*
	 * Allocate everything that can sleep before claiming the exclusive
	 * scan-code controller.  The claim is then held only across the
	 * non-sleeping vinstall(), and every failure path releases it once
	 * through vobjput() (vk->held stays 0 until the claim succeeds).
	 */
	vk = smalloc(sizeof *vk);
	vk->held = 0;
	o = vobjnewkind(VkindKbd, 0);
	if(o == nil){
		free(vk);
		error(Enomem);
	}
	o->provider = ObjOpenDevice;
	vobjsetpriv(o, vk);
	if((t = vgettab(1)) == nil){
		vobjput(o);
		error(Enomem);
	}
	nh = smalloc(sizeof *nh);
	if((r = kbdscancodeacquire()) != 0){
		free(nh);
		vobjput(o);		/* frees vk; held is still 0 */
		if(r == -2)
			error("PS/2 keyboard unavailable");
		error(Einuse);
	}
	vk->held = 1;
	vhandinit(nh, o, nil, (int)rights);
	h = vinstall(t, nh);
	if(h < 0){
		vhandput(nh);
		vobjput(o);		/* releases the scan-code claim */
		error("vobj: too many handles");
	}
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);			/* handle owns the remaining reference */
	return h;
}

static long
kbdstream(void *buf, long n)
{
	char *kb;
	long r;

	if(n == 0)
		return 0;
	kb = smalloc(n);
	if(waserror()){
		free(kb);
		nexterror();
	}
	if(!okaddr((uintptr)buf, n, 1))
		error(Ebadarg);
	r = kbdscancoderead(kb, n);
	if(r < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	if(r > n)
		r = n;
	memmove(buf, kb, r);
	free(kb);
	poperror();
	return r;
}

/* LED state is a shared hardware control, not scan-code authority.  A
 * nonzero KBD Handle carrying Vcontrol is required: there is no handle-less
 * global control and no implicit authority from a numeric selector.  The
 * initial grant is restricted by catalog opening; a delegated Vcontrol
 * Handle remains usable without a second identity check. */
int
vobjkbdleds(int handle, int leds)
{
	Vhand *h;

	if(handle == 0)
		error(Ebadarg);
	h = vlookup(handle, Vcontrol);
	if(h->obj->kind != VkindKbd){
		vhandput(h);
		error(Ebadarg);
	}
	vhandput(h);
	kbdsetleds(leds);
	return 0;
}

/* USB endpoint references are created only through the common catalog.
 * Endpoint rights come from the validated kind/mode option; the open rights
 * argument can attenuate but never amplify that set. */
static int
usbeprights(int kind, int mode)
{
	int r;

	r = Vmeta|Vdelegate;
	if(mode == OREAD)
		r |= Vread;
	else if(mode == OWRITE)
		r |= Vwrite;
	else
		r |= Vread|Vwrite;
	if(kind == USBCTL)
		r |= Vcontrol;
	return r;
}

/* DATA owns the usbcore data-open; CTL only references the endpoint for
 * provider administration, matching the former endpoint-open semantics. */
static int
vobjusbepref(int devno, int epno, int kind, int mode, uvlong rights, VobjId *id)
{
	Vobj *o;
	Vusbep *ve;
	Ep *ep;
	int h, opened;

	if(vobjhandlesbykind(up, VkindUsbEp) >= Vusbepslotmax)
		error("too many USB endpoint handles");
	ep = usbcore_getep(devno, epno);
	if(ep == nil)
		error(Ebadarg);
	/* The selector is not authority and the requested kind is not a
	 * description of the endpoint: validate it against the actual usbcore
	 * transfer type before any data-open or public Handle exists. */
	if(ep->dev->state == Ddetach){
		usbcore_putep(ep);
		error("device is detached");
	}
	if((kind == USBCTL && ep->ttype != Tctl)
	|| (kind == USBDATA && (ep->ttype == Tctl || ep->ttype == Tnone))){
		usbcore_putep(ep);
		error(Ebadarg);
	}
	ve = smalloc(sizeof *ve);
	if(ve == nil){
		usbcore_putep(ep);
		error(Enomem);
	}
	ve->ep = ep;
	ve->kind = kind;
	ve->mode = mode;
	opened = 0;
	if(waserror()){
		if(opened)
			usbcore_dataclose(ep);
		usbcore_putep(ep);
		free(ve);
		nexterror();
	}
	if(kind == USBDATA){
		usbcore_dataopen(ep, mode);
		opened = 1;
	}
	poperror();
	/* ve owns the referenced endpoint from here. */
	o = vobjnewkind(VkindUsbEp, 0);
	if(o == nil){
		if(opened)
			usbcore_dataclose(ep);
		usbcore_putep(ep);
		free(ve);
		error(Enomem);
	}
	o->provider = ObjOpenDevice;
	vobjsetpriv(o, ve);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	h = vobjinstallobj(o, (int)rights);
	poperror();
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);
	return h;
}

/* Common device catalog.
 *
 * Discovery selectors are boot-local hints and never authority.  Every open
 * validates the catalog Handle, the requested rights, the family options and
 * the selectors before acquiring a port, the keyboard controller, a NIC or a
 * USB endpoint.  Provider I/O semantics remain distinct.
 */
int
vobjdevicesopen(uvlong rights, VobjId *id)
{
	Vobj *o;
	int h, max;

	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	max = Vmeta|Vdelegate;
	if(iseve())
		max |= Vcreate|Vcontrol;
	if(rights == 0)
		rights = Vmeta|Vdelegate;
	if(rights & ~(uvlong)max)
		error(Eperm);
	if(vobjhandlesbykind(up, VkindDevices) >= Vusbslotmax)
		error("too many device catalog handles");
	usbcore_init();
	vetherlist(nil, 0); /* initialize the existing NIC registry once */
	o = vobjnewkind(VkindDevices, 0);
	if(o == nil)
		error(Enomem);
	o->provider = ObjOpenDevices;
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	h = vobjinstallobj(o, (int)rights);
	poperror();
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);
	return h;
}

static void
deventryput(void *out, int cap, int *total, ObjDeviceEntry *e)
{
	if(*total < cap)
		memmove((uchar*)out + (long)*total*sizeof *e, e, sizeof *e);
	(*total)++;
}

long
vobjdevicelist(int handle, void *out, long bytes)
{
	Vhand *h;
	Usbentry *entries;
	ObjDeviceEntry e;
	int i, n, total, cap;

	h = vlookup(handle, Vmeta);
	if(h->obj->kind != VkindDevices){
		vhandput(h);
		error(Ebadarg);
	}
	entries = mallocz(OBJ_DEVICE_LISTMAX*sizeof *entries, 1);
	if(entries == nil){
		vhandput(h);
		error(Enomem);
	}
	if(waserror()){
		free(entries);
		vhandput(h);
		nexterror();
	}
	if(bytes != 0)
		validaddr((uintptr)out, bytes, 1);
	cap = bytes / sizeof e;
	total = 0;
	n = usbcore_list(entries, OBJ_DEVICE_LISTMAX);
	if(n > OBJ_DEVICE_LISTMAX)
		n = OBJ_DEVICE_LISTMAX;
	for(i = 0; i < n; i++){
		if(entries[i].state == Ddetach)
			continue;
		if(entries[i].ttype == Tctl){
			memset(&e, 0, sizeof e);
			e.version = OBJ_DEVICE_ABI_VERSION;
			e.family = ObjDeviceUsbControl;
			e.selector0 = entries[i].devno;
			e.selector1 = entries[i].epno;
			e.maxsend = Rsetuplen+OBJ_DEVICE_MAXPAYLOAD;
			e.maxrecv = OBJ_DEVICE_MAXPAYLOAD;
			e.kind = Tctl;
			deventryput(out, cap, &total, &e);
		}
		memset(&e, 0, sizeof e);
		e.version = OBJ_DEVICE_ABI_VERSION;
		e.family = ObjDeviceUsbEndpoint;
		e.selector0 = entries[i].devno;
		e.selector1 = entries[i].epno;
		e.kind = entries[i].ttype;
		deventryput(out, cap, &total, &e);
	}
	{
		int left = total < cap ? cap-total : 0;
		ObjDeviceEntry *dst = total < cap ?
			(ObjDeviceEntry*)((uchar*)out + (long)total*sizeof e) : nil;

		total += vetherlist(dst, left);
	}
	n = uartnports();
	for(i = 0; i < n; i++){
		memset(&e, 0, sizeof e);
		e.version = OBJ_DEVICE_ABI_VERSION;
		e.family = ObjDeviceUart;
		e.selector0 = i;
		e.maxsend = OBJ_STREAM_MAXIO;
		e.maxrecv = OBJ_STREAM_MAXIO;
		deventryput(out, cap, &total, &e);
	}
	memset(&e, 0, sizeof e);
	e.version = OBJ_DEVICE_ABI_VERSION;
	e.family = ObjDeviceKbd;
	e.maxrecv = OBJ_KBD_MAXREAD;
	deventryput(out, cap, &total, &e);
	poperror();
	free(entries);
	vhandput(h);
	return bytes == 0 ? total : (total < cap ? total : cap);
}

static int
devfamilyrights(int family, int mode, int kind)
{
	switch(family){
	case ObjDeviceUart:
		return uartmodebits(mode);
	case ObjDeviceKbd:
		return Vread|Vmeta|Vcontrol|Vdelegate;
	case ObjDeviceUsbControl:
		return Vsend|Vrecv|Vmeta|Vcontrol|Vdelegate;
	case ObjDeviceUsbEndpoint:
		return usbeprights(kind, mode);
	case ObjDeviceEthernet:
		return Vsend|Vrecv|Vmeta|Vcontrol|Vdelegate|Vsubscribe;
	}
	return 0;
}

static Vobj*
vobjusbctrlopen(int devno, int epno)
{
	Vobj *o;
	Vdevice *d;
	Ep *ep;

	usbcore_init();
	ep = usbcore_getep(devno, epno);
	if(ep == nil)
		error(Ebadarg);
	if(ep->ttype != Tctl){
		usbcore_putep(ep);
		error(Ebadarg);
	}
	o = vobjnewkind(VkindDevice, 0);
	if(o == nil){
		usbcore_putep(ep);
		error(Enomem);
	}
	d = mallocz(sizeof *d, 1);
	if(d == nil){
		usbcore_putep(ep);
		vobjput(o);
		error(Enomem);
	}
	d->family = ObjDeviceUsbControl;
	d->ep = ep;
	o->priv = d;
	o->provider = ObjOpenDevice;
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	/* usbcore serializes the exclusive claim against old USB DATA handles. */
	usbcore_dataopen(ep, ORDWR);
	d->opened = 1;
	poperror();
	return o;
}

static Vobj*
vobjetheropen(int controller)
{
	Vobj *o;
	Vdevice *d;

	o = vobjnewkind(VkindDevice, 0);
	if(o == nil)
		error(Enomem);
	d = mallocz(sizeof *d, 1);
	if(d == nil){
		vobjput(o);
		error(Enomem);
	}
	d->family = ObjDeviceEthernet;
	o->priv = d;
	o->provider = ObjOpenDevice;
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	d->ether = vetheropen(controller);
	poperror();
	return o;
}

static int
vobjdeviceinstall(Vobj *o, uvlong rights, VobjId *id)
{
	int h;

	if(waserror()){
		vobjput(o);
		nexterror();
	}
	h = vobjinstallobj(o, (int)rights);
	poperror();
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);
	return h;
}

int
vobjdeviceopen(void *req, long len, uvlong rights, VobjId *id)
{
	ObjOpenDeviceReq r;
	ObjDeviceOpenOptions opt;
	Vhand *catalog;
	Vobj *o;
	uvlong max, cr;
	int mode, kind;

	if(req == nil || len != sizeof r)
		error(Ebadarg);
	validaddr((uintptr)req, sizeof r, 0);
	memmove(&r, req, sizeof r);
	if(r.version != OBJ_DEVICE_ABI_VERSION || r.flags != 0 || r.reserved != 0
	|| r.optlen > OBJ_DEVICE_OPTMAX)
		error(Ebadarg);
	memset(&opt, 0, sizeof opt);
	if(r.optlen != 0){
		if(r.optlen != sizeof opt)
			error(Ebadarg);
		memmove(&opt, r.options, sizeof opt);
		if(opt.version != OBJ_DEVICE_OPT_VERSION)
			error(Ebadarg);
	}
	mode = (int)opt.mode;
	kind = (int)opt.kind;
	switch(r.family){
	case ObjDeviceUsbControl:
		if(r.optlen != 0 || r.selector0 < 0 || r.selector1 < 0)
			error(Ebadarg);
		break;
	case ObjDeviceEthernet:
		if(r.optlen != 0 || r.selector0 < 0 || r.selector1 != 0)
			error(Ebadarg);
		break;
	case ObjDeviceUart:
		if(r.optlen != sizeof opt || opt.kind != 0 || opt.flags != 0
		|| r.selector1 != 0 || r.selector0 < 0
		|| (mode != OREAD && mode != OWRITE && mode != ORDWR))
			error(Ebadarg);
		if(r.selector0 >= uartnports())
			error(Ebadarg);
		break;
	case ObjDeviceKbd:
		if(r.optlen != sizeof opt || opt.mode != 0 || opt.kind != 0
		|| opt.flags != OBJ_DEVICE_KBD_EXCLUSIVE
		|| r.selector0 != 0 || r.selector1 != 0)
			error(Ebadarg);
		break;
	case ObjDeviceUsbEndpoint:
		if(r.optlen != sizeof opt || opt.flags != 0
		|| r.selector0 < 0 || r.selector1 < 0
		|| (kind != USBDATA && kind != USBCTL)
		|| (mode != OREAD && mode != OWRITE && mode != ORDWR))
			error(Ebadarg);
		break;
	default:
		error(Ebadarg);
	}
	max = devfamilyrights(r.family, mode, kind);
	if(rights == 0)
		rights = max;
	if((rights & ~max) != 0)
		error(Eperm);
	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	/* Authorization is complete before any resource is acquired. */
	catalog = vlookup(r.catalog, Vcreate);
	if(catalog->obj->kind != VkindDevices){
		vhandput(catalog);
		error(Ebadarg);
	}
	cr = (uvlong)catalog->rights;
	vhandput(catalog);
	if(r.family == ObjDeviceUsbEndpoint && kind == USBCTL
	&& (cr & Vcontrol) == 0)
		error(Eperm);
	switch(r.family){
	case ObjDeviceUart:
		if(vobjhandlesbykind(up, VkindUart) >= Vuartslotmax)
			error("too many UART handles");
		return vobjuartdevice(r.selector0, mode, rights, id);
	case ObjDeviceKbd:
		if(vobjhandlesbykind(up, VkindKbd) >= Vkbdslotmax)
			error("too many keyboard handles");
		return vobjkbddevice(rights, id);
	case ObjDeviceUsbEndpoint:
		return vobjusbepref(r.selector0, r.selector1, kind, mode,
			rights, id);
	}
	if(vobjhandlesbykind(up, VkindDevice) >= Vusbepslotmax)
		error("too many device handles");
	if(r.family == ObjDeviceUsbControl)
		o = vobjusbctrlopen(r.selector0, r.selector1);
	else
		o = vobjetheropen(r.selector0);
	return vobjdeviceinstall(o, rights, id);
}

/* Caller holds a Vhand for the entire operation, pinning the session and
 * its endpoint across blocking HCI operations and shared Handle close. */
static Vdevice*
vdevice(Vhand *h)
{
	if(h->obj->kind != VkindDevice || h->obj->priv == nil)
		error(Ebadarg);
	return h->obj->priv;
}

/* Conservative read-only allowlist.  Direction alone is not authority:
 * vendor/class IN requests can change state too.  Even native root-hub
 * reads can finish an earlier port reset, so every root request requires
 * Vcontrol.  Everything outside this allowlist needs Vcontrol as well. */
static int
vusbreadonly(Ep *ep, uchar *m)
{
    int request, value, count;

    request = m[Rreq];
    value = GET2(m+Rvalue);
    count = GET2(m+Rcount);
    if(ep->dev->depth < 0)
        return 0;
    if(m[Rtype] == (Rd2h|Rstd|Rdev) && request == Rgetdesc)
        return 1;
    if(value != 0)
        return 0;
    if(request == Rgetstatus && count == 2)
        return m[Rtype] == 0x80 || m[Rtype] == 0x81 || m[Rtype] == 0x82;
    if(request == 8 && count == 1) /* GET_CONFIGURATION */
        return m[Rtype] == 0x80 && GET2(m+Rindex) == 0;
    if(request == 10 && count == 1) /* GET_INTERFACE */
        return m[Rtype] == 0x81;
    return 0;
}

long
vobjdevicesend(int handle, void *in, long len, unsigned int *kind)
{
	Vhand *h;
	Vdevice *d;
	Vobj *reply;
	uchar *message, *trimmed;
	int n, incoming;
	long got;
	uvlong seq;

    if(kind != nil)
        *kind = 0;
    h = vlookup(handle, Vsend);
    if(waserror()){vhandput(h);nexterror();}
    d = vdevice(h);
    if(d->family == ObjDeviceEthernet){
        got = vethersend(d->ether, in, len);
        poperror();
        vhandput(h);
        if(kind != nil)
            *kind = ObjDeviceSendAcceptedBytes;
        return got;
    }
    poperror();
    vhandput(h);

	if(in == nil || len < Rsetuplen || len > Rsetuplen+OBJ_DEVICE_MAXPAYLOAD)
		error(Ebadarg);
	validaddr((uintptr)in, len, 0);
	/* Copy once: both validation and HCI use this immutable request. */
	message = malloc(len);
	if(message == nil)
		error(Enomem);
	if(waserror()){
		free(message);
		nexterror();
	}
	memmove(message, in, len);
	n = GET2(message+Rcount);
	incoming = (message[Rtype] & Rd2h) != 0;
	if(n > OBJ_DEVICE_MAXPAYLOAD || len != Rsetuplen + (incoming ? 0 : n))
		error(Ebadarg);
	h = vlookup(handle, Vsend);
	if(waserror()){
		vhandput(h);
		nexterror();
	}
	d = vdevice(h);
    if(!vusbreadonly(d->ep, message) && !(h->rights & Vcontrol))
        error(Eperm);
	qlock(&d->lk);
	if(waserror()){
		qunlock(&d->lk);
		nexterror();
	}
    if(d->failed)
        error("device: transfer failed; close and reopen");
	if(d->reply != nil)
		error(Einuse);
	if(d->sequence == 0x7fffffffffffffffULL)
		error("device sequence exhausted");
	/* Reserve all provider reply storage before touching hardware. */
	reply = vobjnewkind(VkindBytes, incoming ? n : 0);
	if(reply == nil)
		error(Enomem);
	if(waserror()){
        /* The provider may already have changed hardware or retained HCI
         * transaction state.  Do not retry through any session alias. */
        d->failed = 1;
		vobjput(reply);
		nexterror();
	}
	if(usbcore_write(d->ep, message, len) != len)
		error(Eio);
	got = 0;
	if(incoming && n != 0){
		got = usbcore_read(d->ep, reply->data, n);
		if(got < 0 || got > n)
			error(Eio);
	}
	/* Short USB responses are complete messages.  Trim both storage and
	 * accounting so no uninitialized tail becomes readable or uncharged. */
	if(reply->size > got){
        if(got == 0){
            free(reply->data);
            reply->data = nil;
        }else{
            trimmed = realloc(reply->data, got);
            if(trimmed == nil)
                error(Enomem);
            reply->data = trimmed;
        }
		lock(&vstats.lk);
		vstats.nbytes -= reply->size - got;
		unlock(&vstats.lk);
		reply->size = got;
	}
	d->reply = reply;
	seq = ++d->sequence;
	poperror();
	poperror();
	qunlock(&d->lk);
	poperror();
	vhandput(h);
	poperror();
	free(message);
	if(kind != nil)
		*kind = ObjDeviceSendSequence;
	return (long)seq;
}

/* Ethernet receive-channel acquisition: return the ObjectFIFO capability.
 * This is deliberately not the completed-message receive path. */
int
vobjdevicerx(int handle)
{
	Vhand *h;
	Vdevice *d;
	int rights, result;

	h = vlookup(handle, Vrecv);
	if(waserror()){
		vhandput(h);
		nexterror();
	}
	d = vdevice(h);
	if(d->family != ObjDeviceEthernet)
		error(Ebadarg);
	if(!(h->rights & Vsubscribe))
		error(Eperm);
	rights = Vsubscribe|Vmeta;
	rights |= h->rights & (Vdelegate|Vcontrol);
	result = vobjinstallobj(vetherfifo(d->ether), rights);
	poperror();
	vhandput(h);
	return result;
}

/* USB control completion retrieval only. */
int
vobjdevicerecv(int handle, void *out)
{
	Vhand *h;
	Vdevice *d;
	ObjDeviceResult r;
	int result;

	validaddr((uintptr)out, sizeof r, 1);
	h = vlookup(handle, Vrecv);
	if(waserror()){
		vhandput(h);
		nexterror();
	}
	d = vdevice(h);
	if(d->family == ObjDeviceEthernet)
		error(Ebadarg);

	qlock(&d->lk);
	if(waserror()){
		qunlock(&d->lk);
		nexterror();
	}
	if(d->reply == nil)
		error("device: no completion");
	r.sequence = d->sequence;
	r.family = ObjDeviceUsbControl;
	r.length = d->reply->size;
	/* Failure (e.g. Handle exhaustion) leaves the completion pending. */
	result = vobjinstallobj(d->reply, Vread|Vmeta|Vdelegate);
	memmove(out, &r, sizeof r);
	vobjput(d->reply);
	d->reply = nil;
	poperror();
	qunlock(&d->lk);
	poperror();
	vhandput(h);
	return result;
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

/* USB administration stays provider-specific but is reachable only through
 * a catalog Handle (global debug) or a catalog-opened CTL endpoint reference.
 * The caller pins the Vhand and checks Vcontrol.  A copied IN/OUT buffer
 * never escapes this synchronous call, and no data-transfer API is implied. */
static long
vusbglobalctl(uvlong operation, void *arg, long narg)
{
    char *buf;
    long r;
    int req;

    req = (int)operation;
    if(operation < USBdebug || operation > USBinfo || req != USBdebug)
        error(Ebadarg);
    if(narg < 0 || narg > 4096 || narg < usbioargmin(req)
    || (narg > 0 && arg == nil))
        error(Ebadarg);
    buf = nil;
    if(waserror()){free(buf);nexterror();}
    if(narg > 0){
        validaddr((uintptr)arg, narg, 0);
        validaddr((uintptr)arg, narg, 1);
        buf = smalloc(narg);
        memmove(buf, arg, narg);
    }
    r = usbcore_globalioctl(req, buf, narg);
    if(r < 0) error(up->errstr[0] != 0 ? up->errstr : Eio);
    if(narg > 0) memmove(arg, buf, narg);
    poperror();
    free(buf);
    return r;
}

static long
vusbepctl(Vusbep *ve, uvlong operation, void *arg, long narg)
{
    char *buf;
    long r;
    int req;

    if(ve == nil || ve->kind != USBCTL)
        error(Ebadarg);
    if(operation < USBdebug || operation > USBinfo)
        error(Ebadarg);
    req = (int)operation;
    if(narg < 0 || narg > 4096 || narg < usbioargmin(req)
    || (narg > 0 && arg == nil))
        error(Ebadarg);
    buf = nil;
    if(waserror()){free(buf);nexterror();}
    if(narg > 0){
        validaddr((uintptr)arg, narg, 0);
        validaddr((uintptr)arg, narg, 1);
        buf = smalloc(narg);
        memmove(buf, arg, narg);
    }
    r = usbcore_ioctl(ve->ep, req, buf, narg);
    if(r < 0) error(up->errstr[0] != 0 ? up->errstr : Eio);
    if(narg > 0) memmove(arg, buf, narg);
    poperror();
    free(buf);
    return r;
}

/* IOCTL(145): device configuration, control and device-specific status.
 * Device payload (UART bytes, Ethernet frames, USB transfers) stays in the
 * object interface.  Each operation checks its own required rights and its
 * own bounded request shape. */
long
vobjioctl(int handle, uvlong op, void *data, long len, uvlong arg0)
{
    Vhand *h;
    Vdevice *d;
    Vusbep *ve;
    long r;

    if(len < 0 || (len > 0 && data == nil))
        error(Ebadarg);
    switch(op){
    case IoctlUartConfig:
        if(len != sizeof(Uartcfg))
            error(Ebadarg);
        vobjuartctl(handle, data);
        return 0;
    case IoctlUartStatus:
        if(len <= 0 || len > OBJ_UART_STATMAX)
            error(Ebadarg);
        return vobjuartstatus(handle, data, len);
    case IoctlKbdLeds:
        if(data != nil || len != 0 || arg0 > 0x7fffffffULL)
            error(Ebadarg);
        return vobjkbdleds(handle, (int)arg0);
    case IoctlUsbGlobal:
        h = vlookup(handle, Vcontrol);
        if(waserror()){vhandput(h);nexterror();}
        if(h->obj->kind != VkindDevices)
            error(Ebadarg);
        r = vusbglobalctl(arg0, data, len);
        poperror();
        vhandput(h);
        return r;
    case IoctlUsbEndpoint:
        h = vlookup(handle, Vcontrol);
        if(waserror()){vhandput(h);nexterror();}
        if(h->obj->kind != VkindUsbEp)
            error(Ebadarg);
        ve = h->obj->priv;
        r = vusbepctl(ve, arg0, data, len);
        poperror();
        vhandput(h);
        return r;
    }

    h = vlookup(handle, 0);
    if(waserror()){vhandput(h);nexterror();}
    d = vdevice(h);
    if(d->family == ObjDeviceEthernet){
        if(op == IoctlEtherStatus){
            ObjEtherStatus status;
            if(!(h->rights & Vmeta)) error(Eperm);
            if(data == nil || len != sizeof status) error(Ebadarg);
            validaddr((uintptr)data, sizeof status, 1);
            vetherstatus(d->ether, &status);
            memmove(data, &status, sizeof status);
        }else if(op == IoctlEtherDetach){
            if(!(h->rights & Vcontrol)) error(Eperm);
            if(data != nil || len != 0) error(Ebadarg);
            vetherdetach(d->ether);
        }else
            error(Ebadctl);
    }else if(d->family == ObjDeviceUsbControl){
        if(op != IoctlUsbClearHalt) error(Ebadctl);
        if(!(h->rights & Vcontrol)) error(Eperm);
        if(data != nil || len != 0) error(Ebadarg);
        qlock(&d->lk);
        if(waserror()){qunlock(&d->lk);nexterror();}
        if(d->failed) error("device: transfer failed; close and reopen");
        if(d->reply != nil) error(Einuse);
        usbcore_ioctl(d->ep, USBclrhalt, nil, 0);
        poperror();
        qunlock(&d->lk);
    }else
        error(Ebadctl);
    poperror();
    vhandput(h);
    return 0;
}

/*
 * Native console provider.  It replaces the retired IOWRITE(62) entry point
 * with an ordinary process-local capability.  Authority comes from the same
 * Proc iomask (or an explicit stdout Pipe for Iohout) that sysiowrite used;
 * the provider adds no ambient output authority.  The Handle carries Vwrite
 * only, and its private state is the stream number in o->size.
 */
int
vobjconsoleopen(void *req, long len, uvlong rights, VobjId *id)
{
	ObjOpenConsoleReq cr;
	Vobj *o;
	int h, defrights;

	defrights = Vwrite;
	if(req == nil || len != sizeof cr)
		error(Ebadarg);
	validaddr((uintptr)req, sizeof cr, 0);
	memmove(&cr, req, sizeof cr);
	if(cr.reserved != 0)
		error(Ebadarg);
	if(cr.stream != Iohout && cr.stream != Iohdiag)
		error(Ebadarg);
	if(!iowriteok(cr.stream))
		error(Eperm);
	if(rights == 0)
		rights = defrights;
	if((rights & ~(uvlong)defrights) != 0)
		error(Eperm);
	if(vobjhandlesbykind(up, VkindConsole) >= Vconsoleslotmax)
		error("too many console handles");
	o = vobjnewkind(VkindConsole, (ulong)cr.stream);
	if(o == nil)
		error(Enomem);
	o->size = (ulong)cr.stream;
	o->provider = ObjOpenConsole;
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	h = vobjinstallobj(o, (int)rights);
	poperror();
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);
	return h;
}

int
vobjnamespaceopen(uvlong rights, VobjId *id)
{
	Vobj *o;
	int h;

	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	if(up->pgrp == nil)
		error(Eperm);
	if(rights == 0)
		rights = Vcontrol|Vmeta|Vdelegate;
	if((rights & Vcontrol) == 0 ||
	   (rights & ~(uvlong)(Vcontrol|Vmeta|Vdelegate)) != 0)
		error(Eperm);
	o = vobjnewkind(VkindNamespaceControl, 0);
	if(o == nil)
		error(Enomem);
	o->provider = ObjOpenNamespaceCurrent;
	o->priv = up->pgrp;
	incref(&up->pgrp->ref);
	lifecontrol(o->priv, 1);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	h = vobjinstallobj(o, (int)rights);
	poperror();
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);
	return h;
}

static void
vobjnamespacecheck(int handle)
{
	Vobj *o = vobjholdobj(handle, Vcontrol);
	int ok = o->kind == VkindNamespaceControl && o->priv == up->pgrp;
	vobjput(o);
	if(!ok)
		error(Eperm);
}

/* NamespaceControl is tied to the caller's current process group.  A copied
 * Handle cannot operate on a child or another namespace. */
int
vobjnamespacecreate(int control, char (*roles)[32], int nr)
{
	char names[Obindmax][Onamemax];
	Ohand *view = nil;
	uvlong epoch;
	int i, j, handle;

	vobjnamespacecheck(control);
	if(roles == nil || nr <= 0 || nr > Obindmax)
		error(Ebadarg);
	validaddr((uintptr)roles, nr*Onamemax, 0);
	memmove(names, roles, nr*Onamemax);
	view = smalloc(sizeof *view);
	view->ref.ref = 1;
	if(waserror()){
		ohandput(view);
		nexterror();
	}
	view->provider = octxnew();
	if(view->provider == nil)
		error(Enomem);
	epoch = octxepoch(up->pgrp->ocontext);
	for(i=0; i<nr; i++){
		if(memchr(names[i], 0, Onamemax) == nil || names[i][0] == 0)
			error(Ebadarg);
		for(j=0; names[i][j]; j++)
			if(!((names[i][j]>='a' && names[i][j]<='z') ||
			     (names[i][j]>='A' && names[i][j]<='Z') ||
			     (names[i][j]>='0' && names[i][j]<='9') ||
			      names[i][j]=='_' || names[i][j]=='-'))
				error(Ebadarg);
		for(j=0; j<i; j++)
			if(strcmp(names[i], names[j]) == 0)
				error(Ebadarg);
		oscheckrole(up->pgrp->ocontext, names[i], 1);
	}
	/* The view's authority is acquired under the same gate as binding and
	 * attachment, so RFNOMNT cannot interleave with creation. */
	if(!pgrpnamespaceextendbegin(up->pgrp)) error(Eperm);
	if(waserror()){
		pgrpnamespaceextendend(up->pgrp);
		nexterror();
	}
	for(i=0; i<nr; i++)
		octxcopybinding(view->provider, up->pgrp->ocontext, names[i]);
	if(epoch != octxepoch(up->pgrp->ocontext))
		error("O: namespace changed; retry");
	poperror();
	pgrpnamespaceextendend(up->pgrp);
	handle = vobjinstallohand(view, VkindNamespaceView);
	view = nil;
	poperror();
	return handle;
}

uvlong
vobjnamespaceattach(int control, int handle, int mode)
{
	Ohand *view, *grants[Obindmax];
	uvlong token;
	int n=0, i;

	vobjnamespacecheck(control);
	if(mode < 0 || mode > 2)
		error(Ebadarg);
	view = vobjholdohand(handle, VkindNamespaceView, Vcontrol);
	if(waserror()){
		for(i=0; i<n; i++) ohandput(grants[i]);
		ohandput(view);
		nexterror();
	}
	if(view->provider == nil)
		error(Ebadarg);
	n = octxsnapshot(view->provider, grants, 1);
	for(i=0; i<n; i++)
		osviewgrantvalid(grants[i]);
	token = voidget();
	/* Admission and composition are one step against RFNOMNT. */
	if(!pgrpnamespaceextendbegin(up->pgrp)) error(Eperm);
	if(waserror()){
		pgrpnamespaceextendend(up->pgrp);
		nexterror();
	}
	octxcompose(up->pgrp->ocontext, view->provider, mode, token);
	poperror();
	pgrpnamespaceextendend(up->pgrp);
	poperror();
	for(i=0; i<n; i++) ohandput(grants[i]);
	ohandput(view);
	return token;
}

void
vobjnamespacedetach(int control, uvlong token)
{
	vobjnamespacecheck(control);
	if(token == 0 || up->pgrp == nil)
		error(Ebadarg);
	octxunmount(up->pgrp->ocontext, token);
}

int
vobjnamespacevisible(int control, void *out, long bytes)
{
	Ohand *grants[Obindmax];
	NsVisible records[Obindmax];
	NsIdentity id;
	int allowed[Obindmax], n, i, j, count=0;
	uvlong epoch;

	vobjnamespacecheck(control);
	if(bytes < 0 || bytes > sizeof records || bytes % sizeof records[0] ||
	   (out == nil && bytes != 0))
		error(Ebadarg);
	epoch = octxepoch(up->pgrp->ocontext);
	n = octxsnapshot(up->pgrp->ocontext, grants, 0);
	if(waserror()){
		for(i=0; i<n; i++) ohandput(grants[i]);
		nexterror();
	}
	if(!osvisibility(grants, n, allowed) ||
	   epoch != octxepoch(up->pgrp->ocontext))
		error("O: namespace changed; retry");
	for(i=0; i<n; i++) if(allowed[i]){
		osvolumeid(grants[i], id.volume);
		id.space = grants[i]->sid;
		id.oid = grants[i]->oid;
		for(j=0; j<count; j++)
			if(memcmp(&records[j].object, &id, sizeof id) == 0)
				break;
		if(j == count){
			records[count].object = id;
			records[count].rights = 0;
			records[count].reserved = 0;
			count++;
		}
		records[j].rights |= grants[i]->rights;
	}
	if(count*sizeof records[0] > bytes)
		error(Eshort);
	if(count){
		validaddr((uintptr)out, count*sizeof records[0], 1);
		memmove(out, records, count*sizeof records[0]);
	}
	poperror();
	for(i=0; i<n; i++) ohandput(grants[i]);
	return count;
}

/*
 * Process-lifecycle OBJINVOKE providers.  Each validates the same
 * process-local capability that the retired direct number would have
 * trusted and then calls the one shared kernel core.  No raw syscall
 * dispatch is re-entered and no second scheduler/exit path exists.
 */
long
vobjprocwait(ProcWait *out)
{
	Waitmsg w;
	ulong pid;
	ulong n;

	if(out == nil)
		error(Ebadarg);
	pid = pwait(&w);
	memset(out, 0, sizeof *out);
	out->pid = (long long)pid;
	out->time[0] = w.time[0];
	out->time[1] = w.time[1];
	out->time[2] = w.time[2];
	n = strlen(w.msg);
	if(n > PROCWAIT_MSG_MAX)
		n = PROCWAIT_MSG_MAX;
	memmove(out->msg, w.msg, n);
	out->msglen = (unsigned int)n;
	return 0;
}

static long
vobjiover(int handle, uvlong op, void *buf, long n, long timeout, uvlong *ver)
{
	long r;
	Vobj *o;
	char *kb;

	if(ver != nil)
		*ver = 0;
	if(op == VioRead)
		o = vobjholdobj(handle, Vread);
	else if(op == VioWrite)
		o = vobjholdobj(handle, Vwrite);
	else
		error(Ebadarg);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	switch(o->kind){
	case VkindPipe:
		if(n < 0 || n > OBJ_STREAM_MAXIO)
			error(Ebadarg);
		r = pipestream(o->priv, (int)op, buf, n);
		break;
	case VkindUart:
		if(n < 0 || n > OBJ_STREAM_MAXIO)
			error(Ebadarg);
		{
			Vuart *vu = o->priv;

			if(vu == nil)
				error(Ebadarg);
			if(op == VioRead && vu->mode == OWRITE)
				error(Ebadusefd);
			if(op == VioWrite && vu->mode == OREAD)
				error(Ebadusefd);
			r = uartstream(vu, (int)op, buf, n);
		}
		break;
	case VkindKbd:
		if(op != VioRead)
			error(Ebadarg);
		if(n < 0 || n > OBJ_KBD_MAXREAD)
			error(Ebadarg);
		r = kbdstream(buf, n);
		break;
	case VkindBytes:
		if(op != VioRead)
			error("vobj: byte object is immutable");
		if(n < 0 || n > Vobjpaymax)
			error(Ebadarg);
		if(n == 0){
			r = o->size;
			break;
		}
		if(buf == nil)
			error(Ebadarg);
		validaddr((uintptr)buf, n, 1);
		if(n > o->size)
			n = o->size;
		memmove(buf, o->data, n);
		r = n;
		break;
	case VkindGe:
		if(n < 0 || n > Vobjpaymax)
			error(Ebadarg);
		if(n > 0)
			validaddr((uintptr)buf, n, op == VioRead ? 1 : 0);
		if(op == VioRead){
			r = osreadhand(o->priv, o->oid, buf, n, ver);
		}else{
			oswritehand(o->priv, o->oid, buf, n);
			r = n;
		}
		break;
	case VkindConsole:
		if(op != VioWrite)
			error(Ebadarg);
		if(n < 0 || n > OBJ_STREAM_MAXIO)
			error(Ebadarg);
		if(n == 0){
			r = iowritek((int)o->size, nil, 0);
			break;
		}
		if(buf == nil)
			error(Ebadarg);
		validaddr((uintptr)buf, n, 0);
		kb = smalloc(n);
		if(waserror()){
			free(kb);
			nexterror();
		}
		memmove(kb, buf, n);
		r = iowritek((int)o->size, kb, n);
		free(kb);
		poperror();
		break;
	default:
		error("vobj: object does not support I/O");
	}
	poperror();
	vobjput(o);
	return r;
}

long
vobjio(int handle, uvlong op, void *buf, long n, long timeout)
{
	return vobjiover(handle, op, buf, n, timeout, nil);
}

/* READ of a persistent GE object also publishes the version it read.
 * Other providers leave *VER at zero, preserving their existing result. */
long
vobjreadver(int handle, void *buf, long n, long timeout, uvlong *ver)
{
	return vobjiover(handle, VioRead, buf, n, timeout, ver);
}

int
vobjlist(void *buf, long n)
{
	long count, i;
	Vhtab *t;
	VobjListRec rec[Vhandmax];

	if(n < 0 || n > Vhandmax || (n > 0 && buf == nil))
		error(Ebadarg);
	if(n > 0)
		validaddr((uintptr)buf, n * sizeof(VobjListRec), 1);
	count = 0;
	if((t = vgettab(0)) != nil){
		lock(&t->lk);
		for(i = 0; i < Vhandmax; i++){
			Vhand *h = t->s[i].h;
			if(h == nil)
				continue;
			if(count < Vhandmax){
				memset(&rec[count], 0, sizeof rec[count]);
				rec[count].handle = (unsigned int)((t->s[i].gen << 8) | (i + 1));
				rec[count].kind = (unsigned int)h->obj->kind;
				rec[count].rights = (unsigned int)h->rights;
				rec[count].incarnation = h->obj->incarnation;
				rec[count].space = h->obj->space;
				rec[count].oid = h->obj->oid;
			}
			count++;
		}
		unlock(&t->lk);
	}
	if(n > 0){
		long k = count < n ? count : n;
		memmove(buf, rec, k * sizeof rec[0]);
	}
	return count;
}

int
vobjopenstream(uvlong provider, void *req, long len, uvlong rights, VobjId *id)
{
	Vobj *o;
	VobjGeReq gr;
	Ohand *oh;
	int handle, defrights, ceiling;

	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	if(rights & ~(uvlong)Vall)
		error(Ebadarg);
	o = nil;
	oh = nil;
	if(provider == VopenSpace){
		Ohand *cap;
		if(req == nil || len <= 0 || len >= 256)
			error(Ebadarg);
		cap = osopenspace((char*)req, (int)len);
		oh = cap;
 /* Bootstrap authority remains bound to the original volume owner. */
		defrights = vgerights(oh);
		if(rights == 0)
			rights = defrights;
		if((rights & ~(uvlong)defrights) != 0){
			oshandput(oh);
			error(Eperm);
		}
		o = vobjalloc(VkindSpace, 0, 0);
		if(o == nil){
			oshandput(oh);
			error(Enomem);
		}
		vgecaplimit(oh,(int)rights);
		o->provider = VopenSpace;
		o->space = ossid(oh);
		o->oid = 0;
		vobjsetpriv(o, oh);
	}else if(provider == VopenGeSpace){
		Vhand *sh;
		Vobj *sp;
		Ohand *cap;
		if(req == nil || len != sizeof gr)
			error(Ebadarg);
		validaddr((uintptr)req, sizeof gr, 0);
		memmove(&gr, req, sizeof gr);
		if(gr.rights != 0)
			error(Ebadarg);
		sh = vlookup(gr.ospace, Vmeta);
		sp = sh->obj;
		if(sp->kind != VkindSpace || sp->priv == nil){
			vhandput(sh);
			error(Ebadarg);
		}
		if(waserror()){
			vhandput(sh);
			nexterror();
		}
		ceiling = sh->rights;
		cap = oscopycap((Ohand*)sp->priv, gr.oid);
		poperror();
		vhandput(sh);
		oh = cap;
		defrights = vgerights(oh) & ceiling & ~Vcreate;
		if(rights == 0)
			rights = defrights;
		if((rights & ~(uvlong)defrights) != 0){
			oshandput(oh);
			error(Eperm);
		}
		o = vobjnewkind(VkindGe, 0);
		if(o == nil){
			oshandput(oh);
			error(Enomem);
		}
		vgecaplimit(oh,(int)rights);
		o->provider = VopenGe;
		o->oid = gr.oid;
		o->space = ossid(oh);
		vobjsetpriv(o, oh);
	}else
		error("vobj: provider not supported");
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	handle = vobjinstallobj(o, (int)rights);
	poperror();
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	vobjput(o);
	return handle;
}

/*
 * OPEN(Handle): duplicate an existing authorized capability with an equal or
 * attenuated rights subset.  The numeric source Handle is process-local, so
 * this never grants authority from an identity or an OID alone.
 */
int
vobjopenhandle(int src, uvlong rights, VobjId *id)
{
	Vhand *s, *nh;
	Vhtab *t;
	int handle;

	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	s = vlookup(src, Vdelegate);
	/* A subscription has at most one receiver.  Duplicating its Handle
	 * would create a second receiver on the same queue; callers must take
	 * an independent subscription instead. */
	if(s->sub != nil){
		vhandput(s);
		error("vobj: subscription handles cannot be duplicated");
	}
	if(rights == 0)
		rights = (uvlong)s->rights;
	if((rights & ~(uvlong)s->rights) != 0){
		vhandput(s);
		error(Eperm);
	}
	if((t = vgettab(1)) == nil){
		vhandput(s);
		error(Enomem);
	}
	nh = vhandalloc(s->obj, s->sub, (int)rights);
	handle = vinstall(t, nh);
	if(handle < 0){
		vhandput(nh);
		vhandput(s);
		error("vobj: too many handles");
	}
	if(id != nil){
		id->incarnation = s->obj->incarnation;
		id->space = s->obj->space;
		id->oid = s->obj->oid;
	}
	vhandput(s);
	return handle;
}

/* A role is a bounded ASCII label, never a path or a directory component. */
static void
vrolename(char *dst, char *src, int n)
{
	int i;

	if(src == nil || n <= 0 || n >= Vnamemax)
		error(Ebadarg);
	validaddr((uintptr)src, n, 0);
	memmove(dst, src, n);
	dst[n] = 0;
	for(i = 0; i < n; i++)
		if(!((dst[i] >= 'a' && dst[i] <= 'z')
		|| (dst[i] >= 'A' && dst[i] <= 'Z')
		|| (dst[i] >= '0' && dst[i] <= '9')
		|| dst[i] == '_' || dst[i] == '-'))
			error(Ebadarg);
}

Ohand *vobjbindingcap(void *v) {
 Vhand *h=v;
 if(h==nil || h->obj==nil ||
    (h->obj->kind!=VkindGe && h->obj->kind!=VkindSpace)) return nil;
 return h->obj->priv;
}
Ohand *vobjbindinggecap(void *v) {
 Vhand *h=v;
 return h!=nil && h->obj!=nil && h->obj->kind==VkindGe ? h->obj->priv : nil;
}
int vobjbindingdelegable(void *v) {
 Vhand *h=v;
 return h!=nil && (h->rights & Vdelegate)!=0;
}
void vobjbindinghold(void *v) {if(v) incref(&((Vhand*)v)->ref);}
void vobjbindingput(void *v) {vhandput(v);}

int
vobjbind(int handle, char *role, int rolelen, uvlong rights, uint mode)
{
 Vhand *h=nil,*nh=nil;
 Vobj *bound=nil;
 Ohand *cap=nil;
 char name[Vnamemax];
 int m=mode & ObjBindModeMask;
 if(mode & ~(ObjBindModeMask|ObjBindRemove)) error(Ebadarg);
 vrolename(name,role,rolelen);
 if(up->pgrp==nil) error(Eperm);
 if(mode & ObjBindRemove){
  if(mode!=ObjBindRemove || handle!=0 || rights!=0) error(Ebadarg);
  octxunbind(up->pgrp->ocontext,name);
  return 0;
 }
 if(m>ObjBindShare) error(Ebadarg);
 h=vlookup(handle,Vdelegate);
 if(waserror()){oshandput(cap);vobjput(bound);vhandput(nh);vhandput(h);nexterror();}
 if(h->sub) error(Eperm);
 if(h->obj->kind==VkindNamespaceControl) error(Eperm);
 if(rights==0) rights=h->rights;
 if(rights & ~(uvlong)h->rights) error(Eperm);
 if(h->obj->kind==VkindGe){
  cap=oscopycap(h->obj->priv,h->obj->oid);
  vgecaplimit(cap,(int)rights);
  bound=vobjnewkind(VkindGe,0);
  if(bound==nil) error(Enomem);
  bound->provider=VopenGe;
  bound->oid=h->obj->oid;
  bound->space=ossid(cap);
  bound->priv=cap;cap=nil;
  nh=vhandalloc(bound,nil,(int)rights);
  vobjput(bound);bound=nil;
 }else
  nh=vhandalloc(h->obj,nil,(int)rights);
 /* RFNOMNT forbids namespace extension, not removal of an existing role.
  * Admission and the bind are one step against pgrpsealnamespace(). */
 if(!pgrpnamespaceextendbegin(up->pgrp)) error(Eperm);
 if(waserror()){pgrpnamespaceextendend(up->pgrp);nexterror();}
 octxbindobject(up->pgrp->ocontext,name,nh,m);
 poperror();
 pgrpnamespaceextendend(up->pgrp);
 poperror();vhandput(nh);vhandput(h);return 0;
}

int
vobjopenbinding(char *role, int rolelen, uvlong rights, VobjId *id)
{
 Vhand *bh,*nh;int handle;char name[Vnamemax];
 if(id) validaddr((uintptr)id,sizeof *id,1);
 vrolename(name,role,rolelen);
 if(up->pgrp==nil) error(Eperm);
 bh=octxobject(up->pgrp->ocontext,name);
 if(waserror()){vhandput(bh);nexterror();}
 if(bh->obj->kind==VkindGe || bh->obj->kind==VkindSpace) oscheckhand(bh->obj->priv);
 if(rights==0) rights=bh->rights;
 if(rights & ~(uvlong)bh->rights) error(Eperm);
 nh=vhandalloc(bh->obj,nil,(int)rights);
 if(vgettab(1)==nil){vhandput(nh);error(Enomem);}
 handle=vinstall(vgettab(1),nh);
 if(handle<0){vhandput(nh);error("vobj: too many handles");}
 if(id){id->incarnation=bh->obj->incarnation;id->space=bh->obj->space;id->oid=bh->obj->oid;}
 poperror();vhandput(bh);return handle;
}
