/*
 * Volatile objects, capability regions and native device capabilities.
 *
 * A volatile object has RAM-only state and a scope-qualified identity
 * (incarnation, space, oid).  Creating one never reads or writes GE/O.  The
 * identity is allocated monotonically for this boot; the incarnation changes
 * on every boot, so a stale identity can never resolve to a new object.
 *
 * Device receive channels and console sessions use capability-mapped regions
 * with release/acquire publication and REGIONWAIT/REGIONWAKE notification.
 * The kernel does not queue or parse device payloads in a generic message
 * FIFO.  Byte-stream UART/KBD providers retain bounded staged I/O.
 *
 * Handles are process-local, generation-tagged and rights-bearing.  They are
 * not OIDs, pointers or ambient authority.  Process exec/exit releases all
 * handles.
 */#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
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
	Venvmax		= 64,
	Vregionmax	= 16*1024*1024,
	Vgenmax		= 0x7fffff,
	Vnamemax	= 32,
	/* Native-family compatibility limits, now enforced over the one
	 * process-local Vhand table instead of separate tables. */
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
_Static_assert(ObjProcCreated == PstatCreated &&
	ObjProcRunning == PstatRunning && ObjProcCompleted == PstatCompleted,
	"process state ABI");
_Static_assert(ObjProcCauseExit == PcauseExit &&
	ObjProcCauseTerminated == PcauseTerminated &&
	ObjProcCauseFault == PcauseFault, "process cause ABI");

typedef struct Vobj	Vobj;
typedef struct Vhand	Vhand;
typedef struct Vhslot	Vhslot;
typedef struct Vhtab	Vhtab;
typedef struct Vuart	Vuart;
typedef struct Vkbd	Vkbd;
typedef struct Vusbep	Vusbep;

struct Vhand {
	Ref	ref;
	Vobj	*obj;
	int	rights;
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

typedef struct Wwait Wwait;
typedef struct Vregion Vregion;
struct Wwait {
	Rendez	rz;		/* sleep point; the waiter owns this record */
	Vregion	*r;
	uvlong	offset;		/* region-relative publication word */
	int	woken;
	int	writer;		/* woken by writer-slot release */
	int	onlist;
	Wwait	*next;
	Wwait	*prev;
};
struct Vregion {
	/*
	 * Lock order is lk -> waitlk.  lk may be held while waitlk is taken
	 * (mapping admission); waitlk is never held while taking lk, and no
	 * code sleeps while holding either.  writeractive, writergen and the
	 * wait queue share waitlk so the writer-presence check, word compare
	 * and waiter enrollment are one linearizable step with writer-slot
	 * acquisition and release.
	 */
	QLock	lk;		/* protects revoked and mapping admission */
	Lock	waitlk;		/* protects writeractive, writergen and waiters */
	Page	**pages;	/* one reference per backing page */
	ulong	npages;
	int	writeractive;	/* a writable mapping is installed */
	unsigned writergen;	/* increments on every writer acquire/release */
	int	revoked;	/* no further mappings through any Handle */
	Wwait	*waiters;	/* queue of blocked publication waiters */
	Wwait	*waittail;
};

typedef struct Vdevice Vdevice;
struct Vdevice {
    int family;
    Veth *ether;
    QLock lk;              /* serialize one complete request/completion */
    Ep *ep;
    int opened;
    int failed;            /* hardware outcome uncertain: close/reopen */
    Vobj *reply;           /* one immutable completion */
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
vreserve(ulong bytes)
{
	lock(&vstats.lk);
	if(vstats.nobj >= Vobjmax || vstats.nbytes + bytes > Vobjbytesmax){
		unlock(&vstats.lk);
		return -1;
	}
	vstats.nobj++;
	vstats.nbytes += bytes;
	unlock(&vstats.lk);
	return 0;
}

static void
vrelease(ulong bytes)
{
	lock(&vstats.lk);
	vstats.nobj--;
	vstats.nbytes -= bytes;
	unlock(&vstats.lk);
}

static Vobj*
vobjalloc(int kind, uvlong space, ulong size)
{
	Vobj *o;

	if(vreserve(size) != 0)
		return nil;
	o = mallocz(sizeof *o, 1);
	if(o == nil){
		vrelease(size);
		return nil;
	}
	if(size != 0 && kind != VkindRegion){
		o->data = mallocz(size, 0);
		if(o->data == nil){
			free(o);
			vrelease(size);
			return nil;
		}
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
	if(o->kind == VkindUart && o->priv != nil){	}else if(o->kind == VkindUart && o->priv != nil){
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
	}else if(o->kind == VkindRegion && o->priv != nil){
		Vregion *r = o->priv;
		ulong i;

		o->priv = nil;
		for(i = 0; i < r->npages; i++)
			if(r->pages[i] != nil)
				putpage(r->pages[i]);
		free(r->pages);
		free(r);
	}else if(o->kind == VkindProcess && o->priv != nil){
		Procstat *st = o->priv;

		o->priv = nil;
		procstatput(st);
	}else if(o->kind == VkindNamespaceControl && o->priv != nil){
		lifecontrol(o->priv, -1);
		closepgrp(o->priv);
	}else if(o->kind == VkindGe || o->kind == VkindSpace || o->kind == VkindNamespaceView){
		if(o->priv != nil)
			oshandput(o->priv);
	}
	{
		ulong size = o->size;
		free(o->data);
		free(o);
		vrelease(size);
	}
}

static void
vhandinit(Vhand *h, Vobj *o, int rights)
{
	h->ref.ref = 1;
	h->obj = o;
	incref(&o->ref);
	h->rights = rights;
}

static Vhand*
vhandalloc(Vobj *o, int rights)
{
	Vhand *h;

	h = smalloc(sizeof *h);
	vhandinit(h, o, rights);
	return h;
}

static void
vhandput(Vhand *h)
{
	if(h == nil)
		return;
	if(decref(&h->ref) != 0)
		return;
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
	h = vhandalloc(o, rights);
	handle = vinstall(t, h);
	if(handle < 0){
		vhandput(h);
		error("vobj: too many handles");
	}
	return handle;
}

/*
 * Install one initial VkindProcess Handle in the caller.  The completion
 * record is reference-counted independently of the Proc, so the Handle is
 * valid before the process is published and remains valid after it exits.
 */
int
vobjprocinstall(Procstat *st)
{
	Vobj *o;
	int h;

	if(st == nil)
		error(Ebadarg);
	o = vobjalloc(VkindProcess, 0, 0);
	if(o == nil)
		error(Enomem);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	o->priv = st;
	procstatref(st);
	h = vobjinstallobj(o, Vobserve|Vcontrol|Vmeta|Vdelegate);
	poperror();
	vobjput(o);
	return h;
}

int
vobjprocinfo(int handle, ObjProcessInfo *out)
{
	Vobj *o;

	if(out == nil)
		error(Ebadarg);
	validaddr((uintptr)out, sizeof *out, 1);
	o = vobjholdobj(handle, Vmeta);
	if(o->kind != VkindProcess){
		vobjput(o);
		error(Ebadarg);
	}
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	procstatinfo(o->priv, out);
	poperror();
	vobjput(o);
	return 0;
}

long
vobjprocesswait(int handle, ObjProcessInfo *out, long timeout)
{
	Vobj *o;
	long r;

	if(out == nil)
		error(Ebadarg);
	validaddr((uintptr)out, sizeof *out, 1);
	o = vobjholdobj(handle, Vobserve);
	if(o->kind != VkindProcess){
		vobjput(o);
		error(Ebadarg);
	}
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	r = procstatwait(o->priv, out, timeout);
	poperror();
	vobjput(o);
	return r;
}

int
vobjprocterminate(int handle)
{
	Vobj *o;
	int r;

	o = vobjholdobj(handle, Vcontrol);
	if(o->kind != VkindProcess){
		vobjput(o);
		error(Ebadarg);
	}
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	r = procstatterminate(o->priv);
	poperror();
	vobjput(o);
	return r;
}

/*
 * Volatile capability regions.
 *
 * A region owns its Page backing and one writer slot.  Each mapping is a
 * distinct SG_REGION Segment in one address space, holding a region reference
 * and its own PTE set; writer mappings lack SG_RONLY and readers carry it, so
 * the MMU enforces the split even though the Page objects are shared.  A
 * mapping is independent of its Handle and survives Handle close; process
 * exit or obj_region_unmap() releases it.  The writer slot is never
 * transferred automatically; while any writable mapping exists another
 * writer request is rejected.
 */
Vobj*
vobjregionalloc(uvlong size)
{
	Vobj *o;
	Vregion *r;
	Page *p;
	ulong npages, i;

	if(size == 0 || size > Vregionmax || (size % BY2PG) != 0)
		error(Ebadarg);
	npages = (ulong)(size / BY2PG);
	o = vobjnewkind(VkindRegion, (ulong)size);
	if(o == nil)
		error(Enomem);
	r = nil;
	if(waserror()){
		if(r != nil){
			if(r->pages != nil)
				for(i = 0; i < npages; i++)
					if(r->pages[i] != nil)
						putpage(r->pages[i]);
			free(r->pages);
			free(r);
			o->priv = nil;
		}
		vobjput(o);
		nexterror();
	}
	r = mallocz(sizeof *r, 1);
	if(r == nil)
		error(Enomem);
	r->npages = npages;
	r->pages = mallocz(npages*sizeof(Page*), 1);
	if(r->pages == nil)
		error(Enomem);
	for(i = 0; i < npages; i++){
		p = newpage(0, nil);
		if(p == nil)
			error(Enomem);
		r->pages[i] = fillpage(p, 0);
	}
	o->priv = r;
	o->flags = 0;			/* a region payload is mutable */
	o->provider = VkindRegion;
	poperror();
	return o;
}

int
vobjregioncreate(uvlong size, VobjId *id)
{
	Vobj *o;
	Vhtab *t;
	Vhand *h;
	int handle;

	if((t = vgettab(1)) == nil)
		error(Enomem);
	o = vobjregionalloc(size);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	h = vhandalloc(o, Vread|Vwrite|Vmeta|Vdelegate|Vcontrol);
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
	vobjput(o);			/* the installed Handle owns the object */
	return handle;
}

/*
 * Clear the writer-active state and notify every registered waiter as one
 * linearizable step.  This is the writer-slot release path: a waiter either
 * enrolled before this critical section (and is marked writer/3 here) or
 * checks writeractive after it (and returns 3 immediately).  A replacement
 * writer acquiring the slot later cannot erase the notification because the
 * woken/writer flags are already set.  Wakeup never sleeps, so holding the
 * queue spinlock cannot deadlock the sleeper: the sleeper releases the queue
 * before it sleeps.
 */
static void
regionwriterdown(Vregion *r)
{
	Wwait *w;

	lock(&r->waitlk);
	r->writeractive = 0;
	r->writergen++;
	for(w = r->waiters; w != nil; w = w->next){
		if(w->woken)
			continue;
		w->woken = 1;
		w->writer = 1;
		coherence();
		wakeup(&w->rz);
	}
	unlock(&r->waitlk);
}

/* Called from putseg() when the last reference to a region mapping goes. */
void
regionsegput(Segment *s)
{
	Vobj *o;
	Vregion *r;

	if(s == nil || (o = s->region) == nil)
		return;
	s->region = nil;
	r = o->priv;
	if(r != nil && s->regionwriter){
		s->regionwriter = 0;
		/* Writer death must not strand observers.  This is a lifecycle
		 * notification, not a publication: callers still recheck. */
		regionwriterdown(r);
	}else
		s->regionwriter = 0;
	vobjput(o);
}

static void
regionfillseg(Segment *s, Vregion *r)
{
	Pte **pte, *etp;
	Page **pg, *p;
	uintptr off;
	ulong i;

	qlock(&s->qlk);
	if(waserror()){
		qunlock(&s->qlk);
		nexterror();
	}
	for(i = 0; i < r->npages; i++){
		off = (uintptr)i*BY2PG;
		pte = &s->map[off/PTEMAPMEM];
		if((etp = *pte) == nil){
			etp = ptealloc();
			if(etp == nil)
				error(Enomem);
			*pte = etp;
		}
		pg = &etp->pages[(off&(PTEMAPMEM-1))/BY2PG];
		p = r->pages[i];
		incref(&p->ref);
		*pg = p;
		s->used++;
		if(pg < etp->first)
			etp->first = pg;
		if(pg > etp->last)
			etp->last = pg;
	}
	qunlock(&s->qlk);
	poperror();
}

uintptr
vobjregionmap(int handle, uintptr va)
{
	Vhand *h;
	Vobj *o;
	Vregion *r;
	Segment *s, *os;
	ulong npages, len;
	int sno, write, charged, writerheld;

	h = vlookup(handle, Vread);
	o = h->obj;
	if(o->kind != VkindRegion || o->priv == nil){
		vhandput(h);
		error(Ebadarg);
	}
	r = o->priv;
	npages = r->npages;
	len = npages*BY2PG;
	write = (h->rights & Vwrite) != 0;
	if(va != 0 && (va >= USTKTOP || (va & (BY2PG-1)) != 0)){
		vhandput(h);
		error(Ebadarg);
	}
	charged = 0;
	writerheld = 0;
	s = nil;
	qlock(&up->seglock);
	if(waserror()){
		if(s != nil)
			putseg(s);	/* also releases the writer slot */
		else if(writerheld)
			regionwriterdown(r);
		if(charged)
			procmemdecommit(up, npages);
		qunlock(&up->seglock);
		vhandput(h);
		nexterror();
	}
	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;
	if(sno == NSEG)
		error(Enovmem);
	if(va == 0){
		for(os = up->seg[SSEG]; os != nil; os = isoverlap(va, len)){
			va = os->base;
			if(len >= va)
				error(Enovmem);
			va -= len;
		}
	}
	va &= ~(BY2PG-1);
	if(va == 0 || va+len > USTKTOP || va+len < va)
		error(Ebadarg);
	if(isoverlap(va, len) != nil)
		error(Esoverlap);
	if(procmemcommit(up, npages) < 0)
		error(Enovmem);
	charged = 1;
	s = newseg(SG_REGION|SG_NOEXEC|(write? 0: SG_RONLY), va, npages);
	s->region = o;
	incref(&o->ref);
	s->regionwriter = 0;
	regionfillseg(s, r);
	/*
	 * The revoked check, writer-slot reservation and Segment publication
	 * are one critical section so a successful revoke can never race with
	 * a new mapping.  Existing mappings are deliberately unaffected.
	 * writeractive is changed under waitlk so WAIT's writer-presence
	 * check and enrollment are linearizable with this acquisition.
	 */
	qlock(&r->lk);
	if(r->revoked){
		qunlock(&r->lk);
		error(Eperm);
	}
	lock(&r->waitlk);
	if(write && r->writeractive){
		unlock(&r->waitlk);
		qunlock(&r->lk);
		error(Einuse);
	}
	if(write){
		r->writeractive = 1;
		r->writergen++;
		s->regionwriter = 1;
		writerheld = 1;
	}
	unlock(&r->waitlk);
	up->seg[sno] = s;
	s = nil;
	charged = 0;
	writerheld = 0;			/* the installed Segment owns the slot */
	qunlock(&r->lk);
	qunlock(&up->seglock);
	poperror();
	vhandput(h);
	flushmmu();
	return va;
}

int
vobjregionunmap(uintptr va)
{
	Segment *s;
	int i;

	if(va == 0 || va >= USTKTOP || (va & (BY2PG-1)) != 0)
		error(Ebadarg);
	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}
	for(i = 0; i < NSEG; i++){
		s = up->seg[i];
		if(s != nil && (s->type&SG_TYPE) == SG_REGION &&
		   va >= s->base && va < s->top)
			break;
	}
	if(i == NSEG)
		error(Ebadarg);
	up->seg[i] = nil;
	procmemdecommit(up, s->size);
	putseg(s);
	qunlock(&up->seglock);
	poperror();
	flushmmu();
	return 0;
}

/*
 * Publication WAIT/WAKE.
 *
 * Authority comes from the caller's installed SG_REGION mapping, not from a
 * Handle or a raw pointer.  This is why an RO observer can wait even after its
 * Handle is closed, and why a guessed address or a Handle from another process
 * conveys nothing.  A writable mapping is the unique writer slot and is
 * required for WAKE.
 *
 * WAIT order is fixed.  Validate the mapping and word; compare the word and
 * return 0 immediately if it differs (even with no writer).  If it still
 * matches and no writer mapping is active, return 3 immediately even for an
 * infinite timeout.  If a writer is active and timeout == 0, return 2;
 * otherwise enroll and sleep.  Status 3 is "no active writer at check time or
 * the previous writer was released", not permanent region destruction: a new
 * writer may map later unless revocation denies new maps.
 *
 * The queue is keyed by the live region object plus its region-relative
 * offset.  A mapping reference keeps the region alive for the whole wait, so
 * a Vregion address cannot be reused behind a registered waiter (no ABA).
 * waitlk protects writeractive, writergen, the word compare and enrollment,
 * so a writer acquisition or release cannot interleave between the check and
 * the sleep.  lk may be held while taking waitlk (mapping admission); waitlk
 * is never held while taking lk, and no path sleeps holding either.
 */
static Segment*
regionmaplookup(uintptr addr)
{
	Segment *s;
	int i;

	if(addr == 0 || (addr & (BY2PG-1)) != 0)
		error(Ebadarg);
	qlock(&up->seglock);
	for(i = 0; i < NSEG; i++){
		s = up->seg[i];
		if(s == nil || addr < s->base || addr >= s->top)
			continue;
		if((s->type & SG_TYPE) != SG_REGION || addr != s->base ||
		   s->region == nil ||
		   ((Vobj*)s->region)->kind != VkindRegion ||
		   ((Vobj*)s->region)->priv == nil){
			qunlock(&up->seglock);
			error(Ebadarg);
		}
		incref(&s->ref);
		qunlock(&up->seglock);
		return s;
	}
	qunlock(&up->seglock);
	error(Ebadarg);
	return nil;
}

static int
regionbadoffset(Vobj *o, uvlong off)
{
	uvlong size = (uvlong)o->size;

	return (off & (sizeof(uvlong)-1)) != 0 || off >= size ||
		size - off < sizeof(uvlong);
}

static uvlong*
regionmapword(Vregion *r, uvlong off, KMap **kp)
{
	KMap *k;

	k = kmap(r->pages[off/BY2PG]);
	*kp = k;
	return (uvlong*)((uchar*)k + (off & (BY2PG-1)));
}

static int
regionwoke(void *arg)
{
	Wwait *w = arg;

	coherence();
	return w->woken;
}

static void
waitadd(Vregion *r, Wwait *w)
{
	w->next = nil;
	w->prev = r->waittail;
	if(r->waittail != nil)
		r->waittail->next = w;
	else
		r->waiters = w;
	r->waittail = w;
	w->onlist = 1;
}

static void
waitdel(Vregion *r, Wwait *w)
{
	if(w->prev != nil)
		w->prev->next = w->next;
	else
		r->waiters = w->next;
	if(w->next != nil)
		w->next->prev = w->prev;
	else
		r->waittail = w->prev;
	w->next = nil;
	w->prev = nil;
	w->onlist = 0;
}

uintptr
sysregionwait(sysva_list list)
{
	uintptr addr;
	uvlong off, expected;
	vlong timeout;
	Segment *s;
	Vobj *o;
	Vregion *r;
	Wwait w;
	KMap *k;
	volatile uvlong *wp;
	int ret;

	addr = sysva_arg(list, uintptr);
	off = sysva_arg(list, uvlong);
	expected = sysva_arg(list, uvlong);
	timeout = sysva_arg(list, vlong);

	s = regionmaplookup(addr);
	o = s->region;
	r = o->priv;
	if(regionbadoffset(o, off)){
		putseg(s);
		error(Ebadarg);
	}
	memset(&w, 0, sizeof w);
	w.r = r;
	w.offset = off;
	wp = regionmapword(r, off, &k);

	lock(&r->waitlk);
	coherence();
	if(*wp != expected){
		unlock(&r->waitlk);
		kunmap(k);
		putseg(s);
		return 0;			/* value already changed */
	}
	if(!r->writeractive){
		unlock(&r->waitlk);
		kunmap(k);
		putseg(s);
		return 3;			/* no writer can WAKE this word */
	}
	if(timeout == 0){
		unlock(&r->waitlk);
		kunmap(k);
		putseg(s);
		return 2;			/* non-blocking check: still equal */
	}
	waitadd(r, &w);
	unlock(&r->waitlk);
	kunmap(k);

	if(waserror()){
		lock(&r->waitlk);
		if(w.onlist)
			waitdel(r, &w);
		unlock(&r->waitlk);
		putseg(s);
		nexterror();			/* e.g. Eintr from a note */
	}
	if(timeout < 0)
		sleep(&w.rz, regionwoke, &w);
	else
		tsleep(&w.rz, regionwoke, &w,
			timeout > 0x7fffffff ? 0x7fffffff : (ulong)timeout);
	poperror();

	lock(&r->waitlk);
	if(w.onlist)
		waitdel(r, &w);
	unlock(&r->waitlk);
	putseg(s);

	if(w.woken == 0)
		ret = 2;			/* timeout */
	else if(w.writer)
		ret = 3;			/* writer mapping released */
	else
		ret = 1;			/* woken, possibly spurious */
	return ret;
}

/* Wake waiters on one region-relative publication word.  The caller must
 * hold the writer authority: either the installed writable mapping (the
 * userspace REGIONWAKE path) or the kernel writer slot.  The queue lock makes
 * each wake and the enrollment check linearizable; no payload is copied. */
static uvlong
regionwakego(Vregion *r, uvlong off, uvlong count)
{
	Wwait *w;
	uvlong n;

	n = 0;
	lock(&r->waitlk);
	if(!r->writeractive){
		unlock(&r->waitlk);
		error(Eperm);
	}
	for(w = r->waiters; w != nil && n < count; w = w->next){
		if(w->offset != off || w->woken)
			continue;
		w->woken = 1;
		coherence();
		wakeup(&w->rz);
		n++;
	}
	unlock(&r->waitlk);
	return n;
}

uintptr
sysregionwake(sysva_list list)
{
	uintptr addr;
	uvlong off, count;
	Segment *s;
	Vobj *o;
	Vregion *r;

	addr = sysva_arg(list, uintptr);
	off = sysva_arg(list, uvlong);
	count = sysva_arg(list, uvlong);

	s = regionmaplookup(addr);
	o = s->region;
	r = o->priv;
	if((s->type & SG_RONLY) != 0 || !s->regionwriter){
		putseg(s);
		error(Eperm);			/* only the writable mapping wakes */
	}
	if(count == 0 || regionbadoffset(o, off)){
		putseg(s);
		error(Ebadarg);
	}
	if(waserror()){
		putseg(s);
		nexterror();
	}
	count = regionwakego(r, off, count);
	poperror();
	putseg(s);
	return count;
}

/* Kernel writer authority for a capability region.  The kernel publishes
 * device/console records without installing a user-visible writable mapping:
 * holding the single writer slot makes every user mapping read-only and makes
 * REGIONWAIT block instead of returning the no-writer status. */
int
vobjregionwriterhold(Vobj *o)
{
	Vregion *r;

	if(o == nil || o->kind != VkindRegion || (r = o->priv) == nil)
		return -1;
	qlock(&r->lk);
	if(r->revoked){
		qunlock(&r->lk);
		return -1;
	}
	lock(&r->waitlk);
	if(r->writeractive){
		unlock(&r->waitlk);
		qunlock(&r->lk);
		return -1;
	}
	r->writeractive = 1;
	r->writergen++;
	unlock(&r->waitlk);
	qunlock(&r->lk);
	return 0;
}

void
vobjregionwriterrelease(Vobj *o)
{
	Vregion *r;

	if(o == nil || o->kind != VkindRegion || (r = o->priv) == nil)
		return;
	regionwriterdown(r);
}

static void
regionkio(Vregion *r, uvlong off, void *buf, ulong n, int write)
{
	uchar *p;
	KMap *k;
	uvlong left, chunk;
	ulong page;

	p = buf;
	while(n != 0){
		page = (ulong)(off / BY2PG);
		if(page >= r->npages)
			error(Ebadarg);
		left = BY2PG - (off & (BY2PG-1));
		chunk = n < left ? n : left;
		k = kmap(r->pages[page]);
		if(waserror()){
			kunmap(k);
			nexterror();
		}
		if(write)
			memmove((uchar*)k + (off & (BY2PG-1)), p, chunk);
		else
			memmove(p, (uchar*)k + (off & (BY2PG-1)), chunk);
		kunmap(k);
		poperror();
		p += chunk;
		off += chunk;
		n -= chunk;
	}
}

static void
regionkstore64(Vregion *r, uvlong off, uvlong v)
{
	KMap *k;
	uvlong *w;

	if((off & 7) != 0 || off + 8 > (uvlong)r->npages*BY2PG)
		error(Ebadarg);
	k = kmap(r->pages[off/BY2PG]);
	w = (uvlong*)((uchar*)k + (off & (BY2PG-1)));
	*w = v;
	coherence();
	kunmap(k);
}


/* Initialize the kernel-owned ring header.  Pages are already zeroed and the
 * caller must hold the writer slot. */
int
vobjregionringinit(Vobj *o)
{
	Vregion *r;
	char hdr[OBJ_RING_HEADER];

	if(o == nil || o->kind != VkindRegion || (r = o->priv) == nil
	|| o->size < OBJ_RING_BYTES)
		return -1;
	memset(hdr, 0, sizeof hdr);
	((ObjRingHeader*)hdr)->version = OBJ_RING_VERSION;
	((ObjRingHeader*)hdr)->magic = OBJ_RING_MAGIC;
	((ObjRingHeader*)hdr)->slot_size = OBJ_RING_SLOT;
	((ObjRingHeader*)hdr)->slots = OBJ_RING_SLOTS;
	regionkio(r, 0, hdr, sizeof hdr, 1);
	coherence();
	return 0;
}

/* Publish one complete record into the fixed ring and wake every waiter on
 * the publication word.  The caller must hold the kernel writer slot. */
int
vobjregionringpublish(Vobj *o, uvlong *seqp, void *data, ulong len)
{
	Vregion *r, *rr;
	uvlong seq, off;
	char hdr[16];

	_Static_assert(sizeof(ObjRingHeader) == 40, "ring header ABI");
	_Static_assert(__builtin_offsetof(ObjRingHeader, write_seq) ==
		OBJ_RING_PUBOFF, "ring publication offset");
	if(o == nil || o->kind != VkindRegion || (r = o->priv) == nil
	|| o->size < OBJ_RING_BYTES || seqp == nil || len > OBJ_RING_DATA)
		return -1;
	rr = r;
	seq = ++*seqp;
	memset(hdr, 0, sizeof hdr);
	off = OBJ_RING_HEADER + ((seq-1) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
	regionkstore64(rr, off, 2*seq-1);
	memmove(hdr, &len, sizeof(unsigned int));
	regionkio(rr, off+8, hdr, 8, 1);
	regionkio(rr, off+16, data, len, 1);
	coherence();
	regionkstore64(rr, off, 2*seq);
	coherence();
	regionkstore64(rr, OBJ_RING_PUBOFF, seq);
	regionwakego(rr, OBJ_RING_PUBOFF, ~0ULL);
	return 0;
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
			 * Ownership invariant through this block:
			 *  - before the handoff DOH exclusively owns the new
			 *    Ohand and O, when it exists, has priv == nil;
			 *  - after the handoff O->priv == DOH and DOH == nil, so
			 *    O is the only owner and vobjput(O) closes the Ospace
			 *    and drops the origin pin;
			 *  - H is a local child Vhand owned by this function until
			 *    it is installed in the child table T.  Once installed,
			 *    procdiscard()/vobjprocexit() owns its cleanup.
			 * The error path releases O when it exists, DOH unless
			 * ownership was transferred to O, and H when it has already
			 * been allocated.
			 */
			doh = nil;
			o = nil;
			h = nil;
			doh = oscopycapdelegate((Ohand*)src->priv,
				((Ohand*)src->priv)->rights);
			if(waserror()){
				if(h != nil)
					vhandput(h);
				if(o != nil)
					vobjput(o);
				/* Before the handoff DOH is still this frame's; after
				 * it DOH is nil and O released it above. */
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
			/* Negative fixture: reproduce the old handoff bug by
			 * detaching the provider before the Vobj is released.
			 * The deliberate leak must fail the T30 ownership log. */
			if(injectfault("inherit-space-leak-ohand")){
				o->priv = nil;
				error("vobj: injected delegated Ohand leak");
			}
#endif
			/* Keep cleanup armed until the new Vhand is installed. */
			h = vhandalloc(o, vi->e[i].rights);
			vi->e[i].gen = gen;
			t->s[vi->e[i].slot].h = h;
			t->s[vi->e[i].slot].gen = gen;
			h = nil;
			poperror();
			vobjput(o);		/* the installed Vhand owns O */
		}else{
			h = vhandalloc(src, vi->e[i].rights);
			vi->e[i].gen = gen;
			t->s[vi->e[i].slot].h = h;
			t->s[vi->e[i].slot].gen = gen;
		}
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
 * once (ambiguous).  SPAWN's explicit Handle plan rejects both absent and
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

	if(kind != VkindBytes)
		error(Ebadarg);
	if(id != nil)
		validaddr((uintptr)id, sizeof *id, 1);
	if(n < 0 || n > Vobjpaymax || (n > 0 && buf == nil))
		error(Ebadarg);
	if((t = vgettab(1)) == nil)
		error(Enomem);
	o = vobjalloc(kind, space, n);
	if(o == nil)
		error(Enomem);
	if(waserror()){
		vobjput(o);
		nexterror();
	}
	if(n > 0)
		memmove(o->data, buf, n);
	rights = Vread|Vmeta|Vsend|Vdelegate;
	h = vhandalloc(o, rights);
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
 h=vhandalloc(o, rights);
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

/*
 * Capability revocation.  For GE/O this revokes the grant lineage; for a
 * volatile region it permanently blocks new mappings through every existing
 * Handle.  Already installed mappings survive until unmap/exit, and the
 * pages remain until the last Handle/mapping reference.  Region revocation
 * requires Vcontrol, so a read-only observer cannot revoke another party.
 */
int
vobjrevoke(int handle)
{
    Vhand *h;
    Vregion *r;

    h = vlookup(handle, 0);
    if(waserror()){vhandput(h);nexterror();}
    if(h->obj->kind == VkindRegion){
        r = h->obj->priv;
        if(r == nil || (h->rights & Vcontrol) == 0)
            error(Eperm);
        qlock(&r->lk);
        r->revoked = 1;
        qunlock(&r->lk);
    }else if(h->obj->kind == VkindGe && h->obj->priv != nil){
        if((h->rights & Vdelegate) == 0)
            error(Eperm);
        osrevokehand(h->obj->priv);
    }else{
        error(Ebadarg);
    }
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
	vhandinit(nh, o, (int)rights);
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

/* Ethernet receive-channel acquisition: return the read-only region Handle.
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
	rights = Vread|Vmeta;
	rights |= h->rights & Vdelegate;
	result = vobjinstallobj(vetherregion(d->ether), rights);
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
 * Proc iomask that sysiowrite used; the provider adds no ambient output
 * authority.  The Handle carries Vwrite
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
	nh = vhandalloc(s->obj, (int)rights);
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
  nh=vhandalloc(bound, (int)rights);
  vobjput(bound);bound=nil;
 }else
  nh=vhandalloc(h->obj, (int)rights);
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
 nh=vhandalloc(bh->obj, (int)rights);
 if(vgettab(1)==nil){vhandput(nh);error(Enomem);}
 handle=vinstall(vgettab(1),nh);
 if(handle<0){vhandput(nh);error("vobj: too many handles");}
 if(id){id->incarnation=bh->obj->incarnation;id->space=bh->obj->space;id->oid=bh->obj->oid;}
 poperror();vhandput(bh);return handle;
}
