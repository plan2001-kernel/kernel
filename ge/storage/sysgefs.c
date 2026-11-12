/*
 * NATIVE GE/O KERNEL INTERFACE: the kernel-side native O syscalls
 * (osvolumeopen/sysoexecute) implemented below, the internal Ohand
 * authority and the exported GE shutdown hook.  There is no Dev, no
 * filesystem root and no plan9.ini root= or getest/otest/oobj mount
 * path: nothing here presents objects as files.
 */
#include "u.h"
#include "../../kernel/core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../../kernel/core/error.h"
#include "../../kernel/core/sd.h"
#include "../../kernel/core/lifediag.h"
#include "ocontext.h"
#include "../../kernel/core/vobjint.h"

/*
 * Plan2001: a GE instance (this directory's engine, per-volume instance model in kern.c) is opaque here:
 * its type is gefs.h's, and 6c's cross-module signatures differ for an
 * incomplete struct, so the handle is a void*.
 */
void	gefsshutdown(void*);
void	gestart(void*);
void*	oopenblk(char*, void*, vlong, vlong, int,
		long (*)(void*, void*, long, uvlong, int), int (*)(void*));
typedef struct Gefs Gefs;
typedef struct Ospace Ospace;
typedef struct Oblock Oblock;
struct Oblock {
	SDunit	*unit;
	vlong	start;		/* partition byte offset in the unit */
	vlong	size;		/* partition bytes, for bounds */
};
void*	onativeopen(void*, char*);
uvlong onativesid(void*);
long	onativeget(void*, uvlong, void*, long, uvlong*);
void	onativeput(void*, uvlong, void*, long, uvlong);
long	onativecreate(void*, void*, long, uvlong*, uvlong);
long	ospaceget(void*, uvlong, void*, long, uvlong*);
uvlong	onativecommit(void*);
void	onativeclose(void*);
int	ospaceownerget(void*, uvlong, uvlong*, int*);
void	ospaceownersetlegacy(uvlong);
extern int ownerfault;
extern int ownercrash;
extern uvlong ownerroot(void);
extern void ownerprogrambootregister(void);
void	osownercheck(Ohand*, uvlong oid);

/*
 * Plan2001: the volumes started through OVOLOPEN, for the direct
 * shutdown at reboot.  Each is an independent GE instance with its own
 * device, console and allocation state (GE engine per-volume instance
 * model, kern.c).
 */
enum {
	Maxvols	= 4,
};

static void *vols[Maxvols];
static int nvols;

static void
addvol(void *g)
{
	int i;

	for(i = 0; i < nvols; i++)
		if(vols[i] == g)
			return;
	if(nvols == Maxvols)
		error("gefs: too many volumes");
	vols[nvols++] = g;
}

/*
 * Plan2001: direct teardown for the volumes opened through OVOLOPEN.
 * There is no Dev to receive a shutdown callback, so main.c calls
 * this once.  Each engine instance guards its own halt, and clearing the
 * list makes a repeated call harmless.
 */
void
sysgefsshutdown(void)
{
	int i;

	for(i = 0; i < nvols; i++)
		gefsshutdown(vols[i]);
	nvols = 0;
}

static uvlong
le(uchar *p, int n)
{
	uvlong v;

	v = 0;
	while(n-- > 0)
		v = v<<8 | p[n];
	return v;
}

/* "1b2c..." from a GPT GUID: its first three fields little-endian */
static void
guidstr(char *s, uchar *u)
{
	snprint(s, 37, "%.2ux%.2ux%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux",
		u[3], u[2], u[1], u[0], u[5], u[4], u[7], u[6],
		u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

/*
 * Plan2001: the same GPT parse over a native SD unit, with no Chan and
 * no #S path.  The unit is read at absolute byte offsets; off and size
 * are the matching partition's bounds or nil if it is absent/malformed.
 */
static long
oblockrw(void *v, void *buf, long n, uvlong off, int write)
{
	Oblock *b;
	uvlong end;

	b = v;
	end = b->size;
	if(off > end || n < 0 || (uvlong)n > end - off){
		if(write)
			error(Eio);
		return 0;
	}
	if(n == 0)
		return 0;
	return sdblockrw(b->unit, write, buf, n, b->start + off);
}

static int
oblockflush(void *v)
{
	Oblock *b;

	b = v;
	return sdblockflush(b->unit);
}

static int
oblockgptpart(Oblock *b, char *guid, vlong *off, vlong *size)
{
	uchar h[92], *e, *ents;
	char s[37];
	vlong lb, elba;
	ulong ne, esz;
	int i, bs, found;
	uvlong first, last;

	bs = 0;
	for(lb = 512; lb <= 4096; lb *= 8){
		if(oblockrw(b, h, sizeof h, lb, 0) == sizeof h && memcmp(h, "EFI PART", 8) == 0){
			bs = lb;
			break;
		}
	}
	if(bs == 0)
		return 0;
	elba = le(h+72, 8);
	ne = le(h+80, 4);
	esz = le(h+84, 4);
	if(esz < 128 || esz > 1024 || ne == 0 || ne > 1024)
		return 0;
	ents = smalloc(ne*esz);
	if(waserror()){
		free(ents);
		return 0;
	}
	if(oblockrw(b, ents, ne*esz, elba*bs, 0) != ne*esz)
		error(Eio);
	poperror();
	found = 0;
	for(i = 0; i < ne && !found; i++){
		e = ents + i*esz;
		if(le(e, 8) == 0 && le(e+8, 8) == 0)
			continue;
		guidstr(s, e+16);
		if(cistrcmp(s, guid) == 0){
			first = le(e+32, 8);
			last = le(e+40, 8);
			if(last < first || last >= b->unit->sectors)
				continue;
			*off = first*bs;
			*size = (last - first + 1)*bs;
			found = 1;
		}
	}
	free(ents);
	return found;
}

static Oblock*
oblockopenpart(char *guid, char *dev, int ndev, vlong *off, vlong *size)
{
	Sditer it;
	SDunit *unit;
	Oblock *b;

	memset(&it, 0, sizeof it);
	while((unit = sdunititer(&it)) != nil){
		if(!sdunitready(unit)){
			sdunitput(unit);
			continue;
		}
		if((b = mallocz(sizeof *b, 1)) == nil){
			sdunitput(unit);
			error(Enomem);
		}
		b->unit = unit;
		b->start = 0;
		b->size = (uvlong)unit->sectors * unit->secsize;
		if(oblockgptpart(b, guid, off, size)){
			b->start = *off;
			b->size = *size;
			snprint(dev, ndev, "O lohkolaiteosio (gpt):%s", unit->perm.name);
			return b;
		}
		free(b);
		sdunitput(unit);
	}
	return nil;
}

/*
 * NATIVE GE/O OBJECT IMPLEMENTATION: direct syscalls, not filesystem
 * operations.  These are system calls, not files: no 9P, Fid, qid or
 * /dev entry carries an object.  OVOLOPEN opens the one native O volume
 * by PARTUUID and starts
 * its GE instance; a legacy device specifier is rejected rather than
 * falling back to a pathname/Chan open. OBJOPEN gives a Space handle
 * from the flat catalog. Internal Ohand references authorize storage and
 * namespace operations; a process never sees an Ospace pointer.
 */
static Proc *bootowner;
static ulong bootpid;

static int
oboot(void)
{
	return up == bootowner && up->pid == bootpid;
}

static Gefs *ovol;
static uvlong ovolbytes;
uvlong ovolcapacity(void){return ovolbytes;}
static uchar volumeid[16];

static void*
ovolopenblk(char *spec, char *dev, int ndev, vlong *off, vlong *size, int rw)
{
	Oblock *b;

	if(cistrncmp(spec, "PARTUUID=", 9) != 0)
		return nil;
	b = oblockopenpart(spec+9, dev, ndev, off, size);
	if(b == nil)
		error("no disk has that partition");
	return oopenblk(dev, b, *off, *size, rw, oblockrw, oblockflush);
}

/*
 * Privileged bootstrap/administrative volume activation.  SYSCTL calls this
 * with the already validated user pointer and length; the existing
 * PARTUUID, boot-owner and non-formatting checks are unchanged.
 */
long
osvolumeopen(char *p, int n)
{
	char *spec, dev[64];
	vlong off, size;
	void *v;

	if(n <= 0 || n > 256)
		error(Ebadarg);
	validaddr((uintptr)p, n, 0);
	spec = smalloc(n+1);
	if(waserror()){
		free(spec);
		nexterror();
	}
	memmove(spec, p, n);
	spec[n] = 0;
	if(ovol != nil){
		if(!oboot()) error(Eperm);
		free(spec);
		poperror();
		return 1;
	}
	if(cistrncmp(spec, "PARTUUID=", 9) != 0)
		error("ovolopen: volume spec must be PARTUUID=...");
	v = ovolopenblk(spec, dev, sizeof dev, &off, &size, 1);
	print("o: O volume %s@%lld, %lld MiB, opened without FS (native SD block)\n",
		dev, off, size/MiB);
	gestart(v);
	addvol(v);
	ovolbytes=size;
	ovol = v;
 /* Canonical UUID bytes, rather than an address or collision-prone hash. */
 { int k=0, half=0, digit; char *q;
   for(q=spec+9;*q;q++){
    if(*q=='-') continue;
    digit=*q>='0'&&*q<='9'?*q-'0':(*q>='a'&&*q<='f'?*q-'a'+10:(*q>='A'&&*q<='F'?*q-'A'+10:-1));
    if(digit<0 || k>=16) error(Ebadarg);
    if(!half){volumeid[k]=digit<<4;half=1;}
    else {volumeid[k++]|=digit;half=0;}
   }
   if(k!=16 || half) error(Ebadarg);
 }
	ospaceownersetlegacy(ownerroot());
	bootowner = up;
	bootpid = up->pid;
	/* Trusted boot registration: the actual autostart action remains the
	 * loader's in-place OEXECUTE; this records the launchable entry. */
	ownerprogrambootregister();
	free(spec);
	poperror();
	return 1;
}

/*
 * Kernel-only readiness: open the named GE/O volume by PARTUUID read-only
 * and leave it open for the idle kernel state.  No per-process O handle,
 * Space or userspace object is created.  Returns 1 on success, -1 on error.
 */
int
okernelvol(char *spec)
{
	char dev[64];
	vlong off, size;
	void *v;

	if(ovol != nil)
		return 1;
	if(waserror())
		return -1;
	v = ovolopenblk(spec, dev, sizeof dev, &off, &size, 0);
	print("o: O volume %s@%lld, %lld MiB, opened read-only (kernel-only)\n",
		dev, off, size/MiB);
	ospaceownersetlegacy(ownerroot());
	gestart(v);
	addvol(v);
	ovolbytes = size;
	ovol = v;
	poperror();
	return 1;
}

void
ohandput(Ohand *oh)
{
#ifdef PLAN2001_LIFECYCLE_DIAG
	int delegate;
#endif

	if(oh == nil)
		return;
	if(decref(&oh->ref) != 0)
		return;
#ifdef PLAN2001_LIFECYCLE_DIAG
	/* Only oscopycapdelegate() creates caps=2 capabilities; their own
	 * Ospace and origin pin are separate live resources. */
	delegate = oh->caps == 2;
#endif
	if(oh->provider != nil) octxput(oh->provider);
	if(oh->sp != nil){
		onativeclose(oh->sp);
#ifdef PLAN2001_LIFECYCLE_DIAG
		if(delegate)
			lifedelegate('s', oh->diagid, -1);
#endif
	}
	if(oh->origin != nil){
		ohandput(oh->origin);
#ifdef PLAN2001_LIFECYCLE_DIAG
		if(delegate)
			lifedelegate('p', oh->diagid, -1);
#endif
	}
#ifdef PLAN2001_LIFECYCLE_DIAG
	if(delegate)
		lifedelegate('d', oh->diagid, -1);
#endif
	free(oh);
}

static Ohand*
ogetspace(int h, int kind, int rights)
{
 return vobjholdohand(h, kind, rights);
}

/* Derived capabilities serialize admission and revocation on their origin.
 * The origin reference pins this lock and its revocation state. */
static QLock*
ohgate(Ohand *h)
{
 return h->origin ? &h->origin->gate : &h->gate;
}

/* Caller holds grant gate until the operation completes. */
static void
oauthorize(Ohand *oh, uvlong oid, int rights)
{
	if(oh->provider != nil || oh->revoked || (oh->origin && oh->origin->revoked) || (oh->rights & rights) != rights)
		error(Eperm);
	/*
	 * A capability copy is an explicitly delegated reference to one Space
	 * handle.  Its rights and revocation are still checked; the process
	 * view-membership check is intentionally not repeated because the
	 * capability may cross a process boundary.  The copy owns its own
	 * Ospace handle and pins its object-grant origin for revocation. caps=2
  * is an explicitly delegated Space, not inherited bootstrap authority.
	 */
	if(oh->caps){
		if(oh->caps == 1 && oid != oh->oid)
			error(Eperm);
		return;
	}
	if(oh->bootstrap){
		if(!oboot()) error(Eperm);
	}else if(up->pgrp == nil || !octxmember(up->pgrp->ocontext, oh) || oid != oh->oid)
		error(Eperm);
}

void
oprocexit(Proc *p)
{
 if(p == bootowner){ bootowner = nil; bootpid = 0; }
}

void
oprocexec(Proc *p)
{
 USED(p);
}

/*
 * Authorized Space capability acquisition for the canonical
 * OBJOPEN(ObjOpenSpace) path.  The caller owns the returned
 * reference.  The name is a bounded Space name, not a pathname; bootstrap
 * authority (the mounted O volume owner) is still required.
 */
static Ohand*
osopenspacecore(char *p, int n, int user)
{
	char name[256];
	Ohand *oh;

	if(!oboot()) error(Eperm);
	if(ovol == nil || p == nil || n <= 0 || n >= (int)sizeof name)
		error(Ebadarg);
	if(user)
		validaddr((uintptr)p, n, 0);
	memmove(name, p, n);
	name[n] = 0;
	if(memchr(name, 0, n) != nil) error(Ebadarg);
	oh = smalloc(sizeof *oh);
	oh->ref.ref = 1;
	if(waserror()){ ohandput(oh); nexterror(); }
	oh->sp = onativeopen(ovol, name);
	oh->sid = onativesid(oh->sp);
	strcpy(oh->space, name);
	oh->rights = Oall;
	oh->bootstrap = 1;
	poperror();
	return oh;
}

Ohand*
osopenspace(char *p, int n)
{
	return osopenspacecore(p, n, 1);
}

/* Trusted kernel/registered-program Space open; the name is a kernel
 * string and no user-address validation is performed. */
Ohand*
osopenspacek(char *p, int n)
{
	return osopenspacecore(p, n, 0);
}

enum {
	Objexecmax	= 8*MiB,
};

/*
 * Narrow exported helper for OSPAWN: read at most MAX published bytes from
 * one Ohand/OID into a fresh kernel buffer and return it with the version.
 * The caller owns OH and its reference for the duration of the call; this
 * helper does not drop it, because the OBJ Space provider keeps its own
 * pinned capability.  No Ospace or disk handle escapes.
 */
static long
osgetobjecthandcore(Ohand *oh, uvlong oid, uchar **bp, long max, uvlong *ver,
	int checkintegrity)
{
	uchar *buf = nil;
	uvlong v;
	long n;
	if(oh == nil || bp == nil || max <= 0 || max > Objexecmax)
		error(Ebadarg);
 *bp = nil;
	qlock(ohgate(oh));
	if(waserror()){
		free(buf);
		qunlock(ohgate(oh));
		nexterror();
	}
	oauthorize(oh, oid, Oexecute);
	if(checkintegrity)
		osownercheck(oh, oid);
	if((buf = malloc(max)) == nil) error(Enomem);
	n = onativeget(oh->sp, oid, buf, max, &v);
	if(n < 0) error("oexec: no such object");
	if(ver != nil) *ver = v;
 *bp = buf;
	poperror();
	qunlock(ohgate(oh));
	return n;
}

long
osgetobjecthand(Ohand *oh, uvlong oid, uchar **bp, long max, uvlong *ver)
{
	return osgetobjecthandcore(oh, oid, bp, max, ver, 1);
}

/* Trusted kernel/registered-program load: registry authority already
 * validated. Ordinary executable reads check owner-record integrity too. */
long
osgetobjecthandk(Ohand *oh, uvlong oid, uchar **bp, long max, uvlong *ver)
{
	return osgetobjecthandcore(oh, oid, bp, max, ver, 0);
}

/* Space Handle form used by OEXECUTE. It resolves the unified process-local
 * Vhand to an internal Ohand, then shares
 * the one read/authorization core above. */
long
osgetobject(int h, uvlong oid, uchar **bp, long max, uvlong *ver)
{
	Ohand *oh;
	long n;

	oh = ogetspace(h, VkindSpace, Vexecute);
	if(waserror()){
		ohandput(oh);
		nexterror();
	}
	n = osgetobjecthand(oh, oid, bp, max, ver);
	poperror();
	ohandput(oh);
	return n;
}

void
oshandput(Ohand *oh)
{
	ohandput(oh);
}

int
oshandrights(Ohand *oh)
{
	return oh == nil ? 0 : oh->rights;
}

uvlong
ossid(Ohand *oh)
{
	return oh == nil ? 0 : oh->sid;
}

void
osvolumeid(Ohand *oh, uchar *out)
{
	USED(oh);
	if(out != nil)
		memmove(out, volumeid, 16);
}

Ohand*
oscopycap(Ohand *src, uvlong oid)
{
	Ohand *n;

	if(src == nil) error(Eperm);
 qlock(ohgate(src));
 if(waserror()){qunlock(ohgate(src));nexterror();}
 oauthorize(src, oid, 0);
	osownercheck(src, oid);
	if(ovol == nil)
		error("O: no volume");
	n = smalloc(sizeof *n);
	n->ref.ref = 1;
	n->rights = src->rights;
	n->sid = src->sid;
	n->oid = oid;
	n->bootstrap = 0;
	n->caps = 1;
	memmove(n->space, src->space, sizeof n->space);
	n->space[sizeof n->space - 1] = 0;
	if(waserror()){
		free(n);
		nexterror();
	}
	n->sp = onativeopen(ovol, n->space);
	poperror();
	if(n->sp == nil){
		free(n);
		error("O: capability open failed");
	}
 if(onativesid(n->sp) != src->sid){
  onativeclose(n->sp);free(n);error(Eperm);
 }
 if(!src->bootstrap && src->caps != 2){
  n->origin = src->origin ? src->origin : src;
  incref(&n->origin->ref);
 }
 poperror();qunlock(ohgate(src));
	return n;
}

/*
 * Explicitly delegated Space capability for a child process.  Unlike the
 * bootstrap Ohand obtained by osopenspace(), this copy is not tied to the
 * boot owner: oauthorize() accepts a caps=2 Space capability across a
 * process boundary.  The caller passes the already-validated O-rights
 * attenuation; the copy owns its own opened Ospace and gate.
 */
Ohand*
oscopycapdelegate(Ohand *src, int rights)
{
	Ohand *n;

	if(src == nil)
		error(Eperm);
	if(rights == 0 || (rights & ~src->rights) != 0)
		error(Eperm);
	qlock(ohgate(src));
	if(waserror()){
		qunlock(ohgate(src));
		nexterror();
	}
	oauthorize(src, src->oid, 0);
	if(ovol == nil)
		error("O: no volume");
	n = smalloc(sizeof *n);
	n->ref.ref = 1;
	n->rights = rights;
	n->sid = src->sid;
	n->oid = 0;
	n->bootstrap = 0;
	n->caps = 2;
	memmove(n->space, src->space, sizeof n->space);
	n->space[sizeof n->space - 1] = 0;
	if(waserror()){
		free(n);
		nexterror();
	}
	n->sp = onativeopen(ovol, n->space);
	poperror();
	if(n->sp == nil){
		free(n);
		error("O: capability open failed");
	}
	if(onativesid(n->sp) != src->sid){
		onativeclose(n->sp);
		free(n);
		error(Eperm);
	}
	/* Pin the grant origin across the process boundary.  The capability
	 * copy owns its own Ospace, but revocation and its gate continue to
	 * follow the originating Ohand. */
	n->origin = src->origin ? src->origin : src;
	incref(&n->origin->ref);
#ifdef PLAN2001_LIFECYCLE_DIAG
	/* Publish the three live resources only after every fallible step.
	 * ohandput() emits the matching frees exactly once at final close. */
	n->diagid = lifedelegateid();
	lifedelegate('s', n->diagid, 1);
	lifedelegate('p', n->diagid, 1);
	lifedelegate('d', n->diagid, 1);
#endif
	poperror();
	qunlock(ohgate(src));
	return n;
}

void
oscheckhand(Ohand *h)
{
 if(h==nil) error(Ebadarg);
 qlock(ohgate(h));
 if(waserror()){qunlock(ohgate(h));nexterror();}
 oauthorize(h,h->oid,0);
 poperror();qunlock(ohgate(h));
}

/*
 * The persistent object's immutable owner.  Before a Space is upgraded to
 * owner metadata, a missing Kowner record is a seeded object owned by
 * root.  After the upgrade every live object has a record, so a missing
 * one is corruption.  Knowing a SpaceID/OID never selects or changes it.
 */
uvlong
osownerof(Ohand *oh, uvlong oid)
{
	uvlong owner;
	int required;

	if(oh == nil || oh->sp == nil || oid == 0)
		return ownerroot();
	if(ospaceownerget(oh->sp, oid, &owner, &required))
		return owner;
	/* An absent or tombstoned OID has no owner to report.  It is not a
	 * live object missing metadata; opening it yields a handle whose
	 * read reports absence.  A live object with no record is corruption. */
	if(required && ospaceget(oh->sp, oid, nil, 0, nil) >= 0)
		error("O: corrupt object owner metadata");
	return ownerroot();
}

/*
 * Owner metadata is immutable provenance, NOT access authority.
 * oauthorize() has already validated the Ohand, its attenuated rights,
 * optional one-object scope and its revocation gate. This check retains
 * the owner-record integrity invariant: a missing Kowner for an existing
 * object in an owner-aware Space is corruption, not implicit permission.
 * The legacy seeded Space without Kowner records is still readable by
 * holders of explicitly authorized capabilities.
 *
 * On first publication, the storage layer records the writer's OwnerID
 * exactly once; subsequent delegated writes preserve that original OwnerID.
 */
static void
osownercheckcore(Ohand *oh, uvlong oid)
{
	uvlong owner;
	int required;

	if(oid == 0)
		return;
	if(ospaceownerget(oh->sp, oid, &owner, &required))
		return;
	/* A missing OID may be created with an Owrite capability. */
	if(ospaceget(oh->sp, oid, nil, 0, nil) < 0)
		return;
	if(required)
		error("O: corrupt object owner metadata");
}

void
osownercheck(Ohand *oh, uvlong oid)
{
	osownercheckcore(oh, oid);
}

static void
osownercheckcreate(Ohand *oh, uvlong oid)
{
	osownercheckcore(oh, oid);
}

/* Deterministic test-only create/publication fault point. */
int
osownerfault(int point)
{
	/* 101..104 arm the hard-stop checkpoints used by the crash-recovery
	 * runner; 1..4 keep the in-kernel error fault points (4: migration). */
	if(point >= 101 && point <= 104){
		ownercrash = point;
		return 0;
	}
	if(point < 0 || point > 4)
		error(Ebadarg);
	ownerfault = point;
	return 0;
}

long
osreadhand(Ohand *oh, uvlong oid, void *buf, long n, uvlong *ver)
{
	uvlong v = 0;
	long r;

	if(oh == nil)
		error(Ebadarg);
	if(n < 0 || n > 64*MiB)
		error(Ebadarg);
	if(n > 0)
		validaddr((uintptr)buf, n, 1);
	/* VER is a kernel pointer supplied by the OBJ provider; the legacy
	 * OSPACEGET boundary validates its own user pointer separately. */
	qlock(ohgate(oh));
	if(waserror()){
		qunlock(ohgate(oh));
		nexterror();
	}
	oauthorize(oh, oid, Oread);
	osownercheck(oh, oid);
	r = onativeget(oh->sp, oid, buf, n, &v);
	poperror();
	qunlock(ohgate(oh));
	if(ver != nil)
		*ver = v;
	return r;
}

void
oswritehand(Ohand *oh, uvlong oid, void *buf, long n)
{
	if(oh == nil)
		error(Ebadarg);
	if(n < 0 || n > 64*MiB)
		error(Ebadarg);
	if(n > 0)
		validaddr((uintptr)buf, n, 0);
	qlock(ohgate(oh));
	if(waserror()){
		qunlock(ohgate(oh));
		nexterror();
	}
	oauthorize(oh, oid, Owrite);
	osownercheckcreate(oh, oid);
	onativeput(oh->sp, oid, buf, n,
		up != nil && up->owner != 0 ? up->owner : ownerroot());
	poperror();
	qunlock(ohgate(oh));
}

/*
 * Persistent create/commit helpers for the canonical OBJ* boundary.  They
 * keep the same Ohand authority and per-Space gate as the historical
 * OSPACECREATE/OSPACECOMMIT calls, but take kernel-validated buffers.
 */
static long
oscreatehandcore(Ohand *oh, void *buf, long n, uvlong *oid, uvlong owner)
{
	uvlong v;

	if(oh == nil || oid == nil)
		error(Ebadarg);
	if(n < 0 || n > 64*MiB || (n > 0 && buf == nil))
		error(Ebadarg);
	qlock(ohgate(oh));
	if(waserror()){
		qunlock(ohgate(oh));
		nexterror();
	}
	oauthorize(oh, oh->oid, Ocreate);
	v = 0;
	onativecreate(oh->sp, buf, n, &v, owner);
	poperror();
	qunlock(ohgate(oh));
	*oid = v;
	return 0;
}

/* Trusted installation specifies the NEW object's immutable provenance;
 * installation does not grant read/write access to a matching OwnerID. */
long
oscreatehandowner(Ohand *oh, void *buf, long n, uvlong *oid, uvlong owner)
{
	return oscreatehandcore(oh, buf, n, oid, owner);
}

long
oscreatehand(Ohand *oh, void *buf, long n, uvlong *oid)
{
	if(n > 0)
		validaddr((uintptr)buf, n, 0);
	return oscreatehandcore(oh, buf, n, oid,
		up != nil && up->owner != 0 ? up->owner : ownerroot());
}

long
oscommithand(Ohand *oh, uvlong *ver)
{
	uvlong v;

	if(oh == nil)
		error(Ebadarg);
	qlock(ohgate(oh));
	if(waserror()){
		qunlock(ohgate(oh));
		nexterror();
	}
	oauthorize(oh, oh->oid, Ocommit);
	v = onativecommit(oh->sp);
	poperror();
	qunlock(ohgate(oh));
	if(ver != nil)
		*ver = v;
	return 0;
}

uintptr
sysoexecute(sysva_list list)
{
	uchar *buf;
	char **argv;
	uvlong oid, ver;
	long n;
	int h, argc;
	h = sysva_arg(list, int);
	oid = sysva_arg(list, uvlong);
	argv = sysva_arg(list, char**);
	argc = sysva_arg(list, int);
	if(argc < 0 || argc > 32 || (argc && argv == nil)) error(Ebadarg);
	n = osgetobject(h, oid, &buf, Objexecmax, &ver);
	print("o: OEXECUTE oid=%#llux len=%ld ver=%#llux\n", oid, n, ver);
	return execo(oid, buf, n, argv, argc);
}

int
obootstrap(Proc *p)
{
	return p != nil && p == bootowner && p->pid == bootpid;
}

void
oscheckrole(void *context, char *name, int geonly)
{
 Ohand *h;void *binding;
 if(context==nil) error(Eperm);
 binding=octxobject(context,name);
 if(waserror()){vobjbindingput(binding);nexterror();}
 if(!vobjbindingdelegable(binding) || (geonly && vobjbindinggecap(binding)==nil)) error(Eperm);
 h=vobjbindingcap(binding);
 if(h==nil){poperror();vobjbindingput(binding);return;}
 qlock(ohgate(h));
 if(waserror()){
  qunlock(ohgate(h));nexterror();
 }
 oauthorize(h,h->oid,Odelegate);
 poperror();qunlock(ohgate(h));
 poperror();vobjbindingput(binding);
}

void
osviewgrantvalid(Ohand *h)
{
 if(h==nil) error(Ebadarg);
 qlock(ohgate(h));
 if(h->revoked || (h->origin && h->origin->revoked)){
  qunlock(ohgate(h));error(Eperm);
 }
 qunlock(ohgate(h));
}

/* GE/O authority revocation is distinct from Handle close and role removal.
 * Descendants sharing an origin are revoked; independently opened grants are not. */
void
osrevokehand(Ohand *h)
{
 if(h==nil) error(Ebadarg);
 qlock(ohgate(h));
 if(waserror()){qunlock(ohgate(h));nexterror();}
 oauthorize(h,h->oid,Odelegate);
 if(h->bootstrap) error(Eperm);
 if(h->origin) h->origin->revoked=1;
 h->revoked=1;
 if(h->sp){onativeclose(h->sp);h->sp=nil;}
 poperror();qunlock(ohgate(h));
}

/* Snapshot selected bindings before the child can run. Grants stay live:
 * an unbind after selection affects the source map only (COPY), whereas
 * SHARE deliberately observes later edits to the same view. Revocation
 * always affects both. Kernel grant pointers never cross the ABI. */
void
ocontextspawn(Proc *child, int mode, char (*roles)[32], int nr)
{
	void *snapshot;
	int i, j;
	if(mode == 0) return;
	if(up->pgrp == nil) error(Eperm);
	snapshot = octxcopy(up->pgrp->ocontext);
	if(waserror()){ octxput(snapshot); nexterror(); }
	for(i = 0; i < nr; i++){
		if(memchr(roles[i], 0, 32) == nil || roles[i][0] == 0) error(Ebadarg);
		for(j = 0; j < i; j++)
			if(strcmp(roles[j], roles[i]) == 0) error(Ebadarg);
		oscheckrole(snapshot, roles[i], 0);
		octxcopybinding(child->pgrp->ocontext, snapshot, roles[i]);
	}
	if(mode == 2){
  if(!oboot() && !octxshareable(snapshot)) error(Eperm);
  Ohand *layers[Obindmax]; int count=octxsnapshot(snapshot,layers,1),k;
  for(k=0;k<count;k++){
   int ok;
   qlock(ohgate(layers[k]));
   ok=!layers[k]->revoked && !(layers[k]->origin && layers[k]->origin->revoked) && (oboot() || (layers[k]->rights&Odelegate));
   qunlock(ohgate(layers[k]));
   if(!ok){for(int t=0;t<count;t++) ohandput(layers[t]);error(Eperm);}
  }
  for(k=0;k<count;k++) ohandput(layers[k]);
		/* SHARE opts into the whole mutable view, with every role listed. */
		if(!octxsame(snapshot, child->pgrp->ocontext) ||
					!octxmatches(up->pgrp->ocontext, snapshot)) error("O: selected view changed");
		/*
		 * Acquire the parent's reference before dropping the child's
		 * fresh group.  The child stays unpublished, but ordering the
		 * transfer this way never leaves it with a released Pgrp and
		 * makes a later spawn-failure rollback unambiguous.
		 */
		{
			Pgrp *parent = up->pgrp;
			Pgrp *old;

			incref(&parent->ref);
			old = child->pgrp;
			child->pgrp = parent;
			closepgrp(old);
		}
	}
	poperror();
	octxput(snapshot);
}

/* Current object-ABI namespace visibility. */
static int nsreadable(Ohand *h,int rights) {
 return !h->provider && !h->bootstrap && !h->revoked && !(h->origin && h->origin->revoked) && (h->rights&rights)==rights &&
  up->pgrp && octxmember(up->pgrp->ocontext,h);
}
/* Lock grants in address order, with duplicates removed, then verify an
 * unchanged map epoch. Never hold the context spinlock during I/O or sleep.
 * This provides a linearizable visibility snapshot without granting handles. */
int
osvisibility(Ohand **source,int n,int *allowed)
{
 Ohand *ordered[Obindmax],*tmp;int i,j,k=0,stable;uvlong epoch;
 for(i=0;i<n;i++){
  Ohand *root=source[i]->origin?source[i]->origin:source[i];
  for(j=0;j<k;j++) if(ordered[j]==root) break;
  if(j==k) ordered[k++]=root;
 }
 for(i=1;i<k;i++) for(j=i;j>0 && (uintptr)ordered[j]<(uintptr)ordered[j-1];j--){
  tmp=ordered[j];ordered[j]=ordered[j-1];ordered[j-1]=tmp;
 }
 for(i=0;i<k;i++) qlock(&ordered[i]->gate);
 epoch=up->pgrp?octxepoch(up->pgrp->ocontext):0;
 for(i=0;i<n;i++) allowed[i]=nsreadable(source[i],Oread);
 stable=epoch==(up->pgrp?octxepoch(up->pgrp->ocontext):0);
 for(i=k;i>0;i--) qunlock(&ordered[i-1]->gate);
 return stable;
}
