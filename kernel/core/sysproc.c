#include	"u.h"
#include	"tos.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"ureg.h"
#include	"../core/error.h"
#include	"edf.h"
#include	"sys.h"	/* repository-relative for Linux GCC */
#include	"../../runtime/abi/vobj.h"
#include	"vobjint.h"
#ifdef	PLAN2001_LIFECYCLE_DIAG
#include	"env/envapi.h"
#endif

#include	<a.out.h>

/* ge/storage/sysgefs.c: bounded published-object read for spawn/exec. */
extern long osgetobject(int, uvlong, uchar**, long, uvlong*);

#ifdef	PLAN2001_LIFECYCLE_DIAG
/*
 * Opt-in test hook for the lifecycle diagnostic kernel only.  A matching
 * PLAN2001_FAULT environment value names one safe rollback point and is
 * consumed here.  This function and its call sites are absent from the
 * canonical kernel.
 */
static int
injectfault(char *point)
{
	return envfaulttake("PLAN2001_FAULT", point);
}
#endif

/*
 * Shared fork core.  RFORKEX(143) and its empty-plan wrapper use this
 * function, so forkchild, resource copying, rollback and parent/child
 * linkage cannot diverge.  The caller has already validated the Handle or
 * the raw syscall boundary; this function validates only the flag word and
 * resources.  VI is a prepared, pinned Handle plan (or nil); it is not
 * mutated and it is committed only after every non-Handle resource has been
 * copied successfully.
 */
static ulong
kernrforkcore(ulong flag, Vinherit *vi)
{
	ulong pid;
	uvlong forkpages;
	int n, i;
	Proc *p;

	/* Check flags before we commit */
	if((flag & (RFFDG|RFCFDG)) == (RFFDG|RFCFDG))
		error(Ebadarg);
	if((flag & (RFNAMEG|RFCNAMEG)) == (RFNAMEG|RFCNAMEG))
		error(Ebadarg);
	if((flag & (RFENVG|RFCENVG)) == (RFENVG|RFCENVG))
		error(Ebadarg);

	/* Copying an absent resource is invalid; explicit empty-group flags
	 * below still create exactly the resource requested by the caller.
	 * Namespace flags require the old group to preserve its device mask. */
	if(((flag & RFFDG) && up->fdgrp == nil) ||
	   ((flag & RFENVG) && up->egrp == nil) ||
	   ((flag & (RFNAMEG|RFCNAMEG|RFNOMNT)) && up->pgrp == nil))
		error(Ebadarg);

	if((flag&RFPROC) == 0) {
		Ref *ofg;
		Pgrp *opg;
		Rgrp *org;
		Egrp *oeg;

		if(flag & (RFMEM|RFNOWAIT))
			error(Ebadarg);

		ofg = up->fdgrp;
		opg = up->pgrp;
		org = up->rgrp;
		oeg = up->egrp;

		if(waserror()){
			if(up->fdgrp != ofg){
				closefdgrp(up->fdgrp);
				up->fdgrp = ofg;
			}
			if(up->pgrp != opg){
				closepgrp(up->pgrp);
				up->pgrp = opg;
			}
			if(up->rgrp != org){
				closergrp(up->rgrp);
				up->rgrp = org;
			}
			if(up->egrp != oeg){
				closeegrp(up->egrp);
				up->egrp = oeg;
			}
			nexterror();
		}

		/* File descriptors */
		if(flag & (RFFDG|RFCFDG))
			up->fdgrp = newfdgrp();

		/* Process group */
		if(flag & (RFNAMEG|RFCNAMEG)) {
			up->pgrp = newpgrp();
			pgrpcpy(up->pgrp, opg, flag & RFNAMEG);
		}

		/* Rendezvous group */
		if(flag & RFREND)
			up->rgrp = newrgrp();

		/* Environment group */
		if(flag & (RFENVG|RFCENVG)) {
			up->egrp = newegrp();
			if(flag & RFENVG)
				envcpy(up->egrp, oeg);
		}

#ifdef	PLAN2001_LIFECYCLE_DIAG
		/* Safe rollback point: every new resource is allocated, but the
		 * caller's pointers have not been replaced yet. */
		if(injectfault("fork-current-rollback"))
			error("fork: injected rollback failure");
#endif

		if(ofg != up->fdgrp)
			closefdgrp(ofg);
		if(opg != nil && opg != up->pgrp)
			closepgrp(opg);
		if(org != nil && org != up->rgrp)
			closergrp(org);
		if(oeg != nil && oeg != up->egrp)
			closeegrp(oeg);

		poperror();

		if(flag & RFNOMNT)
			pgrpsealnamespace(up->pgrp);

		if(flag & RFNOTEG){
			qlock(&up->debug);
			setnoteid(up, 0);	/* can't error() with 0 argument */
			qunlock(&up->debug);
		}
		return 0;
	}

	/* newproc owns an empty native handle table and no output grant.
	 * Neither is inherited, including when user memory is shared. */
	if((p = newproc()) == nil)
		error("no procs");

	qlock(&up->debug);
	qlock(&p->debug);

	p->scallnr = up->scallnr;
	p->s = up->s;

	p->nnote = 0;
		p->notify = up->notify;
	p->notified = 0;
	p->notepending = 0;
	p->lastnote = nil;

	if((flag & RFNOTEG) == 0)
		p->noteid = up->noteid;

	p->procmode = up->procmode;
	p->privatemem = up->privatemem;
	p->noswap = up->noswap;
	p->hang = up->hang;
	if(up->procctl == Proc_tracesyscall)
		p->procctl = Proc_tracesyscall;
	p->kp = 0;

	/*
	 * Craft a return frame which will cause the child to pop out of
	 * the scheduler in user mode with the return register zero
	 */
	forkchild(p, up->dbgreg);

	kstrdup(&p->text, up->text);
	kstrdup(&p->user, up->user);
	kstrdup(&p->args, "");
	p->nargs = 0;
	p->setargs = 0;

	p->insyscall = 0;
	memset(p->time, 0, sizeof(p->time));
	p->time[TReal] = MACHP(0)->pmach.ticks;
	p->kentry = up->kentry;
	p->pcycles = -p->kentry;

	pid = pidalloc(p);

	qunlock(&p->debug);
	qunlock(&up->debug);

	/* The child is still unpublished; release it synchronously. */
	if(waserror()){
		procdiscard(p);
		nexterror();
	}

	/* A child never increases the inherited per-process ceiling. */
	p->membudget = up->membudget;
	/* Make a new set of memory segments */
	n = flag & RFMEM;
	qlock(&p->seglock);
	if(waserror()){
		qunlock(&p->seglock);
		nexterror();
	}
	for(i = 0; i < NSEG; i++)
		if(up->seg[i] != nil)
			p->seg[i] = dupseg(up->seg, i, n);
	forkpages = 0;
	for(i = 0; i < NSEG; i++)
		if(p->seg[i] != nil) forkpages += p->seg[i]->size;
	if(procmemreplace(p, forkpages) < 0) error(Enovmem);
	qunlock(&p->seglock);
	poperror();

	/* File descriptors */
	if(flag & (RFFDG|RFCFDG))
		p->fdgrp = newfdgrp();
	else {
		p->fdgrp = up->fdgrp;
		if(up->fdgrp != nil)
			incref(up->fdgrp);
	}

	/* Process groups */
	if(flag & (RFNAMEG|RFCNAMEG)) {
		p->pgrp = newpgrp();
		pgrpcpy(p->pgrp, up->pgrp, flag & RFNAMEG);
	}
	else {
		p->pgrp = up->pgrp;
		if(up->pgrp != nil)
			incref(&up->pgrp->ref);
	}

	/* Rendezvous group */
	if(flag & RFREND)
		p->rgrp = newrgrp();
	else {
		p->rgrp = up->rgrp;
		if(up->rgrp != nil)
			incref(&up->rgrp->ref);
	}

	/* Environment group */
	if(flag & (RFENVG|RFCENVG)) {
		p->egrp = newegrp();
		if(flag & RFENVG)
			envcpy(p->egrp, up->egrp);
	}
	else {
		p->egrp = up->egrp;
		if(up->egrp != nil)
			incref(&up->egrp->ref);
	}

	/* Only the validated explicit plan crosses this fork.  An ordinary
	 * An empty plan means the child keeps its empty handle table. */
	if(vi != nil)
		vobjinheritcommit(p, vi);
	procfork(p);
#ifdef	PLAN2001_LIFECYCLE_DIAG
	/* Latest safe fork failure point: procfork() may have allocated the
	 * child's FP save chain, but no parent/child state is published. */
	if(injectfault("fork-child-fpu-abort"))
		error("fork: injected post-procfork failure");
#endif

	poperror();	/* prepublication error region */

	if(flag & RFNOMNT)
		pgrpsealnamespace(p->pgrp);

	if((flag&RFNOWAIT) == 0){
		p->parent = up;
		lock(&up->exl);
		up->nchild++;
		unlock(&up->exl);
	}

	/*
	 *  since the bss/data segments are now shareable,
	 *  any mmu info about this process is now stale
	 *  (i.e. has bad properties) and has to be discarded.
	 */
	flushmmu();

	procpriority(p, up->basepri, up->fixedpri);
	if(up->wired)
		procwired(p, up->affinity);

	ready(p);
	sched();
	return pid;
}

/*
 * RFORKEX: ordinary fork flags plus one bounded, explicit Handle plan.  The
 * complete plan is validated and pinned before a child exists; the child is
 * published only after every resource copy succeeds.  On failure the parent's
 * Handle table and reference counts are unchanged.  Assigned child Handles
 * are copied to CHILDHANDLES when it is non-NULL.
 */
ulong
kernrforkex(ulong flag, Vinhand *list, int n, uvlong *childhandles)
{
	Vinherit vi;
	ulong pid;
	int i;

	if((flag & RFPROC) == 0){
		if(n != 0)
			error(Ebadarg);
		return kernrforkcore(flag, nil);
	}
	memset(&vi, 0, sizeof vi);
	if(waserror()){
		vobjinheritrelease(&vi);
		nexterror();
	}
	vobjinheritprepare(&vi, list, n);
	pid = kernrforkcore(flag, &vi);
	/* One result per input entry, in input order.  Do not search by source:
	 * the same parent Handle may legitimately appear several times. */
	if(childhandles != nil)
		for(i = 0; i < vi.n; i++)
			childhandles[i] =
				(uvlong)vobjinheritchildhandle(&vi, i);
	vobjinheritrelease(&vi);
	poperror();
	return pid;
}

ulong
beswal(ulong l)
{
	uchar *p;

	p = (uchar*)&l;
	return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

uvlong
beswav(uvlong v)
{
	uchar *p;

	p = (uchar*)&v;
	return ((uvlong)p[0]<<56) | ((uvlong)p[1]<<48) | ((uvlong)p[2]<<40)
				  | ((uvlong)p[3]<<32) | ((uvlong)p[4]<<24)
				  | ((uvlong)p[5]<<16) | ((uvlong)p[6]<<8)
				  | (uvlong)p[7];
}

void
execparse(Exec *e, int n, ulong magic, uintptr *entryp, ulong *textp,
	ulong *datap, uintptr *adap, uintptr *abssp, uintptr *ebssp)
{
	uintptr entry, text;
	ulong data, bss, align;
	uvlong a, d, ab, eb, h;

	if(magic != AOUT_MAGIC)
		error(Ebadexec);
	if(magic & HDR_MAGIC){
		if(n < sizeof(Exec)+sizeof(uvlong))
			error(Ebadexec);
		memmove(&h, e+1, sizeof h);
		entry = beswav(h);
		text = UTZERO+sizeof(Exec)+sizeof(uvlong);
	} else {
		entry = beswal(e->entry);
		text = UTZERO+sizeof(Exec);
	}
	if(entry < text)
		error(Ebadexec);
	if((uvlong)text + beswal(e->text) > (uvlong)(USTKTOP-USTKSIZE))
		error(Ebadexec);
	text += beswal(e->text);
	if(text <= entry || text >= (USTKTOP-USTKSIZE))
		error(Ebadexec);

	switch(magic){
	case S_MAGIC:	/* 2MB segment alignment for amd64 */
		align = 0x1fffff;
		break;
	case P_MAGIC:	/* 16K segment alignment for spim */
	case V_MAGIC:	/* 16K segment alignment for mips */
		align = 0x3fff;
		break;
	case R_MAGIC:	/* 64K segment alignment for arm64 */
		align = 0xffff;
		break;
	default:
		align = BY2PG-1;
	}

	a = ((uvlong)text+align) & ~(uvlong)align;
	data = beswal(e->data);
	bss = beswal(e->bss);
	d = a + data;
	ab = (d+(BY2PG-1)) & ~(uvlong)(BY2PG-1);
	eb = (d+bss+(BY2PG-1)) & ~(uvlong)(BY2PG-1);
	if(a >= (uvlong)(USTKTOP-USTKSIZE) || ab < a || eb < ab ||
	   ab >= (uvlong)(USTKTOP-USTKSIZE) || eb >= (uvlong)(USTKTOP-USTKSIZE))
		error(Ebadexec);

	*entryp = entry;
	*textp = text-UTZERO;
	*datap = data;
	*adap = (uintptr)a;
	*abssp = (uintptr)ab;
	*ebssp = (uintptr)eb;
}

void
execcommit(Segment *ts, Segment *ds, Segment *bs, uintptr tstk,
	char *args, int nargs, char *elem)
{
	Segment *s;
	uvlong pages;
	int i;

	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}
	pages = (uvlong)ts->size + ds->size + bs->size + up->seg[ESEG]->size;
	for(i = ESEG+1; i < NSEG; i++)
		if(up->seg[i] != nil && !(up->seg[i]->type & SG_CEXEC))
			pages += up->seg[i]->size;
	if(procmemreplace(up, pages) < 0) error(Enovmem);
	for(i = SSEG; i <= BSEG; i++){
		s = up->seg[i];
		if(s != nil){
			up->seg[i] = nil;
			putseg(s);
		}
	}
	for(i = ESEG+1; i < NSEG; i++){
		s = up->seg[i];
		if(s != nil && (s->type&SG_CEXEC) != 0){
			up->seg[i] = nil;
			putseg(s);
		}
	}

	assert(ts->ref.ref > 0);
	up->seg[TSEG] = ts;
	up->seg[DSEG] = ds;
	up->seg[BSEG] = bs;

	s = up->seg[ESEG];
	up->seg[ESEG] = nil;
	qlock(&s->qlk);
	s->base = USTKTOP-USTKSIZE;
	s->top = USTKTOP;
	relocateseg(s, USTKTOP-tstk);
	qunlock(&s->qlk);
	up->seg[SSEG] = s;
	qunlock(&up->seglock);
	poperror();

	qlock(&up->debug);
	free(up->text);
	up->text = elem;
	free(up->args);
	up->args = args;
	up->nargs = nargs;
	up->setargs = 0;
	freenotes(up);
	freenote(up->lastnote);
	up->lastnote = nil;
	up->notify = nil;
	up->notified = 0;
	up->noteureg = nil;
	up->privatemem = 0;
	up->noswap = 0;
	up->pcycles = -up->kentry;
	procsetup(up);
	qunlock(&up->debug);

	up->errbuf0[0] = '\0';
	up->errbuf1[0] = '\0';
	flushmmu();
	if(up->hang)
		up->procctl = Proc_stopme;
	oprocexec(up);
	vobjprocexec(up);
	pipeprocexec(up);
	espctl_bootstrap(up);
	kbdprocexec(up);
	uartprocexec(up);
}

static void
segload(Segment *s, uchar *src, ulong len, ulong off)
{
	Page *p;
	KMap *k;
	ulong m, so;
	uintptr va;

	for(so = 0; so < len; so += m){
		m = len-so;
		if(m > BY2PG)
			m = BY2PG;
		va = s->base+so;
		p = fillpage(newpage(va, nil), 0);
		k = kmap(p);
		memmove(VA(k), src+off+so, m);
		kunmap(k);
		segpage(s, p);
	}
}

enum {
	Oargmax		= 32,
	Oargbytes	= 16*KiB,	/* all argv strings, including NULs */
	Objspawnmax	= 8*MiB,	/* same bound as OEXECUTE's image */
};

/*
 * Copy one user string into dst, never writing more than cap bytes.
 * Check each page before reading it; scan the bytes actually copied for
 * the NUL, so a racing peer can change the source without making the
 * accepted length exceed the copy.  Returns the copied length including
 * the NUL, or -1 if no NUL was copied within cap.
 */
static long
execargcopy(char *dst, long cap, char *us)
{
	uintptr a;
	ulong m, k;
	long n;
	char *q;

	if(us == nil || (uintptr)us >= KZERO)
		error(Ebadarg);
	n = 0;
	while(n < cap){
		a = (uintptr)us + n;
		if(a < (uintptr)us)	/* address arithmetic overflow */
			error(Ebadarg);
		m = BY2PG - (a & (BY2PG-1));
		k = cap - n;
		if(k > m)
			k = m;
		if(!okaddr(a, k, 0))
			error(Ebadarg);
		memmove(dst+n, (void*)a, k);
		if((q = memchr(dst+n, 0, k)) != nil)
			return (q-dst)+1;
		n += k;
	}
	return -1;
}

/*
 * N9.6: static ELF64 x86-64 user executable loaded by OEXECUTE.
 *
 * The fixed Plan2001-O user layout is deliberately small: exactly two
 * PT_LOAD entries, a read/execute text segment at UTZERO and a
 * read/write data segment above it.  BSS is the p_memsz-p_filesz tail
 * of the data segment: the part in the last data page is demand-zero,
 * and the rest becomes the existing BSEG (which brk_ grows).  No
 * interpreter, dynamic section, relocation, W+X or kernel alias.
 */
typedef struct ElfEhdr ElfEhdr;
typedef struct ElfPhdr ElfPhdr;
struct ElfEhdr
{
	uchar	ident[16];
	u16int	type;
	u16int	machine;
	u32int	version;
	u64int	entry;
	u64int	phoff;
	u64int	shoff;
	u32int	flags;
	u16int	ehsize;
	u16int	phentsize;
	u16int	phnum;
	u16int	shentsize;
	u16int	shnum;
	u16int	shstrndx;
};
struct ElfPhdr
{
	u32int	type;
	u32int	flags;
	u64int	off;
	u64int	vaddr;
	u64int	paddr;
	u64int	filesz;
	u64int	memsz;
	u64int	align;
};

enum
{
	Elfclass64	= 2,
	Elfdata2lsb	= 1,
	Elfversion	= 1,
	EtExec		= 2,
	EmX86_64	= 62,
	PtLoad		= 1,
	PtDynamic	= 2,
	PtInterp	= 3,
	PfX		= 1,
	PfW		= 2,
	PfR		= 4,
};

static int
iself(uchar *buf, long n)
{
	return n >= sizeof(ElfEhdr) && buf[0] == 0x7f && buf[1] == 'E' &&
		buf[2] == 'L' && buf[3] == 'F';
}

/*
 * Parse and validate an ELF64 image.  The image bytes themselves are
 * not modified.  Returns the load parameters consumed by execo().
 */
static void
elfparse(uchar *buf, long n, uintptr *entryp, uintptr *tbasep,
	uintptr *toffp, uintptr *tlenp, uintptr *dbasep, uintptr *doffp,
	uintptr *dlenp, uintptr *bbasep, ulong *bnpagesp)
{
	ElfEhdr *eh;
	ElfPhdr *ph;
	u64int phoff, end, vaend, bbase, bend, tpageend, bnp;
	uintptr tbase, tlen, dbase, dlen, entry;
	ulong bnpages;
	int i, nload, textseen, dataseen;

	if(!iself(buf, n))
		error(Ebadexec);
	eh = (ElfEhdr*)buf;
	if(eh->ident[4] != Elfclass64 || eh->ident[5] != Elfdata2lsb ||
	   eh->ident[6] != Elfversion)
		error(Ebadexec);
	if(eh->type != EtExec || eh->machine != EmX86_64 ||
	   eh->version != Elfversion)
		error(Ebadexec);
	if(eh->ehsize < sizeof(ElfEhdr) || eh->phentsize != sizeof(ElfPhdr) ||
	   eh->phnum == 0 || eh->phnum > 64)
		error(Ebadexec);
	phoff = eh->phoff;
	if(phoff < sizeof(ElfEhdr) || phoff > (u64int)n ||
	   (u64int)eh->phnum > ((u64int)n - phoff)/sizeof(ElfPhdr))
		error(Ebadexec);

	tbase = tlen = dbase = dlen = entry = 0;
	textseen = dataseen = nload = 0;
	tpageend = 0;
	for(i = 0; i < eh->phnum; i++){
		ph = (ElfPhdr*)(buf + phoff + i*sizeof(ElfPhdr));
		if(ph->type == PtInterp || ph->type == PtDynamic)
			error(Ebadexec);
		if(ph->type != PtLoad)
			continue;
		nload++;
		if(ph->filesz > ph->memsz)
			error(Ebadexec);
		if(ph->off > (u64int)n || ph->filesz > (u64int)n - ph->off)
			error(Ebadexec);
		if(ph->vaddr < UTZERO || ph->vaddr >= USTKTOP-USTKSIZE ||
		   ph->memsz > (USTKTOP-USTKSIZE) - ph->vaddr)
			error(Ebadexec);
		if(ph->align != BY2PG || (ph->vaddr & (BY2PG-1)) !=
		   (ph->off & (BY2PG-1)))
			error(Ebadexec);
		vaend = ph->vaddr + ph->memsz;
		if(vaend < ph->vaddr)
			error(Ebadexec);
		if((ph->flags & (PfR|PfW|PfX)) == (PfR|PfW|PfX) ||
		   (ph->flags & (PfR|PfW|PfX)) == 0)
			error(Ebadexec);
		if(ph->flags & PfX){
			if(textseen || (ph->flags & PfW) || tbase != 0)
				error(Ebadexec);
			tbase = ph->vaddr;
			tlen = ph->filesz;
			if(tlen == 0 || tbase != UTZERO ||
			   eh->entry < tbase || eh->entry >= tbase+tlen)
				error(Ebadexec);
			*toffp = (uintptr)ph->off;
			tpageend = PGROUND(tbase+tlen);
			textseen = 1;
		}else if(ph->flags & PfW){
			if(dataseen || (ph->flags & PfX))
				error(Ebadexec);
			/* Segment page maps are indexed from a page-aligned base.
			 * ELF's weaker offset/address congruence alone is insufficient:
			 * fault() rounds the address down before subtracting this base.
			 */
			if(ph->vaddr & (BY2PG-1))
				error(Ebadexec);
			dbase = ph->vaddr;
			dlen = ph->filesz;
			*doffp = (uintptr)ph->off;
			bbase = PGROUND(dbase+dlen);
			bend = PGROUND(dbase+ph->memsz);
			dataseen = 1;
		}else
			error(Ebadexec);
		if(nload > 2)
			error(Ebadexec);
	}
	if(nload != 2 || !textseen || !dataseen)
		error(Ebadexec);
	if(dbase < tpageend)
		error(Ebadexec);
	if(bend < bbase || bend > USTKTOP-USTKSIZE)
		error(Ebadexec);
	bnp = (bend - bbase) >> PGSHIFT;
	if(bnp > (u64int)(ulong)~0)
		error(Ebadexec);	/* would not fit the page accounting */
	bnpages = (ulong)bnp;
	end = eh->entry;
	if(end == 0)
		error(Ebadexec);
	entry = (uintptr)end;

	*entryp = entry;
	*tbasep = tbase;
	*tlenp = tlen;
	*dbasep = dbase;
	*dlenp = dlen;
	*bbasep = (uintptr)bbase;
	*bnpagesp = bnpages;
}

uintptr
execo(uvlong oid, void *vbuf, long n, char **uargv, int argc)
{
	uchar *buf;
	Exec *e;
	Segment *ts, *ds, *bs, *stk, *s;
	ulong magic, text, data, ssize, nbytes, total;
	uintptr abss, ebss;
	uintptr entry, adata, tstk;
	uintptr tbase, toff, tlen, dbase, doff, dlen, bbase;
	ulong bnpages;
	int iselfimage;
	char *elem, *args, name[64], *charp, *p;
	char **argv, **kargv, *kargs;
	char *uarg[Oargmax];
	Tos *tos;
	int i, argn, nargs, nlen;
	long cap, len;

	buf = vbuf;
	ts = ds = bs = stk = nil;
	elem = args = nil;
	kargv = nil;
	kargs = nil;
	if(waserror()){
		free(buf);
		if(ts != nil)
			putseg(ts);
		if(ds != nil)
			putseg(ds);
		if(bs != nil)
			putseg(bs);
		if(args != nil)
			free(args);
		if(elem != nil)
			free(elem);
		free(kargv);
		free(kargs);
		if(stk != nil){
			qlock(&up->seglock);
			if(up->seg[ESEG] == stk){
				up->seg[ESEG] = nil;
				putseg(stk);
			}
			qunlock(&up->seglock);
		}
		if(up->seg[SSEG] == nil)
			pexit(up->errstr, 1);
		nexterror();
	}

	if(argc < 0 || argc > Oargmax)
		error(Ebadarg);
	if(argc > 0){
		if(uargv == nil)
			error(Ebadarg);
		if((uintptr)uargv & (BY2WD-1))
			error(Ebadarg);
		if(!okaddr((uintptr)uargv, argc*BY2WD, 0))
			error(Ebadarg);
		/*
		 * Snapshot the pointer array once.  A peer may still change
		 * the strings, but no later code reads the live array.
		 */
		memmove(uarg, uargv, argc*BY2WD);
		kargs = malloc(Oargbytes);
		if(kargs == nil)
			error(Enomem);
		kargv = malloc((argc+1)*sizeof(char*));
		if(kargv == nil)
			error(Enomem);
		p = kargs;
		total = 0;
		for(i = 0; i < argc; i++){
			cap = Oargbytes - total;
			if(cap <= 0)
				error(Enovmem);
			len = execargcopy(p, cap, uarg[i]);
			if(len < 0)
				error(Ebadarg);
			kargv[i] = p;
			p += len;
			total += len;
		}
		kargv[argc] = nil;
		argn = argc;
	}else{
		snprint(name, sizeof name, "oexec %016llux", oid);
		kstrdup(&kargs, name);
		kargv = malloc(2*sizeof(char*));
		if(kargv == nil)
			error(Enomem);
		kargv[0] = kargs;
		kargv[1] = nil;
		argn = 1;
	}

	iselfimage = iself(buf, n);
	if(iselfimage){
		elfparse(buf, n, &entry, &tbase, &toff, &tlen, &dbase,
			&doff, &dlen, &bbase, &bnpages);
	}else{
		if(n < sizeof(Exec))
			error(Ebadexec);
		e = (Exec*)buf;
		magic = beswal(e->magic);
		execparse(e, n, magic, &entry, &text, &data, &adata, &abss, &ebss);
		if((uvlong)n < (uvlong)text+data)
			error(Ebadexec);
		tbase = UTZERO;
		toff = 0;
		tlen = text;
		dbase = adata;
		doff = text;
		dlen = data;
		bbase = abss;
		bnpages = (ebss-abss)>>PGSHIFT;
	}

	snprint(name, sizeof name, "%.63s", kargv[0]);
	kstrdup(&elem, name);

	ts = newseg(SG_TEXT|SG_RONLY|SG_NOSWAP, tbase, PGROUND(tlen)>>PGSHIFT);
	ts->flushme = 1;
	segload(ts, buf, tlen, toff);
	ds = newseg(SG_DATA|SG_NOSWAP, dbase, PGROUND(dlen)>>PGSHIFT);
	segload(ds, buf, dlen, doff);
	bs = newseg(SG_BSS|SG_NOSWAP, bbase, bnpages);
	free(buf);
	buf = nil;

	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}
	s = up->seg[SSEG];
	do {
		tstk = s->base;
		if(tstk <= USTKSIZE)
			error(Enovmem);
	} while((s = isoverlap(tstk-USTKSIZE, USTKSIZE)) != nil);
	stk = newseg(SG_STACK|SG_NOEXEC|SG_NOSWAP, tstk-USTKSIZE, USTKSIZE/BY2PG);
	up->seg[ESEG] = stk;
	qunlock(&up->seglock);
	poperror();

	tos = (Tos*)(tstk-sizeof(Tos));
	memset(tos, 0, sizeof(Tos));
	tos->cyclefreq = m->pmach.cyclefreq;

	total = 0;
	for(i = 0; i < argn; i++)
		total += strlen(kargv[i])+1;
	charp = (char*)tos - total;
	if(charp < (char*)tstk-USTKSIZE)
		error(Enovmem);
	nbytes = (char*)tstk - charp;
	ssize = BY2WD*(argn+1) + ((nbytes+(BY2WD-1)) & ~(BY2WD-1));
	if(PGROUND(ssize+BY2WD) >= USTKSIZE)
		error(Enovmem);
	argv = (char**)(tstk-ssize);
	p = charp;
	for(i = 0; i < argn; i++){
		nlen = strlen(kargv[i])+1;
		memmove(p, kargv[i], nlen);
		argv[i] = p+(USTKTOP-tstk);
		p += nlen;
	}
	argv[argn] = nil;

	args = malloc(total < 128 ? total : 128);
	if(args == nil)
		error(Enomem);
	nargs = total < 128 ? total : 128;
	memmove(args, charp, nargs);
	if(nargs > 0 && args[nargs-1] != '\0'){
		for(i = nargs-1; i > 0; --i)
			if(fullrune(args+i, nargs-i))
				break;
		args[i] = '\0';
		nargs = i+1;
	}

	free(kargv);
	free(kargs);
	kargv = nil;
	kargs = nil;

	execcommit(ts, ds, bs, tstk, args, nargs, elem);
	poperror();

	tos = (Tos*)(USTKTOP-sizeof(Tos));
	argv = (char**)(USTKTOP-ssize);
	return execregs(entry, argn, argv, tos);
}

/*
 * Materialize one already-zeroed stack page at VA into segment S.  The
 * child is not the current process, so its user addresses cannot be
 * touched directly as execo() does for an exec-in-place.
 */
static void
spawnstackpage(Segment *s, uintptr va, void *data, ulong n)
{
	KMap *k;
	Page *p;

	p = newpage(va, nil);
	k = kmap(p);
	memset(VA(k), 0, BY2PG);
	if(data != nil && n > 0)
		memmove(VA(k), data, n > BY2PG ? BY2PG : n);
	kunmap(k);
	segpage(s, p);
}

/*
 * Shared spawn: create a real child from a published static x86-64 ELF.
 * The caller's Space handle is used only to read the image. The child
 * gets fresh segments and Egrp plus only the explicitly selected O view,
 * Pipe handles, environment and UDP6 permit. CPU placement is applied
 * before ready(). OTERMINATE may only interrupt a direct child.
 */
/*
 * Shared spawn core.  The explicit GE/O Space capability is supplied by the
 * caller: SPAWN(146) and OEXECUTE both resolve the pinned Ohand from a
 * VkindSpace Handle before reaching this core.  Everything else (ELF
 * validation, argv, Ospawn/Oinherit layout, budgets, CPU pinning and child
 * setup) is unchanged.
 */
long
kernospawn(Ohand *soh, uvlong oid, char **uargv, int argc, struct Ospawn *uopt)
{
	struct Ospawn opt;
	Proc *p;
	Ureg *cur;
	Segment *ts, *ds, *bs, *stk;
	Tos *tosk;
	uchar *buf, *stackbuf;
	uvlong ver, req, budget, totalpages;
	ulong bnpages, pid, pages, ssize, nbytes, total, stacklen;
	ulong tpages, dpages, off, chunk;
	uintptr entry, tbase, toff, tlen, dbase, doff, dlen, bbase;
	uintptr sp, argvva, tosva, charp, stacklo;
	char *kargs;
	char argbuf[128];
	char **kargv, **av;
	char *uarg[Oargmax];
	char name[64], ebuf[ERRMAX];
	int argn, cpu, i, nargs;
	struct Oinherit *inh;
	struct Oinheritv *inhv;
	Vinhand plan[24];
	Vinherit vi;
	int nplan;
	int lockup, lockp;
	long n, cap, len;

	if(soh == nil)
		error(Ebadarg);
	memset(&vi, 0, sizeof vi);
	nplan = 0;
	inh = nil;
	inhv = nil;
	buf = nil;
	stackbuf = nil;
	kargv = nil;
	kargs = nil;
	ts = ds = bs = stk = nil;
	p = nil;
	pid = 0;
	lockup = lockp = 0;

	if(waserror()){
		if(lockp)
			qunlock(&p->debug);
		if(lockup)
			qunlock(&up->debug);
		procdiscard(p);
		vobjinheritrelease(&vi);
		free(buf);
		free(inh);
		free(inhv);
		free(stackbuf);
		free(kargv);
		free(kargs);
		if(ts != nil)
			putseg(ts);
		if(ds != nil)
			putseg(ds);
		if(bs != nil)
			putseg(bs);
		if(stk != nil)
			putseg(stk);
		nexterror();
	}

	if(argc < 0 || argc > Oargmax)
		error(Ebadarg);
	if(argc > 0){
		if(uargv == nil)
			error(Ebadarg);
		if((uintptr)uargv & (BY2WD-1))
			error(Ebadarg);
		if(!okaddr((uintptr)uargv, argc*BY2WD, 0))
			error(Ebadarg);
		/* Snapshot the array once; the strings are copied below. */
		memmove(uarg, uargv, argc*BY2WD);
		kargs = malloc(Oargbytes);
		if(kargs == nil)
			error(Enomem);
		kargv = malloc((argc+1)*sizeof(char*));
		if(kargv == nil)
			error(Enomem);
		off = 0;
		for(i = 0; i < argc; i++){
			cap = Oargbytes - off;
			if(cap <= 0)
				error(Enovmem);
			len = execargcopy(kargs+off, cap, uarg[i]);
			if(len < 0)
				error(Ebadarg);
			kargv[i] = kargs+off;
			off += len;
		}
		kargv[argc] = nil;
		argn = argc;
		total = off;
	}else{
		snprint(ebuf, sizeof ebuf, "ospawn %016llux", oid);
		kstrdup(&kargs, ebuf);
		kargv = malloc(2*sizeof(char*));
		if(kargv == nil)
			error(Enomem);
		kargv[0] = kargs;
		kargv[1] = nil;
		argn = 1;
		total = strlen(kargs)+1;
	}

	if(uopt == nil){
		memset(&opt, 0, sizeof opt);
		opt.cpu = ~0ULL;
	}else{
		if((uintptr)uopt & (BY2WD-1))
			error(Ebadarg);
		validaddr((uintptr)uopt, sizeof opt, 0);
		memmove(&opt, uopt, sizeof opt);
	}
	if(opt.flags & ~3ULL) error(Ebadarg);
	if(opt.flags & 1){
		inh = malloc(sizeof *inh);
		if(inh == nil) error(Enomem);
		validaddr((uintptr)(uopt+1), sizeof *inh, 0);
		memmove(inh, uopt+1, sizeof *inh);
		if(inh->view > 2 || inh->nroles > 32 || inh->npipes > 16 || inh->nenv > 8)
			error(Ebadarg);
		if(inh->view == 0 && inh->nroles != 0) error(Ebadarg);
	}
	if(opt.flags & 2){
		uchar *base = (uchar*)(uopt+1);
		if(opt.flags & 1)
			base += sizeof *inh;
		inhv = malloc(sizeof *inhv);
		if(inhv == nil) error(Enomem);
		validaddr((uintptr)base, sizeof *inhv, 0);
		memmove(inhv, base, sizeof *inhv);
		if(inhv->nhandles > 8) error(Ebadarg);
		for(i = 0; i < (int)inhv->nhandles; i++)
			if(inhv->h[i].rights & ~(uvlong)Vall) error(Ebadarg);
	}
	/* Translate both spawn delegation lists into the one shared plan.
	 * vhandles are explicit Handle/rights pairs; the legacy Pipe list is an
	 * explicit Handle/read-write pair with the same attenuation rules. */
	if(inhv != nil){
		for(i = 0; i < (int)inhv->nhandles; i++){
			if(nplan >= (int)(sizeof plan/sizeof plan[0]) ||
			   inhv->h[i].handle > 0x7fffffffULL)
				error(Ebadarg);
			plan[nplan].handle = (int)inhv->h[i].handle;
			plan[nplan].rights = inhv->h[i].rights;
			plan[nplan].dstslot = 0;
			nplan++;
		}
	}
	if(inh != nil){
		for(i = 0; i < (int)inh->npipes; i++){
			int r = inh->pipes[i][1];

			if(nplan >= (int)(sizeof plan/sizeof plan[0]) ||
			   inh->pipes[i][0] <= 0 || r <= 0 || (r & ~3))
				error(Ebadarg);
			plan[nplan].handle = inh->pipes[i][0];
			plan[nplan].rights = (uvlong)(((r & 1) ? Vread : 0) |
				((r & 2) ? Vwrite : 0));
			plan[nplan].dstslot = 0;
			nplan++;
		}
	}
	vobjinheritprepare(&vi, plan, nplan);
	cpu = -1;
	if(opt.cpu != ~0ULL){
		if(opt.cpu > 0x7fffffffULL)
			error(Ebadarg);
		cpu = (int)opt.cpu;
		if(!proccpuonline(cpu)){
			snprint(ebuf, sizeof ebuf, "ospawn: cpu %d is not online", cpu);
			error(ebuf);
		}
	}

	n = osgetobjecthand(soh, oid, &buf, Objspawnmax, &ver);
	if(n < 0)
		error("oexec: no such object");
	if(n > Objspawnmax)
		error(Ebadexec);
	if(!iself(buf, n))
		error("ospawn: not a static x86-64 ELF");
	elfparse(buf, n, &entry, &tbase, &toff, &tlen, &dbase,
		&doff, &dlen, &bbase, &bnpages);

	tpages = PGROUND(tlen) >> PGSHIFT;
	dpages = PGROUND(dlen) >> PGSHIFT;
	totalpages = (uvlong)tpages + (uvlong)dpages +
		(uvlong)bnpages + (uvlong)(USTKSIZE/BY2PG);
	if(totalpages > (uvlong)~(ulong)0)
		error(Enovmem);
	pages = (ulong)totalpages;

	req = opt.membudget;
	if(req != 0){
		if(req > ~0ULL - (BY2PG-1))
			error(Enovmem);
		req = (req + (BY2PG-1)) >> PGSHIFT;
		if(req > (uvlong)~(ulong)0)
			error(Enovmem);
		if(up->membudget != 0 && req > up->membudget){
			snprint(ebuf, sizeof ebuf,
				"ospawn: memory budget exceeds parent limit");
			error(ebuf);
		}
		budget = req;
	}else
		budget = up->membudget;	/* 0 = unlimited */
	if(budget != 0 && totalpages > budget){
		snprint(ebuf, sizeof ebuf,
			"ospawn: static segments exceed memory budget");
		error(ebuf);
	}

	if((p = newproc()) == nil)
		error("no procs");

	qlock(&up->debug);
	lockup = 1;
	qlock(&p->debug);
	lockp = 1;
	p->nnote = 0;
	p->notify = nil;
	p->notified = 0;
	p->notepending = 0;
	p->lastnote = nil;
	p->kp = 0;
	p->procmode = up->procmode;
	p->privatemem = 0;
	p->noswap = 0;
	p->hang = 0;
	forkchild(p, up->dbgreg);

	snprint(name, sizeof name, "%.63s", kargv[0]);
	kstrdup(&p->text, name);
	kstrdup(&p->user, up->user);
	nargs = total < 128 ? total : 128;
	if(nargs > 0){
		memmove(argbuf, kargs, nargs);
		if(argbuf[nargs-1] != '\0'){
			for(i = nargs-1; i > 0; --i)
				if(fullrune(argbuf+i, nargs-i))
					break;
			argbuf[i] = '\0';
			nargs = i+1;
		}
	}else
		argbuf[0] = '\0';
	kstrdup(&p->args, argbuf);
	p->nargs = nargs;
	p->setargs = 0;
	p->insyscall = 0;
	memset(p->time, 0, sizeof(p->time));
	p->time[TReal] = MACHP(0)->pmach.ticks;
	p->kentry = up->kentry;
	p->pcycles = -p->kentry;
	pid = pidalloc(p);
	qunlock(&p->debug);
	lockp = 0;
	qunlock(&up->debug);
	lockup = 0;

	/* Fresh, empty environment group; no inherited namespace or handles. */
	p->fdgrp = nil;
	p->pgrp = nil;
	p->rgrp = nil;
	p->egrp = newegrp();
	/* Empty by default; selected initial values can be set by the child. */
	p->vobjhandles = nil;
	p->iomask = 0;
	p->pgrp = newpgrp();
	/* One atomic, already validated Handle delegation.  A failed commit
	 * aborts the child; the parent's Handle state is untouched. */
	vobjinheritcommit(p, &vi);
	if(inh != nil){
		ocontextspawn(p, inh->view, inh->roles, inh->nroles);
		if(inh->stdoutpipe){
			int ch;

			if(inh->stdoutpipe > 0x7fffffffULL) error(Ebadarg);
			/* stdoutpipe names one parent Pipe.  It must resolve to
			 * exactly one plan entry: absent (0) and ambiguous (-1)
			 * are both rejected rather than picking the first. */
			ch = vobjinheritsrchandle(&vi, (int)inh->stdoutpipe);
			if(ch <= 0) error(Ebadarg);
			pipeoutputset(p, ch);
		}
		for(i = 0; i < inh->nenv; i++){
			if(memchr(inh->env[i].name, 0, 32) == nil || inh->env[i].name[0] == 0 ||
						memchr(inh->env[i].value, 0, 128) == nil) error(Ebadarg);
			envsetval(p->egrp, inh->env[i].name, inh->env[i].value);
		}
	}
	vobjinheritrelease(&vi);
	free(inhv);
	inhv = nil;
	free(inh);
	inh = nil;
	procsetup(p);

	lock(&p->memlock);
	p->membudget = budget;
	unlock(&p->memlock);
	if(procmemcommit(p, pages) < 0){
		snprint(ebuf, sizeof ebuf,
			"ospawn: memory budget %llud pages cannot hold %lud pages",
			budget, pages);
		error(ebuf);
	}

	ts = newseg(SG_TEXT|SG_RONLY|SG_NOSWAP, tbase, PGROUND(tlen)>>PGSHIFT);
	ts->flushme = 1;
	segload(ts, buf, tlen, toff);
	p->seg[TSEG] = ts;
	ts = nil;

	ds = newseg(SG_DATA|SG_NOSWAP, dbase, PGROUND(dlen)>>PGSHIFT);
	segload(ds, buf, dlen, doff);
	p->seg[DSEG] = ds;
	ds = nil;

	bs = newseg(SG_BSS|SG_NOSWAP, bbase, bnpages);
	p->seg[BSEG] = bs;
	bs = nil;
	free(buf);
	buf = nil;

	/* Same user-stack layout as execo(), materialized for the child. */
	tosva = USTKTOP - sizeof(Tos);
	charp = tosva - total;
	if(charp < USTKTOP-USTKSIZE)
		error(Enovmem);
	nbytes = USTKTOP - charp;
	ssize = BY2WD*(argn+1) + ((nbytes+(BY2WD-1)) & ~(BY2WD-1));
	if(PGROUND(ssize+BY2WD) >= USTKSIZE)
		error(Enovmem);
	argvva = USTKTOP - ssize;
	sp = argvva - BY2WD;
	stacklo = sp & ~(BY2PG-1);
	stacklen = USTKTOP - stacklo;
	if(stacklen > USTKSIZE)
		error(Enovmem);
	stackbuf = malloc(stacklen);
	if(stackbuf == nil)
		error(Enomem);
	memset(stackbuf, 0, stacklen);
	*(uintptr*)(stackbuf + (sp - stacklo)) = argn;
	av = (char**)(stackbuf + (argvva - stacklo));
	off = 0;
	for(i = 0; i < argn; i++){
		len = strlen(kargv[i])+1;
		memmove(stackbuf + (charp - stacklo) + off, kargv[i], len);
		av[i] = (char*)(charp + off);
		off += len;
	}
	av[argn] = nil;
	tosk = (Tos*)(stackbuf + (tosva - stacklo));
	memset(tosk, 0, sizeof(Tos));
	tosk->cyclefreq = m->pmach.cyclefreq;

	stk = newseg(SG_STACK|SG_NOEXEC|SG_NOSWAP,
		USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	for(off = 0; off < stacklen; off += chunk){
		chunk = stacklen - off;
		if(chunk > BY2PG)
			chunk = BY2PG;
		spawnstackpage(stk, stacklo+off, stackbuf+off, chunk);
	}
	free(stackbuf);
	stackbuf = nil;
	free(kargv);
	kargv = nil;
	free(kargs);
	kargs = nil;
	p->seg[SSEG] = stk;
	stk = nil;

	/* The copied Ureg becomes the child's first user frame. */
	cur = (Ureg*)(p->sched.sp + 2*BY2WD);
	cur->pc = entry;
	cur->sp = sp;
	cur->cs = UESEL;
	cur->ss = UDSEL;
	cur->flags = 0x200;
	cur->ax = tosva;
	cur->r14 = 0;
	cur->r15 = 0;

	if(cpu >= 0)
		procwiredexact(p, cpu);
	procpriority(p, up->basepri, up->fixedpri);
	procfirstlog(p, cpu);

#ifdef	PLAN2001_LIFECYCLE_DIAG
	/* Latest safe prepublication point: the child is fully built,
	 * including Handle-plan commit and Pgrp COPY/SHARE, but parent/child
	 * accounting has not been committed and it is not schedulable. */
	if(injectfault("spawn-child-late-abort"))
		error("ospawn: injected late child abort");
#endif

	/* Publication boundary: from here the child is parent-visible and
	 * must not be rolled back as if it never existed. */
	lock(&up->exl);
	p->parent = up;
	up->nchild++;
	unlock(&up->exl);

	print("o: SPAWN pid=%lud oid=%#llux cpu=%d\n", pid, oid, cpu);
	poperror();

	ready(p);
	sched();
	return pid;
}

/*
 * Shared OTERMINATE core.  Only a direct, live child can be named; the
 * caller's own ProcessSelf capability has already been checked by the OBJ
 * provider.  PID reuse, parent/parentpid, kp and exiting are all judged
 * exactly as before.
 */
int
kernoterminate(int ipid)
{
	Proc *p;
	char *err;
	ulong pid;
	int idx;

	if(ipid <= 0)
		error(Ebadarg);
	pid = (ulong)ipid;
	if(pid == up->pid)
		error(Ebadarg);
	if((idx = procindex(pid)) < 0)
		error(Ebadarg);
	if((p = proctab(idx)) == nil)
		error(Ebadarg);

	err = nil;
	qlock(&p->debug);
	if(p->pid != pid || p->exiting)
		err = Ebadarg;
	else if(p->kp || p->parent != up || p->parentpid != up->pid)
		err = Eperm;
	else
		postnote(p, 0, "terminated", NDebug);
	qunlock(&p->debug);
	if(err != nil)
		error(err);
	return 0;
}

int
return0(void*)
{
	return 0;
}

/*
 * The single sleep/yield implementation shared by SLEEP(17) and
 * OBJINVOKE(ObjOpSleep).  Positive milliseconds wait, ms<=0 yields with the
 * same EDF preference as before; the semantics are unchanged.
 */
void
kernelsleep(long ms)
{
	if(ms <= 0) {
		if (up->edf != nil && (up->edf->flags & Admitted))
			edfyield();
		else
			yield();
	} else {
		tsleep(&up->sleep, return0, 0, ms);
	}
}

uintptr
syssleep(sysva_list list)
{
	kernelsleep(sysva_arg(list, long));
	return 0;
}

uintptr
sysalarm(sysva_list list)
{
	return procalarm(sysva_arg(list, ulong));
}


/*
 * Direct NOTED(29).  The successful return must use the note-return path in
 * dosyscall(), which restores the saved Ureg instead of writing a normal
 * syscall result.  NOTED_RET is a private marker between this handler and
 * dosyscall(), not a user-visible value.
 */
#define NOTED_RET ((uintptr)-2)

uintptr
sysnotify(sysva_list list)
{
	setnotify((int (*)(void*, char*))(uintptr)sysva_arg(list, uintptr));
	return 0;
}

uintptr
sysnoted(sysva_list list)
{
	notecont((int)sysva_arg(list, int));
	return NOTED_RET;
}

/*
 * RFORKEX(143): the one canonical direct fork boundary, with explicit and
 * validated object-Handle delegation.  Only the declared entries cross; the
 * caller learns each assigned child Handle from the optional childhandles
 * array.  The parent's table is never modified.  A NULL options pointer is
 * the empty plan (count 0); it is not an error.
 */
uintptr
sysrforkex(sysva_list list)
{
	struct Rforkex opt;
	struct Rforkex *uopt;
	uvlong *uch;
	Vinhand list_[RFORKEX_MAXHANDLES];
	ulong flag;
	ulong pid;
	int i;

	flag = (ulong)sysva_arg(list, uvlong);
	uopt = sysva_arg(list, struct Rforkex*);
	uch = sysva_arg(list, uvlong*);
	memset(&opt, 0, sizeof opt);
	if(uopt != nil){
		validaddr((uintptr)uopt, 2*sizeof(unsigned int), 0);
		memmove(&opt, uopt, 2*sizeof(unsigned int));
		if(opt.version != RFORKEX_VERSION ||
		   opt.count > RFORKEX_MAXHANDLES)
			error(Ebadarg);
		if(opt.count > 0){
			validaddr((uintptr)&uopt->h[0],
				opt.count*sizeof opt.h[0], 0);
			memmove(opt.h, &uopt->h[0],
				opt.count*sizeof opt.h[0]);
		}
	}
	if(uch != nil && opt.count > 0)
		validaddr((uintptr)uch, opt.count*sizeof *uch, 1);
	for(i = 0; i < (int)opt.count; i++){
		if(opt.h[i].handle > 0x7fffffffULL ||
		   (opt.h[i].rights & ~(uvlong)Vall) != 0 ||
		   opt.h[i].dstslot > Vhandmax)
			error(Ebadarg);
		list_[i].handle = (int)opt.h[i].handle;
		list_[i].rights = opt.h[i].rights;
		list_[i].dstslot = (int)opt.h[i].dstslot;
	}
	pid = kernrforkex(flag, list_, (int)opt.count, uch);
	return pid;
}

/*
 * SPAWN(146): direct child creation from an explicit authorized Space
 * Handle and OID.  It shares the exact spawn core (and therefore the
 * Oexecute gate) with OEXECUTE and the retired OSPAWN options layout.
 */
uintptr
sysspawn(sysva_list list)
{
	Ohand *soh;
	char **argv;
	struct Ospawn *opt;
	uvlong oid;
	long pid;
	int space, argc;

	space = (int)sysva_arg(list, int);
	oid = sysva_arg(list, uvlong);
	argv = sysva_arg(list, char**);
	argc = (int)sysva_arg(list, int);
	opt = sysva_arg(list, struct Ospawn*);
	soh = vobjholdohand(space, VkindSpace, Vexecute);
	if(waserror()){
		oshandput(soh);
		nexterror();
	}
	pid = kernospawn(soh, oid, argv, argc, opt);
	poperror();
	oshandput(soh);
	return (uintptr)pid;
}

uintptr
sysoterminate(sysva_list list)
{
	return kernoterminate((int)sysva_arg(list, int));
}

uintptr
sysexits(sysva_list list)
{
	char *status;
	char *inval = "invalid exit string";
	char buf[ERRMAX];

	status = sysva_arg(list, char*);
	if(status != nil){
		if(waserror())
			status = inval;
		else{
			validaddr((uintptr)status, 1, 0);
			if(vmemchr(status, 0, ERRMAX) == nil){
				memmove(buf, status, ERRMAX);
				buf[ERRMAX-1] = 0;
				status = buf;
			}
			poperror();
		}

	}
	pexit(status, 1);
}

void
werrstr(char *fmt, ...)
{
	va_list va;

	if(up == nil)
		return;

	va_start(va, fmt);
	vseprint(up->syserrstr, up->syserrstr+ERRMAX, fmt, va);
	va_end(va);
}

/*
 * libc's errfmt()/rerrstr() userspace syscall is not available in the
 * kernel.  Provide the same formatting source from the current process.
 */
void
rerrstr(char *buf, uint nbuf)
{
	if(buf == nil || nbuf == 0)
		return;
	if(up == nil || up->errstr == nil){
		buf[0] = 0;
		return;
	}
	strecpy(buf, buf+nbuf, up->errstr);
}

int
generrstr(char *buf, uint nbuf)
{
	char *err;

	if(nbuf == 0)
		error(Ebadarg);
	if(nbuf > ERRMAX)
		nbuf = ERRMAX;
	validaddr((uintptr)buf, nbuf, 1);

	err = up->errstr;
	utfecpy(err, err+nbuf, buf);
	utfecpy(buf, buf+nbuf, up->syserrstr);

	up->errstr = up->syserrstr;
	up->syserrstr = err;

	return 0;
}

uintptr
syserrstr(sysva_list list)
{
	char *buf;
	uint len;

	buf = sysva_arg(list, char*);
	len = sysva_arg(list, uint);
	return (uintptr)generrstr(buf, len);
}

/* NOTIFY(28): nil removes the callback; otherwise validate its user address.
 * Note queue, Ureg and note return state are unaffected. */
void
setnotify(int (*f)(void*, char*))
{
	if(f != nil)
		validaddr((uintptr)f, sizeof(void*), 0);
	up->notify = f;
}

int
donotify(Ureg *ureg)
{
	Ureg *nureg;
	char *msg;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;

	spllo();
	qlock(&up->debug);
	msg = popnote(ureg);
	if(msg == nil){
		qunlock(&up->debug);
		splhi();
		return 0;
	}
	splhi();
	fpunotify(up);
	spllo();
	qunlock(&up->debug);

	if(up->notify == nil
	|| (nureg = notify(ureg, msg)) == nil){
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", msg);
		pexit(msg, up->lastnote->flag!=NDebug);
	}

	/* word under Ureg is old ureg */
	*(Ureg**)((uintptr)nureg-BY2WD) = up->noteureg;
	up->noteureg = nureg;

	splhi();
	return 1;
}

/* The note-return core used by NOTED(29) and
 * OBJINVOKE(ObjOpNoted).  It is the historical sysnoted() body unchanged:
 * the same up->notified check, noteureg chain, validation and the exact
 * NCONT/NDFLT/NSAVE/NRSTR switch.  On NDFLT it exits; on a bad state it
 * raises Ebadarg.  Only a successful call returns. */
void
notecont(int arg)
{
	Ureg *nureg;

	qlock(&up->debug);
	if(up->notified){
		splhi();
		fpunoted(up);
		spllo();
	} else if(arg!=NRSTR){
		qunlock(&up->debug);
		error(Ebadarg);
	}
	qunlock(&up->debug);

	nureg = up->noteureg;
	if(!okaddr((uintptr)nureg-BY2WD, BY2WD+sizeof(Ureg), 0)){
		pprint("suicide: bad ureg in noted or call to noted when not notified\n");
		pexit("Suicide", 0);
	}

	switch(arg){
	case NCONT:
	case NRSTR:
		/* word under Ureg is old ureg */
		up->noteureg = *(Ureg**)((uintptr)nureg-BY2WD);
		/* wet floor */
	case NSAVE:
		if(noted(up->dbgreg, nureg, arg)){
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		break;
	default:
		up->lastnote->flag = NDebug;
		/* fall through */
	case NDFLT:
		noted(up->dbgreg, nureg, arg);	/* for debugging */
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		pexit(up->lastnote->msg, up->lastnote->flag!=NDebug);
	}

	/* allow next note */
	up->notified = 0;
}

uintptr
syssegbrk(sysva_list list)
{
	int i;
	uintptr addr;
	Segment *s;

	addr = sysva_arg(list, uintptr);
	for(i = 0; i < NSEG; i++) {
		s = up->seg[i];
		if(s == nil || addr < s->base || addr >= s->top)
			continue;
		switch(s->type&SG_TYPE) {
		case SG_TEXT:
		case SG_DATA:
		case SG_STACK:
		case SG_PHYSICAL:
		case SG_FIXED:
		case SG_STICKY:
			error(Ebadarg);
		default:
			return ibrk(sysva_arg(list, uintptr), i);
		}
	}
	error(Ebadarg);
}

uintptr
syssegattach(sysva_list list)
{
	int attr;
	char *name;
	uintptr va;
	ulong len;

	attr = sysva_arg(list, int);
	name = sysva_arg(list, char*);
	va = sysva_arg(list, uintptr);
	len = sysva_arg(list, ulong);
	validaddr((uintptr)name, 1, 0);
	name = validnamedup(name, 1);
	if(waserror()){
		free(name);
		nexterror();
	}
	va = segattach(attr, name, va, len);
	free(name);
	poperror();
	return va;
}

uintptr
syssegdetach(sysva_list list)
{
	int i;
	uintptr addr;
	Segment *s;

	addr = sysva_arg(list, uintptr);

	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}

	for(i = 0; i < NSEG; i++)
		if((s = up->seg[i]) != nil) {
			qlock(&s->qlk);
			if((addr >= s->base && addr < s->top) ||
			   (s->top == s->base && addr == s->base))
				goto found;
			qunlock(&s->qlk);
		}

	error(Ebadarg);

found:
	/*
	 * Check we are not detaching the initial stack segment.
	 */
	if(s == up->seg[SSEG]){
		qunlock(&s->qlk);
		error(Ebadarg);
	}
	qunlock(&s->qlk);
	up->seg[i] = nil;
	procmemdecommit(up, s->size);
	putseg(s);
	qunlock(&up->seglock);
	poperror();

	/* Ensure we flush any entries from the lost segment */
	flushmmu();
	return 0;
}

uintptr
syssegfree(sysva_list list)
{
	Segment *s;
	uintptr from, to;

	from = sysva_arg(list, uintptr);
	to = sysva_arg(list, ulong);
	to += from;
	if(to < from)
		error(Ebadarg);
	s = seg(up, from, 1);
	if(s == nil)
		error(Ebadarg);
	to &= ~(BY2PG-1);
	from = PGROUND(from);
	if(from >= to) {
		qunlock(&s->qlk);
		return 0;
	}
	if(to > s->top) {
		qunlock(&s->qlk);
		error(Ebadarg);
	}
	mfreeseg(s, from, (to - from) / BY2PG);
	qunlock(&s->qlk);
	flushmmu();
	return 0;
}

/* For binary compatibility */
uintptr
sysbrk_(sysva_list list)
{
	return ibrk(sysva_arg(list, uintptr), BSEG);
}

uintptr
sysrendezvous(sysva_list list)
{
	uintptr tag, val, new;
	Proc *p, **l;

	tag = sysva_arg(list, uintptr);
	new = sysva_arg(list, uintptr);
	if(up->rgrp == nil)
		error(Ebadarg);
	l = &REND(up->rgrp, tag);

	lock(&up->rgrp->lk);
	for(p = *l; p != nil; p = p->rendhash) {
		if(p->rendtag == tag) {
			*l = p->rendhash;
			val = p->rendval;
			p->rendval = new;
			unlock(&up->rgrp->lk);

			ready(p);

			return val;
		}
		l = &p->rendhash;
	}

	/* Going to sleep here */
	up->rendtag = tag;
	up->rendval = new;
	up->rendhash = *l;
	*l = up;
	up->state = Rendezvous;
	unlock(&up->rgrp->lk);

	sched();

	return up->rendval;
}

/*
 * The implementation of semaphores is complicated by needing
 * to avoid rescheduling in syssemrelease, so that it is safe
 * to call from real-time processes.  This means syssemrelease
 * cannot acquire any qlocks, only spin locks.
 *
 * Semacquire and semrelease must both manipulate the semaphore
 * wait list.  Lock-free linked lists only exist in theory, not
 * in practice, so the wait list is protected by a spin lock.
 *
 * The semaphore value *addr is stored in user memory, so it
 * cannot be read or written while holding spin locks.
 *
 * Thus, we can access the list only when holding the lock, and
 * we can access the semaphore only when not holding the lock.
 * This makes things interesting.  Note that sleep's condition function
 * is called while holding two locks - r and up->rlock - so it cannot
 * access the semaphore value either.
 *
 * An acquirer announces its intention to try for the semaphore
 * by putting a Sema structure onto the wait list and then
 * setting Sema.waiting.  After one last check of semaphore,
 * the acquirer sleeps until Sema.waiting==0.  A releaser of n
 * must wake up n acquirers who have Sema.waiting set.  It does
 * this by clearing Sema.waiting and then calling wakeup.
 *
 * There are three interesting races here.

 * The first is that in this particular sleep/wakeup usage, a single
 * wakeup can rouse a process from two consecutive sleeps!
 * The ordering is:
 *
 * 	(a) set Sema.waiting = 1
 * 	(a) call sleep
 * 	(b) set Sema.waiting = 0
 * 	(a) check Sema.waiting inside sleep, return w/o sleeping
 * 	(a) try for semaphore, fail
 * 	(a) set Sema.waiting = 1
 * 	(a) call sleep
 * 	(b) call wakeup(a)
 * 	(a) wake up again
 *
 * This is okay - semacquire will just go around the loop
 * again.  It does mean that at the top of the for(;;) loop in
 * semacquire, phore.waiting might already be set to 1.
 *
 * The second is that a releaser might wake an acquirer who is
 * interrupted before he can acquire the lock.  Since
 * release(n) issues only n wakeup calls -- only n can be used
 * anyway -- if the interrupted process is not going to use his
 * wakeup call he must pass it on to another acquirer.
 *
 * The third race is similar to the second but more subtle.  An
 * acquirer sets waiting=1 and then does a final canacquire()
 * before going to sleep.  The opposite order would result in
 * missing wakeups that happen between canacquire and
 * waiting=1.  (In fact, the whole point of Sema.waiting is to
 * avoid missing wakeups between canacquire() and sleep().) But
 * there can be spurious wakeups between a successful
 * canacquire() and the following semdequeue().  This wakeup is
 * not useful to the acquirer, since he has already acquired
 * the semaphore.  Like in the previous case, though, the
 * acquirer must pass the wakeup call along.
 *
 * This is all rather subtle.  The code below has been verified
 * with the spin model /sys/src/9/port/semaphore.p.  The
 * original code anticipated the second race but not the first
 * or third, which were caught only with spin.  The first race
 * is mentioned in /sys/doc/sleep.ps, but I'd forgotten about it.
 * It was lucky that my abstract model of sleep/wakeup still managed
 * to preserve that behavior.
 *
 * I remain slightly concerned about memory coherence
 * outside of locks.  The spin model does not take
 * queued processor writes into account so we have to
 * think hard.  The only variables accessed outside locks
 * are the semaphore value itself and the boolean flag
 * Sema.waiting.  The value is only accessed with cmpswap,
 * whose job description includes doing the right thing as
 * far as memory coherence across processors.  That leaves
 * Sema.waiting.  To handle it, we call coherence() before each
 * read and after each write.		- rsc
 */

/* Add semaphore p with addr a to list in seg. */
static void
semqueue(Segment *s, long *a, Sema *p)
{
	memset(p, 0, sizeof *p);
	p->addr = a;
	lock(&s->sema.rz.lk);	/* Sema -> Rendez -> named Lock */
	p->next = &s->sema;
	p->prev = s->sema.prev;
	p->next->prev = p;
	p->prev->next = p;
	unlock(&s->sema.rz.lk);
}

/* Remove semaphore p from list in seg. */
static void
semdequeue(Segment *s, Sema *p)
{
	lock(&s->sema.rz.lk);
	p->next->prev = p->prev;
	p->prev->next = p->next;
	unlock(&s->sema.rz.lk);
}

/* Wake up n waiters with addr a on list in seg. */
static void
semwakeup(Segment *s, long *a, long n)
{
	Sema *p;

	lock(&s->sema.rz.lk);
	for(p=s->sema.next; p!=&s->sema && n>0; p=p->next){
		if(p->addr == a && p->waiting){
			p->waiting = 0;
			coherence();
			wakeup(&p->rz);
			n--;
		}
	}
	unlock(&s->sema.rz.lk);
}

/* Add delta to semaphore and wake up waiters as appropriate. */
static long
semrelease(Segment *s, long *addr, long delta)
{
	long value;

	do
		value = *addr;
	while(!cmpswap(addr, value, value+delta));
	semwakeup(s, addr, delta);
	return value+delta;
}

/* Try to acquire semaphore using compare-and-swap */
static int
canacquire(long *addr)
{
	long value;

	while((value=*addr) > 0)
		if(cmpswap(addr, value, value-1))
			return 1;
	return 0;
}

/* Should we wake up? */
static int
semawoke(void *p)
{
	coherence();
	return !((Sema*)p)->waiting;
}

/* Acquire semaphore (subtract 1). */
static int
semacquire(Segment *s, long *addr, int block)
{
	int acquired;
	Sema phore;

	if(canacquire(addr))
		return 1;
	if(!block)
		return 0;
	semqueue(s, addr, &phore);
	if(acquired = !waserror()){
		for(;;){
			phore.waiting = 1;
			coherence();
			if(canacquire(addr))
				break;
			sleep(&phore.rz, semawoke, &phore);
		}
		poperror();
	}
	semdequeue(s, &phore);
	coherence();	/* not strictly necessary due to lock in semdequeue */
	if(!phore.waiting)
		semwakeup(s, addr, 1);
	if(!acquired)
		nexterror();
	return 1;
}

/* Acquire semaphore or time-out */
static int
tsemacquire(Segment *s, long *addr, ulong ms)
{
	int timedout, acquired;
	ulong t;
	Sema phore;

	if(canacquire(addr))
		return 1;
	if(ms == 0)
		return 0;
	timedout = 0;
	semqueue(s, addr, &phore);
	if(acquired = !waserror()){
		for(;;){
			phore.waiting = 1;
			coherence();
			if(canacquire(addr))
				break;
			t = MACHP(0)->pmach.ticks;
			tsleep(&phore.rz, semawoke, &phore, ms);
			t = TK2MS(MACHP(0)->pmach.ticks - t);
			if(t >= ms){
				timedout = 1;
				break;
			}
			ms -= t;
		}
		poperror();
	}
	semdequeue(s, &phore);
	coherence();	/* not strictly necessary due to lock in semdequeue */
	if(!phore.waiting)
		semwakeup(s, addr, 1);
	if(!acquired)
		nexterror();
	return !timedout;
}

uintptr
syssemacquire(sysva_list list)
{
	int block;
	long *addr;
	Segment *s;

	addr = sysva_arg(list, long*);
	block = sysva_arg(list, int);
	evenaddr((uintptr)addr);
	s = seg(up, (uintptr)addr, 0);
	if(s == nil || (s->type&SG_RONLY) != 0 || (uintptr)addr+sizeof(long) > s->top){
		validaddr((uintptr)addr, sizeof(long), 1);
		error(Ebadarg);
	}
	if(*addr < 0)
		error(Ebadarg);
	return (uintptr)semacquire(s, addr, block);
}

uintptr
systsemacquire(sysva_list list)
{
	long *addr;
	ulong ms;
	Segment *s;

	addr = sysva_arg(list, long*);
	ms = sysva_arg(list, ulong);
	evenaddr((uintptr)addr);
	s = seg(up, (uintptr)addr, 0);
	if(s == nil || (s->type&SG_RONLY) != 0 || (uintptr)addr+sizeof(long) > s->top){
		validaddr((uintptr)addr, sizeof(long), 1);
		error(Ebadarg);
	}
	if(*addr < 0)
		error(Ebadarg);
	return (uintptr)tsemacquire(s, addr, ms);
}

uintptr
syssemrelease(sysva_list list)
{
	long *addr, delta;
	Segment *s;

	addr = sysva_arg(list, long*);
	delta = sysva_arg(list, long);
	evenaddr((uintptr)addr);
	s = seg(up, (uintptr)addr, 0);
	if(s == nil || (s->type&SG_RONLY) != 0 || (uintptr)addr+sizeof(long) > s->top){
		validaddr((uintptr)addr, sizeof(long), 1);
		error(Ebadarg);
	}
	/* delta == 0 is a no-op, not a release */
	if(delta < 0 || *addr < 0)
		error(Ebadarg);
	return (uintptr)semrelease(s, addr, delta);
}

/* amd64: full nanosecond value in the return register. */
uintptr
sys_nsec(sysva_list list)
{
    USED(list);
    return (uintptr)todget(nil, nil);
}

#include "systab-gcc.h"

#ifdef PLAN2001_SYSCALL_TRACE
/*
 * Measurement-only dispatch tracing.  This is compiled only when the build
 * explicitly defines PLAN2001_SYSCALL_TRACE; the normal kernel has no trace
 * code, no extra state and no changed syscall semantics.
 *
 * Traps are kept in a bounded kernel buffer and dumped only at explicit
 * checkpoints.  The regular serial stream therefore keeps its normal prompt
 * framing while the measurement still carries every trap's sequence, PID,
 * program class, number, dispatch state and user PC.  The checkpoint call is
 * SYSCTL SysctlSnapshot and is deliberately not treated as a separate
 * measurement event.
 */
enum {
	TraceMax	= 32768,
	TraceOther	= 0,
	TraceMpy	= 1,
};

typedef struct Trace Trace;
struct Trace {
	uvlong	seq;
	uintptr	pc;
	uintptr	retpc;
	ulong	pid;
	ushort	nr;
	uchar	state;	/* 0 ACTIVE, 1 RESERVED, 2 UNKNOWN */
	uchar	kind;
};

static Trace	tracebuf[TraceMax];
static ulong	tracen;
static ulong	tracedrop;
static uvlong	syscalltraceno;
static int	tracecheckpoint;
static int	tracebootdumped;

/* OBJINVOKE operation histogram, so the measurement can prove that a typed
 * operation executed even when its legacy syscall number stayed at zero. */
enum { ObjOpTraceMax = 64 };
static ulong	objopcount[ObjOpTraceMax];

void
objoptrace(uint op)
{
	if(op < ObjOpTraceMax)
		objopcount[op]++;
}

static int
syscalltracekind(void)
{
	if(up->text != nil && strcmp(up->text, "micropython") == 0)
		return TraceMpy;
	return TraceOther;
}

void
syscalltracedump(char *label)
{
	char *state;
	int i;
	Trace *t;

	tracecheckpoint++;
	print("P2K-SYSCALL-DUMP checkpoint=%d label=%s records=%lud dropped=%lud\n",
		tracecheckpoint, label, tracen, tracedrop);
	for(i = 0; i < tracen; i++){
		t = &tracebuf[i];
		state = t->state == 0 ? "ACTIVE" :
			t->state == 1 ? "RESERVED" : "UNKNOWN";
		print("P2K-SYSCALL rec seq=%llud pid=%lud kind=%s nr=%ud state=%s pc=%#p retpc=%#p\n",
			t->seq, t->pid, t->kind == TraceMpy ? "micropython" : "other",
			t->nr, state, t->pc, t->retpc);
	}
	for(i = 0; i < ObjOpTraceMax; i++){
		if(objopcount[i] != 0)
			print("P2K-OBJOP checkpoint=%d op=%d count=%lud\n",
				tracecheckpoint, i, objopcount[i]);
		objopcount[i] = 0;
	}
	tracen = 0;
	tracedrop = 0;
}

static uintptr
syscalltraceret(Sargs *args, ulong scallnr, int kind)
{
	uintptr sp, off;

	/* The generated user stubs place the caller's return address above the
	 * syscall argument block.  MicroPython uses one 56-byte frame; the tiny
	 * boot loader uses its per-call frame from USER_SYSCALLS. */
	sp = (uintptr)args;
	if(kind == TraceMpy)
		off = 48;
	else
		switch(scallnr){
		case SYSCTL:	off = 40; break;
		case OBJOPEN:	off = 48; break;
		case OSPACEGET:
		case OSPACEPUT:
		case OSPACECOMMIT:
		case OEXECUTE:	off = 40; break;
		default:	off = 16; break;
		}
	if(okaddr(sp + off, BY2WD, 0))
		return *(uintptr*)(sp + off);
	return 0;
}

static void
syscalltrace(Sargs *args, ulong scallnr, uintptr pc)
{
	int state, kind;
	Trace *t;

	kind = syscalltracekind();
	if(!tracebootdumped && kind == TraceMpy){
		/*
		 * The first MicroPython trap follows the in-place OEXECUTE, so the
		 * boot loader's records are complete but not yet mixed with the
		 * interpreter's own traps.  This also runs for the first checkpoint
		 * call, which can be the first MicroPython trap.
		 */
		syscalltracedump("boot-loader");
		tracebootdumped = 1;
	}
	state = 2;
	if(scallnr < nsyscall){
		if(systab[scallnr] != nil)
			state = 0;
		else if(sysreserved[scallnr])
			state = 1;
	}
	syscalltraceno++;
	if(tracen == TraceMax){
		tracedrop++;
		return;
	}
	t = &tracebuf[tracen++];
	t->seq = syscalltraceno;
	t->pc = pc;
	t->retpc = syscalltraceret(args, scallnr, kind);
	t->pid = up->pid;
	t->nr = scallnr;
	t->state = state;
	t->kind = kind;
}
#endif

int
dosyscall(ulong scallnr, Sargs *args, uintptr *retp)
{
	vlong startns, stopns;
	uintptr ret;
	int s;

	m->pmach.syscall++;

	up->insyscall = 1;
	s = spllo();

	if(!waserror()){
		evenaddr((uintptr)args);
		validaddr((uintptr)args, sizeof(Sargs), 0);

		up->s = *args;
		up->scallnr = scallnr;

		if(up->procctl == Proc_tracesyscall){
			Sysargs sa;

			sysargs_init(&sa, up->s.args);
			syscallfmt(scallnr, userpc(), &sa);
			splhi();
			up->procctl = Proc_stopme;
			procctl();
			spllo();
			todget(nil, &startns);
		}
#ifdef PLAN2001_SYSCALL_TRACE
		syscalltrace(args, scallnr, userpc());
#endif
		if(scallnr >= nsyscall || systab[scallnr] == nil){
			/* A reserved historical number is a plain Ebadarg: it has no
			 * implementation and, unlike a truly unknown call, must not
			 * post a fatal debug note.  Neither path reaches legacy code. */
			if(scallnr < nsyscall && sysreserved[scallnr])
				error(Ebadarg);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}
		up->psstate = sysctab[scallnr];
		{
			Sysargs sa;

			sysargs_init(&sa, up->s.args);
			ret = systab[scallnr](&sa);
			if(sa.bad)
				error(Ebadarg);
		}
		poperror();
		if(scallnr == NOTED && ret == NOTED_RET){
			/* NOTED(29) ran the shared note-return core and changed the
			 * Ureg.  Return without writing the ordinary *retp, so the
			 * restored context is not overwritten.  The private marker is
			 * emitted only after notecont() succeeds; a failed call raises
			 * an error and never reaches this path. */
			splx(s);
			up->insyscall = 0;
			up->psstate = nil;
			return 1;
		}
	}else{
		/* failure: save the error buffer for errstr */
		char *e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
		ret = -1;
	}
	if(up->nerrlab){
		int i;

		print("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#p pc=%#p\n", up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}
	*retp = ret;
	if(up->procctl == Proc_tracesyscall){
		todget(nil, &stopns);
		{
			Sysargs sa;

			sysargs_init(&sa, up->s.args);
			sysretfmt(scallnr, &sa, ret, startns, stopns);
		}
		splhi();
		up->procctl = Proc_stopme;
		procctl();
	}
	splx(s);
	up->insyscall = 0;
	up->psstate = nil;
	return 0;
}
