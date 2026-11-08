/*
 * NATIVE GE/O KERNEL INTERFACE: the kernel-side native O syscalls
 * (sysovolopen/sysoexecute) implemented below, the internal Ohand
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
#include "ocontext.h"
#include "../../kernel/core/vobjint.h"
#include "../../runtime/abi/namespace.h"
#define GR_KERNEL 1
#include "../../runtime/ge_relations/relations.h"

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
void	onativeput(void*, uvlong, void*, long);
long	onativecreate(void*, void*, long, uvlong*);
uvlong	onativecommit(void*);
void	onativeclose(void*);

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
static ulong onextgen;
static Lock ogenlock;

static int
oboot(void)
{
	return up == bootowner && up->pid == bootpid;
}

static int
ogeneration(void)
{
	ulong g;
	lock(&ogenlock);
	if(onextgen == 0x7fffff){
		unlock(&ogenlock);
		error("O: handle generations exhausted");
	}
	g = ++onextgen;
	unlock(&ogenlock);
	return g;
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

uintptr
sysovolopen(sysva_list list)
{
	char *p, *spec, dev[64];
	vlong off, size;
	void *v;
	int n;

	p = sysva_arg(list, char*);
	n = sysva_arg(list, int);
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
	bootowner = up;
	bootpid = up->pid;
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
	if(oh == nil)
		return;
	if(decref(&oh->ref) != 0)
		return;
	if(oh->provider != nil) octxput(oh->provider);
	if(oh->sp != nil) onativeclose(oh->sp);
	ohandput(oh->origin);
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
Ohand*
osopenspace(char *p, int n)
{
	char name[256];
	Ohand *oh;

	if(!oboot()) error(Eperm);
	if(ovol == nil || p == nil || n <= 0 || n >= (int)sizeof name)
		error(Ebadarg);
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
long
osgetobjecthand(Ohand *oh, uvlong oid, uchar **bp, long max, uvlong *ver)
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
	if((buf = malloc(max)) == nil) error(Enomem);
	n = onativeget(oh->sp, oid, buf, max, &v);
	if(n < 0) error("oexec: no such object");
	if(ver != nil) *ver = v;
 *bp = buf;
	poperror();
	qunlock(ohgate(oh));
	return n;
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

void
oscheckhand(Ohand *h)
{
 if(h==nil) error(Ebadarg);
 qlock(ohgate(h));
 if(waserror()){qunlock(ohgate(h));nexterror();}
 oauthorize(h,h->oid,0);
 poperror();qunlock(ohgate(h));
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
	onativeput(oh->sp, oid, buf, n);
	poperror();
	qunlock(ohgate(oh));
}

/*
 * Persistent create/commit helpers for the canonical OBJ* boundary.  They
 * keep the same Ohand authority and per-Space gate as the historical
 * OSPACECREATE/OSPACECOMMIT calls, but take kernel-validated buffers.
 */
long
oscreatehand(Ohand *oh, void *buf, long n, uvlong *oid)
{
	uvlong v;

	if(oh == nil || oid == nil)
		error(Ebadarg);
	if(n < 0 || n > 64*MiB || (n > 0 && buf == nil))
		error(Ebadarg);
	if(n > 0)
		validaddr((uintptr)buf, n, 0);
	qlock(ohgate(oh));
	if(waserror()){
		qunlock(ohgate(oh));
		nexterror();
	}
	oauthorize(oh, oh->oid, Ocreate);
	v = 0;
	onativecreate(oh->sp, buf, n, &v);
	poperror();
	qunlock(ohgate(oh));
	*oid = v;
	return 0;
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

/* Labels are bounded ASCII roles, never paths. */
static void
orolename(char *dst, char *src, int n)
{
	int i;
	if(n <= 0 || n >= Onamemax) error(Ebadarg);
	validaddr((uintptr)src, n, 0);
	memmove(dst, src, n);
	dst[n] = 0;
	for(i=0; i<n; i++)
		if(!((dst[i]>='a' && dst[i]<='z') || (dst[i]>='A' && dst[i]<='Z') ||
							(dst[i]>='0' && dst[i]<='9') || dst[i]=='_' || dst[i]=='-'))
			error(Ebadarg);
}

/* RFNOMNT is represented by a fully denied device mask after Dev removal. */
static void
onscanextend(void)
{
 int i,denied=1;
 if(up->pgrp==nil) error(Eperm);
 rlock(&up->pgrp->ns);
 for(i=0;i<sizeof up->pgrp->devmask;i++) if(up->pgrp->devmask[i]!=255) denied=0;
 runlock(&up->pgrp->ns);
 if(denied) error(Eperm);
}
static uintptr
obinding(sysva_list list)
{
	Ohand *source, *grant;
	char name[Onamemax], *p;
	uvlong oid, ver;
	int h, n, rights, mode=0;
	h = sysva_arg(list, int);
	oid = sysva_arg(list, uvlong);
	p = sysva_arg(list, char*);
	n = sysva_arg(list, int);
	rights = sysva_arg(list, int);
 mode=(unsigned)rights>>8;rights &= 255;
 if(mode>2) error(Ebadarg);
 onscanextend();
	orolename(name, p, n);
	if(up->pgrp == nil || rights<=0 || (rights & ~(Oall & ~Ocreate))) error(Ebadarg);
	source = ogetspace(h, VkindSpace, vobjrightsfromo(rights | Odelegate));
	qlock(ohgate(source));
	if(waserror()){
		qunlock(ohgate(source));
		ohandput(source);
		nexterror();
	}
	oauthorize(source, oid, rights | Odelegate);
	if(!source->bootstrap){
		/* Rebinding a delegated grant preserves its revocation lineage. */
		if(rights != source->rights) error(Eperm);
		octxbindmode(up->pgrp->ocontext, name, source,mode);
	}else{
		grant = smalloc(sizeof *grant);
		grant->ref.ref = 1;
		if(waserror()){ ohandput(grant); nexterror(); }
		grant->sp = onativeopen(ovol, source->space);
		grant->sid = onativesid(grant->sp);
		if(grant->sid != source->sid) error(Eperm);
		if(onativeget(grant->sp, oid, nil, 0, &ver) < 0) error("O: no such object");
		grant->oid = oid;
		grant->rights = rights;
		strcpy(grant->space, source->space);
		octxbindmode(up->pgrp->ocontext, name, grant,mode);
		poperror();
		ohandput(grant);
	}
	poperror();
	qunlock(ohgate(source));
	ohandput(source);
	return 0;
}

uintptr
sysobindx(sysva_list list)
{return obinding(list);}

/* A view handle is process-local and cannot be used as a storage handle.
 * Export freezes only authorized roles; attachments retain actual grants. */
uintptr
sysonsview(sysva_list list)
{
 char (*roles)[32],name[32],names[32][32]; int nr,i,j,result;uvlong epoch; Ohand *view,*h;
 roles=sysva_arg(list,char (*)[32]); nr=sysva_arg(list,int);
 onscanextend();
 if(nr<=0 || nr>Obindmax) error(Ebadarg);
 validaddr((uintptr)roles,nr*32,0);
 memmove(names,roles,nr*32);roles=names;
 view=smalloc(sizeof *view); view->ref.ref=1;
 if(waserror()){ohandput(view);nexterror();}
 view->provider=octxnew(); if(view->provider==nil) error(Enomem);
 epoch=octxepoch(up->pgrp->ocontext);
 for(i=0;i<nr;i++){
  if(memchr(roles[i],0,32)==nil) error(Ebadarg);
  strcpy(name,roles[i]);
  if(name[0]==0) error(Ebadarg);
  for(j=0;name[j];j++) if(!((name[j]>='a'&&name[j]<='z') ||
   (name[j]>='A'&&name[j]<='Z') || (name[j]>='0'&&name[j]<='9') || name[j]=='_' || name[j]=='-')) error(Ebadarg);
  for(j=0;j<i;j++) if(strcmp(roles[i],roles[j])==0) error(Ebadarg);
  h=octxlookup(up->pgrp->ocontext,name); qlock(ohgate(h));
  if(waserror()){qunlock(ohgate(h));ohandput(h);nexterror();}
  oauthorize(h,h->oid,oboot()?0:Odelegate);
  octxbind(view->provider,name,h);
  poperror();qunlock(ohgate(h));ohandput(h);
 }
 if(epoch!=octxepoch(up->pgrp->ocontext)) error("O: namespace changed; retry");
 result=vobjinstallohand(view, VkindNamespaceView);poperror();return result;
}
uintptr
sysonmount(sysva_list list)
{
 Ohand *view,*grants[Obindmax]; int handle,mode,n=0,i; uvlong attachment;
 handle=sysva_arg(list,int);mode=sysva_arg(list,int);
 onscanextend();if(mode<0 || mode>2) error(Ebadarg);
 view=ogetspace(handle, VkindNamespaceView, Vcontrol);
 if(waserror()){for(i=0;i<n;i++) ohandput(grants[i]);ohandput(view);nexterror();}
 if(view->provider==nil) error(Ebadarg);
 n=octxsnapshot(view->provider,grants,1);
 for(i=0;i<n;i++){
  qlock(ohgate(grants[i]));
  if(grants[i]->revoked || (grants[i]->origin && grants[i]->origin->revoked)){
   qunlock(ohgate(grants[i]));error(Eperm);
  }
  qunlock(ohgate(grants[i]));
 }
 attachment=ogeneration();
 octxcompose(up->pgrp->ocontext,view->provider,mode,attachment);
 poperror();for(i=0;i<n;i++) ohandput(grants[i]);ohandput(view);
 return attachment;
}
uintptr
sysonunmount(sysva_list list)
{
 uvlong id=sysva_arg(list,uvlong);
 if(up->pgrp==nil) error(Eperm);
 octxunmount(up->pgrp->ocontext,id);return 0;
}
uintptr
sysolookup(sysva_list list)
{
	char name[Onamemax], *p;
	uvlong *info, values[3];
	Ohand *oh;
	int n, h;
	p = sysva_arg(list, char*);
	n = sysva_arg(list, int);
	info = sysva_arg(list, uvlong*);
	orolename(name, p, n);
	if(up->pgrp == nil) error(Eperm);
	validaddr((uintptr)info, sizeof values, 1);
	oh = octxlookup(up->pgrp->ocontext, name);
	qlock(ohgate(oh));
	if(waserror()){
		qunlock(ohgate(oh));
		ohandput(oh);
		nexterror();
	}
	/* Rights may be write-only; lookup itself does not read object bytes. */
	oauthorize(oh, oh->oid, 0);
	values[0] = oh->sid;
	values[1] = oh->oid;
	values[2] = oh->rights;
	memmove(info, values, sizeof values);
	h = vobjinstallohand(oh, VkindGe);
	poperror();
	qunlock(ohgate(oh));
	return h;                   /* installed slot owns lookup reference */
}

uintptr
sysounbind(sysva_list list)
{
	char name[Onamemax], *p;
	int n;
	p = sysva_arg(list, char*);
	n = sysva_arg(list, int);
	orolename(name, p, n);
	if(up->pgrp == nil) error(Eperm);
	octxunbind(up->pgrp->ocontext, name);
	return 0;
}

uintptr
sysorevoke(sysva_list list)
{
	Ohand *oh;
	int h = sysva_arg(list, int);
	oh = ogetspace(h, VkindGe, Vdelegate);
	qlock(ohgate(oh));
	if(waserror()){
		qunlock(ohgate(oh));
		ohandput(oh);
		nexterror();
	}
	oauthorize(oh, oh->oid, oboot() ? 0 : Odelegate);
	if(oh->bootstrap) error(Eperm);
	if(oh->origin) oh->origin->revoked = 1;
 oh->revoked = 1;
	/* Release an abandoned transaction even while views retain this grant. */
	onativeclose(oh->sp);
	oh->sp = nil;
	poperror();
	qunlock(ohgate(oh));
	ohandput(oh);
	return 0;
}

/* Snapshot selected bindings before the child can run. Grants stay live:
 * an unbind after selection affects the source map only (COPY), whereas
 * SHARE deliberately observes later edits to the same view. Revocation
 * always affects both. Kernel grant pointers never cross the ABI. */
void
ocontextspawn(Proc *child, int mode, char (*roles)[32], int nr)
{
	void *snapshot;
	Ohand *h;
	int i, j;
	if(mode == 0) return;
	if(up->pgrp == nil) error(Eperm);
	snapshot = octxcopy(up->pgrp->ocontext);
	if(waserror()){ octxput(snapshot); nexterror(); }
	for(i = 0; i < nr; i++){
		if(memchr(roles[i], 0, 32) == nil || roles[i][0] == 0) error(Ebadarg);
		for(j = 0; j < i; j++)
			if(strcmp(roles[j], roles[i]) == 0) error(Ebadarg);
		h = octxlookup(snapshot, roles[i]);
		qlock(ohgate(h));
		if(waserror()){
			qunlock(ohgate(h)); ohandput(h); nexterror();
		}
		oauthorize(h, h->oid, oboot() ? 0 : Odelegate);
		octxcopybinding(child->pgrp->ocontext, snapshot, roles[i]);
		poperror();
		qunlock(ohgate(h));
		ohandput(h);
	}
	if(mode == 2){
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
		closepgrp(child->pgrp);
		child->pgrp = up->pgrp;
		incref(&child->pgrp->ref);
	}
	poperror();
	octxput(snapshot);
}

/* NS4: kernel-mediated R1 reads. Raw metadata never crosses this ABI. */
static QLock relationlock;
static void nsid(Ohand *h,NsIdentity *id) {
 memmove(id->volume,volumeid,16);id->space=h->sid;id->oid=h->oid;
}
static int nsidentity(Ohand *h,NsIdentity *id) {
 return memcmp(id->volume,volumeid,16)==0 && id->space==h->sid && id->oid==h->oid;
}
static int nsreadable(Ohand *h,int rights) {
 return !h->provider && !h->bootstrap && !h->revoked && !(h->origin && h->origin->revoked) && (h->rights&rights)==rights &&
  up->pgrp && octxmember(up->pgrp->ocontext,h);
}
static void nsout(NsOutput *user,void *bytes,usize n) {
 NsOutput out;
 validaddr((uintptr)user,sizeof out,1);memmove(&out,user,sizeof out);
 if(out.capacity>GR_MAX_DATA) error(Ebadarg);
 if(out.buffer){
  if(out.capacity<n) error(Eshort);
  validaddr((uintptr)out.buffer,n,1);memmove((void*)(uintptr)out.buffer,bytes,n);
 }else if(out.capacity) error(Ebadarg);
 out.count=n;memmove(user,&out,sizeof out);
}
/* Lock grants in address order, with duplicates removed, then verify an
 * unchanged map epoch. Never hold the context spinlock during I/O or sleep.
 * This provides a linearizable visibility snapshot without granting handles. */
static int
nsvisibility(Ohand **source,int n,int *allowed)
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
uintptr
sysonsvisible(sysva_list list)
{
 NsOutput *out=sysva_arg(list,NsOutput*); Ohand *h[Obindmax];
 NsVisible result[Obindmax];int n,i,j,k=0;uvlong epoch;
 epoch=up->pgrp?octxepoch(up->pgrp->ocontext):0;
 n=up->pgrp?octxsnapshot(up->pgrp->ocontext,h,0):0;
 if(waserror()){for(i=0;i<n;i++) ohandput(h[i]);nexterror();}
 { int allowed[Obindmax];
 if(!nsvisibility(h,n,allowed) || epoch!=(up->pgrp?octxepoch(up->pgrp->ocontext):0)) error("O: namespace changed; retry");
 for(i=0;i<n;i++) if(allowed[i]){
   for(j=0;j<k;j++) if(nsidentity(h[i],&result[j].object)) break;
   if(j==k){nsid(h[i],&result[k].object);result[k].rights=0;result[k].reserved=0;k++;}
   result[j].rights|=h[i]->rights;
 }
 }
 nsout(out,result,k*sizeof result[0]);
 poperror();for(i=0;i<n;i++) ohandput(h[i]);return k;
}
/* Backends catch native GE errors: no kernel longjmp through the pure C library. */
static int nrread(void *sp,uint64_t oid,void *buf,size_t cap,size_t *size) {
 long n;uvlong ver=0;
 if(waserror()) return -1;
 n=onativeget(sp,oid,nil,0,&ver);
 if(n<0){poperror();return 1;}
 *size=n;
 if(buf){if(n>cap){poperror();return -1;}if(onativeget(sp,oid,buf,n,&ver)!=n){poperror();return -1;}}
 poperror();return 0;
}
static int nrcreate(void *sp,const void *buf,size_t n,uint64_t *oid) {
 if(waserror()) return -1;
 if(onativecreate(sp,(void*)buf,n,oid)<0){poperror();return -1;}poperror();return 0;
}
static int nrwrite(void *sp,uint64_t oid,const void *buf,size_t n) {
 if(waserror()) return -1;onativeput(sp,oid,(void*)buf,n);poperror();return 0;
}
static int nrcommit(void *sp) {
 if(waserror()) return -1;onativecommit(sp);poperror();return 0;
}
static void nrcheck(int rc) {if(rc<0) error(rc==GR_INVALID?Ebadarg:"GE relation operation failed");}
/* Private DB and native Space handle; the process never receives either. */
static GrDB *nropen(Ohand *h,void **sp) {
 GrDB *d;GrBackend b;
 *sp=onativeopen(ovol,h->space);
 if(onativesid(*sp)!=h->sid){onativeclose(*sp);*sp=nil;error(Eperm);}
 d=smalloc(sizeof *d);
 if(waserror()){free(d);nexterror();}
 b.arg=*sp;b.read=nrread;b.create=nrcreate;b.write=nrwrite;b.commit=nrcommit;
 nrcheck(gr_open(d,&b));poperror();return d;
}
/* Bootstrap R1 client operations use the same transaction lock as delegated
 * mutations. NS_BOOT is privileged by original process identity AND a local
 * bootstrap Space handle; its selector is explicitly NsAuthority, not OID. */
static uintptr
nrbootstrap(int op,NsAuthority *user,char *text,int len,NsOutput *output)
{
 NsAuthority authority;Ohand *h;GrDB *d=nil;void *sp=nil;
 uchar *data=nil;char *input=nil;uvlong values[GR_MAX_OBJECTS],oid;
 usize n=0,bytes=0;int locked=0,gate=0,i;
 if(!oboot()) error(Eperm);
 if(op<NS_FIND || op>NS_CREATE || len<0 || len>(op==NS_CREATE?GR_MAX_DATA:4096)) error(Ebadarg);
 validaddr((uintptr)user,sizeof authority,0);memmove(&authority,user,sizeof authority);
 h=ogetspace(authority.handle, VkindSpace, vobjrightsfromo(Oread | (op==NS_CREATE ? Ocreate|Ocommit : (op==NS_ADD || op==NS_REMOVE ? Owrite|Ocommit : 0))));
 if(waserror()){
  if(gate) qunlock(ohgate(h));
  if(sp) onativeclose(sp);
  if(locked) qunlock(&relationlock);
  ohandput(h);free(d);free(data);free(input);nexterror();
 }
 if(len){validaddr((uintptr)text,len,0);input=smalloc(len+1);memmove(input,text,len);input[len]=0;}
 qlock(&relationlock);locked=1;
 qlock(ohgate(h));gate=1;
 if(!h->bootstrap) error(Eperm);
 oauthorize(h,authority.oid,Oread);
 d=nropen(h,&sp);
 if(op==NS_FIND){
  nrcheck(gr_find(d,input?input:"",len,values,GR_MAX_OBJECTS,&n));
  data=smalloc(n*sizeof(NsIdentity)+1);
  for(i=0;i<n;i++){
   NsIdentity id;nsid(h,&id);id.oid=values[i];memmove(data+i*sizeof id,&id,sizeof id);
  }
  nsout(output,data,n*sizeof(NsIdentity));
 }else if(op==NS_GET){
  nrcheck(gr_get(d,authority.oid,nil,0,&n));if(n>GR_MAX_DATA) error(Ebadarg);
  data=smalloc(n+1);nrcheck(gr_get(d,authority.oid,data,n,&n));nsout(output,data,n);
 }else if(op==NS_TAGS){
  nrcheck(gr_tags(d,authority.oid,values,GR_MAX_TAGS,&n));
  data=smalloc(GR_MAX_TAGS*(GR_MAX_NAME+4));
  for(i=0;i<n;i++){
   char *name=(char*)gr_tag_name(d,values[i]);uint length=strlen(name);
   memmove(data+bytes,&length,4);bytes+=4;memmove(data+bytes,name,length);bytes+=length;
  }
  nsout(output,data,bytes);
 }else if(op==NS_CREATE){
  oauthorize(h,0,Ocreate|Ocommit);nrcheck(gr_create(d,input,len,&oid));nsout(output,&oid,sizeof oid);
 }else{
  oauthorize(h,authority.oid,Owrite|Ocommit);
  nrcheck(op==NS_ADD?gr_add(d,authority.oid,input,len):gr_remove(d,authority.oid,input,len));nsout(output,nil,0);
 }
 onativeclose(sp);sp=nil;
 gate=0;qunlock(ohgate(h));locked=0;qunlock(&relationlock);
 poperror();ohandput(h);free(d);free(data);free(input);return 0;
}
uintptr
sysoquery(sysva_list list)
{
 int op=sysva_arg(list,int);
 NsIdentity *selector=sysva_arg(list,NsIdentity*),id;
 char *text=sysva_arg(list,char*),*input=nil;
 int len=sysva_arg(list,int);
 NsOutput *output=sysva_arg(list,NsOutput*);
 Ohand *h[Obindmax],*selected=nil;
 NsIdentity result[Obindmax];
 GrDB *d=nil;void *sp=nil;uchar *data=nil;
 uvlong matches[GR_MAX_OBJECTS],tags[GR_MAX_TAGS];
 usize count=0,bytes=0;int n,i,j,k=0,gate=0,locked=0;uvlong epoch;
 if(op&NS_BOOT) return nrbootstrap(op&~NS_BOOT,(NsAuthority*)selector,text,len,output);
 if(op<NS_FIND || op>NS_REMOVE || len<0 || len>4096) error(Ebadarg);
 if(selector){validaddr((uintptr)selector,sizeof id,0);memmove(&id,selector,sizeof id);}
 else if(op!=NS_FIND) error(Ebadarg);
 if(len){validaddr((uintptr)text,len,0);input=smalloc(len+1);memmove(input,text,len);input[len]=0;}
 epoch=up->pgrp?octxepoch(up->pgrp->ocontext):0;
 n=up->pgrp?octxsnapshot(up->pgrp->ocontext,h,0):0;
 if(waserror()){
  if(gate) qunlock(ohgate(selected));
  if(locked) qunlock(&relationlock);
  if(sp) onativeclose(sp);
  free(d);free(input);free(data);
  for(i=0;i<n;i++) ohandput(h[i]);nexterror();
 }
 qlock(&relationlock);locked=1;
 if(op==NS_FIND){
  for(i=0;i<n;i++){
   qlock(ohgate(h[i]));
   if(nsreadable(h[i],Oread)){
    /* Loading only authorized Spaces prevents metadata existence disclosure. */
    selected=h[i];gate=1;
    if(!d || sp==nil || onativesid(sp)!=h[i]->sid){
     if(sp) onativeclose(sp);sp=nil;free(d);d=nil;
     d=nropen(h[i],&sp);
     nrcheck(gr_find(d,input?input:"",len,matches,GR_MAX_OBJECTS,&count));
    }
    for(j=0;j<count;j++) if(matches[j]==h[i]->oid){
     int t;for(t=0;t<k;t++) if(nsidentity(h[i],&result[t])) break;
     if(t==k) nsid(h[i],&result[k++]);break;
    }
    gate=0;
   }
   qunlock(ohgate(h[i]));
  }
  /* Revalidate all retained grants in one stable visibility epoch. */
  {int allowed[Obindmax];
   if(!nsvisibility(h,n,allowed) || epoch!=(up->pgrp?octxepoch(up->pgrp->ocontext):0)) error("O: namespace changed; retry");
   for(i=0;i<k;){
    int found=0;
    for(j=0;j<n;j++) if(allowed[j] && nsidentity(h[j],&result[i])) found=1;
    if(!found) result[i]=result[--k];else i++;
   }
  }
  nsout(output,result,k*sizeof result[0]);
 }else{
  int rights=Oread | ((op==NS_ADD || op==NS_REMOVE)?Orelate:0);
  for(i=0;i<n;i++) if(nsidentity(h[i],&id)){
   qlock(ohgate(h[i]));
   if(nsreadable(h[i],rights)){selected=h[i];gate=1;break;}
   qunlock(ohgate(h[i]));
  }
  if(!selected) error(Eperm);
  if(op==NS_GET){
   long size;uvlong version;
   size=onativeget(selected->sp,id.oid,nil,0,&version);
   if(size<0 || size>GR_MAX_DATA) error(Eio);
   data=smalloc(size?size:1);
   if(onativeget(selected->sp,id.oid,data,size,&version)!=size) error(Eio);
   nsout(output,data,size);
  }else{
   d=nropen(selected,&sp);
   if(op==NS_TAGS){
    int rc=gr_tags(d,id.oid,tags,GR_MAX_TAGS,&count);
    if(rc==GR_NOTFOUND) count=0;else nrcheck(rc);
    data=smalloc(GR_MAX_TAGS*(GR_MAX_NAME+4));
    for(i=0;i<count;i++){
     char *name=(char*)gr_tag_name(d,tags[i]);uint len=strlen(name);
     memmove(data+bytes,&len,4);bytes+=4;memmove(data+bytes,name,len);bytes+=len;
    }
    nsout(output,data,bytes);
   }else {
    nrcheck(op==NS_ADD?gr_add(d,id.oid,input,len):gr_remove(d,id.oid,input,len));
    nsout(output,nil,0);
   }
  }
  gate=0;qunlock(ohgate(selected));
 }
 if(sp) onativeclose(sp);sp=nil;free(d);d=nil;
 locked=0;qunlock(&relationlock);
 poperror();free(input);free(data);for(i=0;i<n;i++) ohandput(h[i]);return 0;
}
