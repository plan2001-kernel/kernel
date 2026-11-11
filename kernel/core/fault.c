#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

enum
{
	Faultok		= 0,
	Faultretry	= -1,
	Faultnopage	= -2,
};

void
faultnote(char *type, char *access, uintptr addr)
{
	char buf[ERRMAX];

	checkpages();
	snprint(buf, sizeof(buf), "sys: trap: %s %s addr=%#p", type, access, addr);
	postnote(up, 1, buf, NDebug);
}

static int
fixfault(Segment *s, uintptr addr, int read)
{
	Pte **pte, *etp;
	uintptr soff, mmuphys;
	Page **pg, *old, *new;

	addr &= ~(BY2PG-1);
	soff = addr-s->base;
	pte = &s->map[soff/PTEMAPMEM];
	if((etp = *pte) == nil){
		etp = ptealloc();
		if(etp == nil){
			qunlock(&s->qlk);
			if(!waserror()){
				resrcwait("no memory for ptealloc");
				poperror();
			}
			return Faultretry;
		}
		*pte = etp;
	}

	pg = &etp->pages[(soff&(PTEMAPMEM-1))/BY2PG];
	if(pg < etp->first)
		etp->first = pg;
	if(pg > etp->last)
		etp->last = pg;

	switch(s->type & SG_TYPE) {
	default:
		panic("fault");

	case SG_TEXT: 			/* Resident text, no backing image */
		if(pagedout(*pg)){
			qunlock(&s->qlk);
			return Faultnopage;
		}
		mmuphys = PPN((*pg)->pa) | PTERONLY | PTECACHED | PTEVALID;
		(*pg)->modref = PG_REF;
		break;

	case SG_BSS:
	case SG_STACK:
		if(*pg == nil) {
			new = newpage(addr, &s->qlk);
			if(new == nil)
				return Faultretry;
			*pg = fillpage(new, (s->type&SG_TYPE)==SG_STACK? 0xfe: 0);
			s->used++;
		}
		/* wet floor */
	case SG_DATA:			/* Copy on write */
		if(pagedout(*pg)){
			qunlock(&s->qlk);
			return Faultnopage;
		}
		/*
		 *  It's only possible to copy on write if
		 *  we're the only user of the segment.
		 */
		if(read && conf.copymode == 0 && s->ref.ref == 1) {
			mmuphys = PPN((*pg)->pa) | PTERONLY | PTECACHED | PTEVALID;
			(*pg)->modref |= PG_REF;
			break;
		}

		old = *pg;
		if(old->ref.ref > 1) {
			new = newpage(addr, &s->qlk);
			if(new == nil)
				return Faultretry;
			copypage(old, new);
			settxtflush(new, s->flushme);
			*pg = new;
			/* s->used count unchanged */
			putpage(old);
		}
		/* wet floor */
	case SG_STICKY:			/* Never paged out */
		mmuphys = PPN((*pg)->pa) | PTEWRITE | PTECACHED | PTEVALID;
		(*pg)->modref |= up->privatemem? PG_PRIV|PG_MOD|PG_REF: PG_MOD|PG_REF;
		break;

	case SG_FIXED:			/* Never paged out */
		mmuphys = PPN((*pg)->pa) | PTEWRITE | PTEUNCACHED | PTEVALID;
		(*pg)->modref |= up->privatemem? PG_PRIV|PG_MOD|PG_REF: PG_MOD|PG_REF;
		break;

	case SG_REGION:
		/*
		 * Every mapping has its own Segment and PTE set; the physical
		 * Page objects are shared.  SG_RONLY is therefore a per-address-
		 * space property, enforced both by fault() rejecting stores and
		 * by installing PTERONLY here.  A missing page is an invariant
		 * violation: region backing is allocated eagerly.  This case is
		 * deliberately separate from the SG_DATA/SG_BSS/SG_STACK
		 * fallthrough so ordinary private memory keeps its PG_PRIV
		 * sanitization.
		 */
		if(pagedout(*pg)){
			qunlock(&s->qlk);
			return Faultnopage;
		}
		if((s->type & SG_RONLY) != 0){
			mmuphys = PPN((*pg)->pa) | PTERONLY | PTECACHED | PTEVALID;
			(*pg)->modref |= PG_REF;
		}else{
			/* Region Pages are shared with readers: never mark them
			 * PG_PRIV, which would let zeroprivatepages() clear memory
			 * another authorized address space is observing. */
			mmuphys = PPN((*pg)->pa) | PTEWRITE | PTECACHED | PTEVALID;
			(*pg)->modref |= PG_MOD|PG_REF;
		}
		break;
	}

#ifdef PTENOEXEC
	if((s->type & SG_NOEXEC) != 0 || s->flushme == 0)
		mmuphys |= PTENOEXEC;
#endif

	qunlock(&s->qlk);

	putmmu(addr, mmuphys, *pg);

	return Faultok;
}

static void
mapphys(Segment *s, uintptr addr, int attr)
{
	uintptr mmuphys;
	Page pg = {0};

	addr &= ~(BY2PG-1);
	pg.ref.ref = 1;
	pg.va = addr;
	pg.pa = s->pseg->pa+(addr-s->base);
	settxtflush(&pg, s->flushme);

	mmuphys = PPN(pg.pa) | PTEVALID;
	if((attr & SG_RONLY) == 0)
		mmuphys |= PTEWRITE;
	else
		mmuphys |= PTERONLY;

#ifdef PTENOEXEC
	if((attr & SG_NOEXEC) != 0 || s->flushme == 0)
		mmuphys |= PTENOEXEC;
#endif

#ifdef PTEDEVICE
	if((attr & SG_DEVICE) != 0)
		mmuphys |= PTEDEVICE;
	else
#endif
	if((attr & SG_CACHED) == 0)
		mmuphys |= PTEUNCACHED;
	else
		mmuphys |= PTECACHED;

	qunlock(&s->qlk);

	putmmu(addr, mmuphys, &pg);
}

int
fault(uintptr addr, uintptr pc, int read)
{
	Segment *s;
	char *sps;
	int pnd, ins, attr, r;

	if(up == nil)
		panic("fault: no user process pc=%#p addr=%#p", pc, addr);

	if(up->nlocks){
		Lock *l = up->lastlock;
		print("fault: nlocks %d, proc %lud %s, addr %#p, lock %#p, lpc %#p\n", 
			up->nlocks, up->pid, up->text, addr, l, l ? l->pc : 0);
	}

	pnd = up->notepending;
	ins = up->insyscall;
	up->insyscall = 1;
	sps = up->psstate;
	up->psstate = "Fault";

	m->pmach.pfault++;

	for(;;) {
		spllo();

		s = seg(up, addr, 1);		/* leaves s locked if seg != nil */
		if(s == nil) {
			up->psstate = sps;
			up->insyscall = ins;
			return -1;
		}

		attr = s->type;
		if((attr & SG_TYPE) == SG_PHYSICAL)
			attr |= s->pseg->attr;

		if((attr & SG_FAULT) != 0
		|| read? ((attr & SG_NOEXEC) != 0 || s->flushme == 0) && (addr & -BY2PG) == (pc & -BY2PG):
			 (attr & SG_RONLY) != 0) {
			qunlock(&s->qlk);
			up->psstate = sps;
			up->insyscall = ins;
			if(up->kp && up->nerrlab)	/* for segio */
				error(Eio);
			return -1;
		}

		if((attr & SG_TYPE) == SG_PHYSICAL){
			mapphys(s, addr, attr);
			break;
		}

		r = fixfault(s, addr, read);
		if(r == Faultok)
			break;
		if(r == Faultnopage){
			up->psstate = sps;
			up->insyscall = ins;
			if(up->nerrlab)
				error(Eio);
			return -1;
		}

		splhi();
		switch(up->procctl){
		case Proc_exitme:
		case Proc_exitbig:
			if(up->nerrlab){
				up->psstate = sps;
				up->insyscall = ins;
				up->notepending |= pnd;
				error(up->procctl==Proc_exitbig?
					"Killed: Insufficient physical memory":
					"Killed");
			}
			procctl();
			break;
		}
	}

	up->psstate = sps;
	up->insyscall = ins;
	up->notepending |= pnd;

	return 0;
}

/*
 * Called only in a system call
 */
int
okaddr(uintptr addr, ulong len, int write)
{
	Segment *s;

	if((long)len >= 0 && len <= -addr) {
		for(;;) {
			s = seg(up, addr, 0);
			if(s == nil || (write && (s->type&SG_RONLY)))
				break;

			if(addr+len > s->top) {
				len -= s->top - addr;
				addr = s->top;
				continue;
			}
			return 1;
		}
	}
	return 0;
}

void
validaddr(uintptr addr, ulong len, int write)
{
	if(!okaddr(addr, len, write)){
		pprint("suicide: invalid address %#p/%lud in sys call pc=%#p\n", addr, len, userpc());
		postnote(up, 1, "sys: bad address in syscall", NDebug);
		error(Ebadarg);
	}
}

/*
 * &s[0] is known to be a valid address.
 */
void*
vmemchr(void *s, int c, ulong n)
{
	uintptr a;
	ulong m;
	void *t;

	a = (uintptr)s;
	for(;;){
		m = BY2PG - (a & (BY2PG-1));
		if(n <= m)
			break;
		/* spans pages; handle this page */
		t = memchr((void*)a, c, m);
		if(t != nil)
			return t;
		a += m;
		n -= m;
		if(a < KZERO)
			validaddr(a, 1, 0);
	}

	/* fits in one page */
	return memchr((void*)a, c, n);
}

Segment*
seg(Proc *p, uintptr addr, int dolock)
{
	Segment **s, **et, *n;

	et = &p->seg[NSEG];
	for(s = p->seg; s < et; s++) {
		if((n = *s) == nil)
			continue;
		if(addr >= n->base && addr < n->top) {
			if(dolock == 0)
				return n;

			qlock(&n->qlk);
			if(addr >= n->base && addr < n->top)
				return n;
			qunlock(&n->qlk);
		}
	}

	return nil;
}

extern void checkmmu(uintptr, uintptr);

void
checkpages(void)
{
	uintptr addr, off;
	Pte *p;
	Page *pg;
	Segment **sp, **ep, *s;
	
	if(up == nil)
		return;

	for(sp=up->seg, ep=&up->seg[NSEG]; sp<ep; sp++){
		if((s = *sp) == nil)
			continue;
		qlock(&s->qlk);
		if(s->mapsize > 0){
			for(addr=s->base; addr<s->top; addr+=BY2PG){
				off = addr - s->base;
				if((p = s->map[off/PTEMAPMEM]) == nil)
					continue;
				pg = p->pages[(off&(PTEMAPMEM-1))/BY2PG];
				if(pagedout(pg))
					continue;
				checkmmu(addr, pg->pa);
			}
		}
		qunlock(&s->qlk);
	}
}
