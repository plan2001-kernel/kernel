/*
 * Plan2001: what gefs has from libc and from its main.c, in the kernel
 * (gefs.h).  The native GE/O engine starts its kprocs (the sweeper,
 * the syncers and the tasks) and reaches its disk through the bounded
 * native block callback.  Each call makes a GE instance of its own (GE
 * volume instance isolation): volumes share the engine's code, not its
 * state, so two of them run at the same time on different partitions.
 */
#include "gefs.h"

/* the kernel's, below: its spin locks, sleep and error */
#undef	Lock
#undef	lock
#undef	unlock
#undef	canlock
#undef	error
#undef	sleep

int	debug;
int	permissive;
int	usereserve;
char	*reamuser;
int	noneid		= 0;
int	nogroupid	= 9999;
int	admid		= -1;

static void	gwritetrace(Gefs*, vlong);

/*
 * libc's atomics: the read a load, the rest with one of Nalock spin
 * locks (by the address), which are barriers too
 */
enum {
	Nalock	= 64,
};
static Lock alock[Nalock];

#define	ALOCK(p)	(&alock[((uintptr)(p)>>3) % Nalock])

long
agetl(Along *a)
{
	return *(volatile long*)&a->v;
}

vlong
agetv(Avlong *a)
{
	return *(volatile vlong*)&a->v;
}

void*
agetp(Aptr *a)
{
	return *(void* volatile*)&a->v;
}

long
aswapl(Along *a, long v)
{
	Lock *l;
	long o;

	l = ALOCK(a);
	lock(l);
	o = a->v;
	a->v = v;
	unlock(l);
	return o;
}

vlong
aswapv(Avlong *a, vlong v)
{
	Lock *l;
	vlong o;

	l = ALOCK(a);
	lock(l);
	o = a->v;
	a->v = v;
	unlock(l);
	return o;
}

void*
aswapp(Aptr *a, void *v)
{
	Lock *l;
	void *o;

	l = ALOCK(a);
	lock(l);
	o = a->v;
	a->v = v;
	unlock(l);
	return o;
}

/* the new value, as libc's */
long
aincl(Along *a, long d)
{
	Lock *l;
	long v;

	l = ALOCK(a);
	lock(l);
	v = a->v += d;
	unlock(l);
	return v;
}

vlong
aincv(Avlong *a, vlong d)
{
	Lock *l;
	vlong v;

	l = ALOCK(a);
	lock(l);
	v = a->v += d;
	unlock(l);
	return v;
}

int
acasl(Along *a, long o, long n)
{
	Lock *l;
	int r;

	l = ALOCK(a);
	lock(l);
	r = a->v == o;
	if(r)
		a->v = n;
	unlock(l);
	return r;
}

int
acasv(Avlong *a, vlong o, vlong n)
{
	Lock *l;
	int r;

	l = ALOCK(a);
	lock(l);
	r = a->v == o;
	if(r)
		a->v = n;
	unlock(l);
	return r;
}

int
acasp(Aptr *a, void *o, void *n)
{
	Lock *l;
	int r;

	l = ALOCK(a);
	lock(l);
	r = a->v == o;
	if(r)
		a->v = n;
	unlock(l);
	return r;
}

/*
 * libc's %r is an errstr system call: in the kernel it is up->errstr,
 * put into the format as text
 */
static void
vfmtr(char *buf, int nbuf, char *fmt, va_list arg)
{
	char nfmt[ERRMAX+256], *p, *f, *e, *s;

	p = fmt;
	f = nfmt;
	e = nfmt + sizeof nfmt - 1;
	while(*p != 0 && f < e){
		if(p[0] == '%' && p[1] == 'r'){
			for(s = up->errstr; *s != 0 && f < e-1; s++){
				if(*s == '%')
					*f++ = '%';
				*f++ = *s;
			}
			p += 2;
			continue;
		}
		if(p[0] == '%' && p[1] == '%' && f < e-1){
			*f++ = *p++;
			*f++ = *p++;
			continue;
		}
		*f++ = *p++;
	}
	*f = 0;
	vsnprint(buf, nbuf, nfmt, arg);
}

_Noreturn void
gerror(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	error(buf);
}

_Noreturn void
broke(Gefs *fs, char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	aincl(&fs->rdonly, 1);
	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	print("gefs: %s\n", buf);
	error(buf);
}

_Noreturn void
gsysfatal(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	panic("gefs: %s", buf);
}

_Noreturn void
_babort(Gefs *fs, vlong addr, char *msg)
{
	gwritetrace(fs, addr);
	panic("gefs: assert failed for [%llx]: %s", addr, msg);
}

void
gwerrstr(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	kstrcpy(up->errstr, buf, ERRMAX);
}

/* fprint's output is the kernel console: the native engine has no fd table */
int
gfprint(Gefs *fs, int fd, char *fmt, ...)
{
	char buf[512];
	va_list arg;
	int n;

	USED(fs);
	USED(fd);
	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	n = strlen(buf);
	print("%s", buf);
	return n;
}

/* main.c's trace (plan9.ini gefstrace=MiB), by pid */
void
_trace(Gefs *fs, char *msg, Bptr bp, vlong v0, vlong v1)
{
	Trace *t;
	ulong idx;

	idx = aincl(&fs->traceidx, 1);
	t = &fs->trace[(idx-1) % fs->ntrace];
	strecpy(t->msg, t->msg+sizeof(t->msg), msg);
	t->tid = up != nil ? up->pid : 0;
	t->qgen = agetv(&fs->qgen);
	t->bp = bp;
	t->v0 = v0;
	t->v1 = v1;
}

/* main.c's writetrace: what the trace has of addr (or all, -1), oldest first */
static void
gwritetrace(Gefs *fs, vlong addr)
{
	Trace *t;
	long ti;
	int i;

	if(fs == nil || fs->trace == nil)
		return;
	ti = agetl(&fs->traceidx);
	for(i = 0; i < fs->ntrace; i++){
		t = &fs->trace[(ti+i) % fs->ntrace];
		if(t->msg[0] == 0)
			continue;
		if(addr != -1 && t->bp.addr != -1 && t->bp.addr != addr && t->v0 != addr)
			continue;
		if(addr != -1 && t->bp.addr == -1 && t->v0 != addr)
			continue;
		print("[%d@%d] %s", t->tid, t->qgen, t->msg);
		if(t->bp.addr != -1)
			print(" %B", t->bp);
		if(t->v0 != -1)
			print(" %llx", t->v0);
		if(t->v1 != -1)
			print(" %llx", t->v1);
		print("\n");
	}
}

void*
emalloc(usize sz, int zero)
{
	void *p;

	if((p = mallocz(sz, zero)) == nil)
		gerror(Enomem);
	setmalloctag(p, getcallerpc(&sz));
	return p;
}

void
gsleep(long ms)
{
	if(ms <= 0)
		yield();
	else
		tsleep(&up->sleep, return0, nil, ms);
}

/* the kernel has wlock but not this */
int
canwlock(RWLock *q)
{
	lock(&q->use);
	if(q->readers == 0 && q->writer == 0){
		q->wpc = getcallerpc(&q);
		q->writer = 1;
		unlock(&q->use);
		return 1;
	}
	unlock(&q->use);
	return 0;
}

/*
 * libc's Rendez: a sleeper is queued before r->l is let go, so no
 * wakeup is missed; the waker wakes it with r->wl held, so it is still
 * there (its Gsleeper on its stack) when it is woken.
 */
static int
woken(void *a)
{
	return ((Gsleeper*)a)->woken;
}

void
rsleep(Grendez *r)
{
	Gsleeper s;

	memset(&s, 0, sizeof s);
	lock(&r->wl);
	if(r->tail != nil)
		r->tail->next = &s;
	else
		r->head = &s;
	r->tail = &s;
	unlock(&r->wl);
	qunlock(r->l);
	while(!s.woken){
		if(waserror())
			continue;	/* a note: still queued, sleep on */
		sleep(&s.rz, woken, &s);
		poperror();
	}
	/* the waker took it off the queue; waiting for it to let go of wl */
	lock(&r->wl);
	unlock(&r->wl);
	qlock(r->l);
}

static int
wake1(Grendez *r)
{
	Gsleeper *s;

	if((s = r->head) == nil)
		return 0;
	r->head = s->next;
	if(r->head == nil)
		r->tail = nil;
	s->next = nil;
	s->woken = 1;
	wakeup(&s->rz);
	return 1;
}

int
rwakeup(Grendez *r)
{
	int n;

	lock(&r->wl);
	n = wake1(r);
	unlock(&r->wl);
	return n;
}

int
rwakeupall(Grendez *r)
{
	int n;

	lock(&r->wl);
	for(n = 0; wake1(r); n++)
		;
	unlock(&r->wl);
	return n;
}

/*
 * The disk's volatile write cache to the medium, after each wrwait in
 * gefs's sync (the arena heads, the superblocks, the arena tails).  The
 * native block provider's flush does what the drivers make ATA's flush
 * cache, NVMe's flush, virtio's.  A failure is reported once.
 */
void
gflushdev(Gefs *fs)
{
	if(fs->oblk == nil)
		return;
	if(waserror()){
		if(!fs->k.flushwarned)
			print("gefs: %s: cache flush: %s\n", "native block", up->errstr);
		fs->k.flushwarned = 1;
		return;
	}
	if(fs->oblkflush(fs->oblk) < 0)
		error(Eio);
	poperror();
	fs->k.nflush++;
}

/* halted (the console's halt): read-only, synced; fshalt waits for this */
void
gefshalted(Gefs *fs)
{
	print("gefs: halted\n");
	fs->k.halted = 1;
	aswapl(&fs->rdonly, 1);
}

/*
 * main.c's, with the blocks from the kernel's heap.  The native open
 * makes the volume: every GE object it creates is bound to it -- the
 * tree, the dlists and the blocks carry their volume, so the engine's
 * functions find it in the data they were given.
 */
static void
initfs(Gefs *fs, vlong cachesz, int rdonly)
{
	Bfree *f, *h;
	Blk *b;
	usize i;

	aswapl(&fs->rdonly, rdonly);
	fs->lrurz.l = &fs->lrulk;
	fs->syncrz.l = &fs->synclk;
	fs->bfreerz.l = &fs->bfreelk;
	fs->noauth = 1;
	fs->cmax = cachesz/Blksz;
	if((fs->bcache = mallocz(fs->cmax*sizeof(Bucket), 1)) == nil)
		sysfatal("malloc: %r");

	fs->dlcount = 0;
	fs->dlcmax = fs->cmax/10;
	if(fs->dlcmax < 4)
		fs->dlcmax = 4;
	if(fs->dlcmax > 512)
		fs->dlcmax = 512;
	if((fs->dlcache = mallocz(fs->dlcmax*sizeof(Dlist*), 1)) == nil)
		sysfatal("malloc: %r");

	h = nil;
	for(i = 0; i < fs->cmax; i++){
		if((f = mallocz(sizeof(Bfree), 1)) == nil)
			sysfatal("malloc: %r");
		f->bp = Zb;
		f->limbo.fs = fs;
		f->limbo.next = h ? &h->limbo : nil;
		h = f;
	}
	fs->bfree = h;

	for(i = 0; i < fs->cmax; i++){
		if((b = mallocz(sizeof(Blk), 1)) == nil)
			sysfatal("cache: %lld blocks: %r", fs->cmax);
		b->bp = Zb;
		b->limbo.fs = fs;
		b->magic = Magic;
		lrutop(b);
	}
}

/* main.c's launch: a kproc running f(id, arg) */
typedef struct Gproc Gproc;
struct Gproc {
	Gefs	*fs;		/* the volume the proc serves */
	void	(*f)(Gefs*, int, void*);
	void	*arg;
	int	id;
};

static void
gproc(void *a)
{
	Gproc p;

	p = *(Gproc*)a;
	free(a);
	(*p.f)(p.fs, p.id, p.arg);
	pexit("", 1);
}

void
launch(Gefs *fs, void (*f)(Gefs*, int, void*), void *arg, int id, char *text)
{
	Gproc *p;
	char name[KNAMELEN];

	p = smalloc(sizeof *p);
	p->fs = fs;
	p->f = f;
	p->arg = arg;
	p->id = id;
	if(id >= 0)
		snprint(name, sizeof name, "gefs%d%s.%d", fs->id, text, id);
	else
		snprint(name, sizeof name, "gefs%d%s", fs->id, text);
	kproc(name, gproc, p);
}

/*
 * Plan2001: the cache budget is the kernel's, not a volume's
 * (GE volume instance isolation 3): the instances' caches together must fit in it.
 * The first volume gets the whole budget, as before (plan9.ini's
 * gefscache=, or an eighth of memory); a later one gets what is left,
 * and is refused if that is less than Cachemin, instead of taking the
 * heap past the budget.  A machine that runs several volumes sets
 * gefscache= for all of them.
 */
enum {
	Cachemin	= 16,	/* MiB; also the least gefscache= may say */
};

static struct {
	QLock	qlk;
	int	set;
	int	budget;	/* MiB, all the volumes together */
	int	used;
} cachebud;

static int
cachemb(void)
{
	char *s;
	uvlong mem;
	int mb, budget;

	qlock(&cachebud.qlk);
	if(!cachebud.set){
		budget = 0;
		if((s = getconf("gefscache")) != nil)
			budget = strtol(s, nil, 0);
		if(budget < Cachemin){
			mem = (uvlong)conf.npage*BY2PG;
			budget = mem/8/MiB;
			if(budget < Cachemin)
				budget = Cachemin;
			if(budget > 512)
				budget = 512;
		}
		cachebud.budget = budget;
		cachebud.set = 1;
	}
	mb = cachebud.budget - cachebud.used;
	if(mb < Cachemin){
		qunlock(&cachebud.qlk);
		return -1;
	}
	cachebud.used += mb;
	qunlock(&cachebud.qlk);
	return mb;
}

/*
 * Plan2001: open a GE volume (docs/O-FORMAT.md).  This
 * is the storage engine's context: its device, cache, superblock,
 * arenas, logs and recovery.  It loads no FS mount or user metadata and
 * starts no 9P server.
 */
static void*
geopen0(char *dev, void *oblk, vlong off, vlong size, int rw,
	long (*oblkrw)(void*, void*, long, uvlong, int),
	int (*oblkflush)(void*))
{
	static int nvol;	/* instances made, for their names */
	Gefs *fs;
	char *d;
	int mb;

	/* main.c's sanity checks */
	assert(4*Kpmax < Pivspc);
	assert(2*Msgmax < Bufspc);
	assert(Treesz < Inlmax);

	/*
	 * Plan2001: this call's GE instance (GE volume instance isolation).  Nothing
	 * below is one volume's alone any more: the engine's state is in
	 * fs, and the objects it makes carry it.
	 */
	if((fs = mallocz(sizeof(Gefs), 1)) == nil)
		sysfatal("malloc: %r");
	fs->id = ++nvol;
	fs->snap.limbo.fs = fs;	/* the volume's own trees and dlists */
	fs->snapdl.limbo.fs = fs;
	fs->dropdl.limbo.fs = fs;
	fs->dropdl.hd = Zb;
	fs->dropdl.tl = Zb;
	if((d = getconf("gefsdebug")) != nil)
		debug = strtol(d, nil, 0);
	if((mb = cachemb()) < 0){
		free(fs);
		error("gefs: no cache left for another volume (set gefscache=)");
	}
	initfs(fs, (vlong)mb*MiB, !rw);
	if((d = getconf("gefstrace")) != nil && strtol(d, nil, 0) > 0){
		fs->ntrace = strtol(d, nil, 0)*MiB/sizeof(Trace);
		if((fs->trace = mallocz(fs->ntrace*sizeof(Trace), 1)) == nil)
			fs->ntrace = 0;
	}
	fmtinstall('B', Bconv);
	fmtinstall('N', Mconv);
	fmtinstall('P', Pconv);
	fmtinstall('K', Kconv);
	fmtinstall('R', Rconv);
	fmtinstall('J', Qconv);
	initshow();

	USED(off);
	fs->oblk = oblk;
	fs->oblksize = size;
	fs->oblkrw = oblkrw;
	fs->oblkflush = oblkflush;
	loadge(fs, dev);
	return fs;
}

void*
geopenblk(char *dev, void *oblk, vlong off, vlong size, int rw,
	long (*oblkrw)(void*, void*, long, uvlong, int),
	int (*oblkflush)(void*))
{
	return geopen0(dev, oblk, off, size, rw, oblkrw, oblkflush);
}

/*
 * Plan2001: the native O engine's kprocs (the sweeper, tasks and
 * syncers), without FS's console and 9P server.  Only the O profile
 * gets workers (docs/O-FORMAT.md); there is no FS/9P start path in the
 * live engine.
 */
void
gestart(void *v)
{
	Gefs *fs;
	int i;

	fs = v;
	if(fs == nil || (fs->flag & Ovolflag) == 0)
		error("gestart: not a native O volume");
	fs->osweepchan = mkchan(32);
	fs->nsyncers = 1;
	if(fs->nsyncers > fs->narena)
		fs->nsyncers = fs->narena;
	for(i = 0; i < fs->nsyncers; i++)
		qinit(fs, &fs->syncq[i]);
	for(i = 0; i < fs->narena; i++){
		fs->arenas[i].fs = fs;
		fs->arenas[i].sync = &fs->syncq[i%fs->nsyncers];
	}

	launch(fs, runsweep, nil, aincl(&fs->nworker, 1), "sweep");
	launch(fs, runtasks, nil, -1, "tasks");
	for(i = 0; i < fs->nsyncers; i++)
		launch(fs, runsync, &fs->syncq[i], aincl(&fs->nworker, 1), "sync");
}

/*
 * At reboot (sysgefs's shutdown): halted as the console's halt does
 * it (the sweeper syncs), waited for a while.
 */
void
gefsshutdown(void *v)
{
	Gefs *fs = v;
	Osweepmsg *a;
	int i;

	if(fs == nil || fs->osweepchan == nil || agetl(&fs->rdonly) || fs->k.halting++)
		return;
	if(waserror())
		return;
	a = emalloc(sizeof(Osweepmsg), 1);
	a->op = OsweepHalt;
	chsend(fs->osweepchan, a);
	for(i = 0; i < 100 && !fs->k.halted; i++)
		tsleep(&up->sleep, return0, nil, 100);
	poperror();
	if(!fs->k.halted)
		print("gefs%d: not halted\n", fs->id);
}
