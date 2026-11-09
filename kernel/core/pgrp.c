#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"lifediag.h"

void* octxnew(void);
void* octxcopy(void*);
void octxput(void*);

enum {
	Whinesecs = 10,		/* frequency of out-of-resources printing */
};

Pgrp*
newpgrp(void)
{
	Pgrp *p;

	p = mallocz(sizeof(Pgrp), 1);
	if(p == nil)
		error(Enomem);
	p->ref.ref = 1;
	p->ocontext = octxnew();
	if(p->ocontext == nil){ free(p); error(Enomem); }
	lifepgrp(p, 1);
	return p;
}

/* One permanent anchor, plus one owned reference per kernel process.
 * Initialize before allocating a Proc. Allocation is outside the publication
 * lock, so errors leave no lock held. This also works before up exists.
 */
Pgrp*
kprocpgrp(void)
{
	static Lock lk;
	static Pgrp *group;
	Pgrp *p, *candidate;

	lock(&lk);
	p = group;
	if(p != nil)
		incref(&p->ref);
	unlock(&lk);
	if(p != nil)
		return p;
	candidate = newpgrp();
	lock(&lk);
	if(group == nil){
		group = candidate;       /* transfer candidate's anchor reference */
		candidate = nil;
	}
	p = group;
	incref(&p->ref);
	unlock(&lk);
	if(candidate != nil)
		closepgrp(candidate);
	return p;
}

Rgrp*
newrgrp(void)
{
	Rgrp *r;

	r = malloc(sizeof(Rgrp));
	if(r == nil)
		error(Enomem);
	r->ref.ref = 1;
	return r;
}

void
closergrp(Rgrp *r)
{
	if(decref(&r->ref) == 0)
		free(r);
}

void
closepgrp(Pgrp *p)
{
	if(decref(&p->ref) == 0){
		lifepgrp(p, -1);
		octxput(p->ocontext);
		free(p);
	}
}

/*
 * A Pgrp owns a native object view and its namespace-extension state.
 * There is no mount list. RFNAMEG copies bindings; RFCNAMEG starts empty.
 * The restriction state is copied with the view, so an empty clone does not
 * silently regain extension authority.
 * The caller owns a live source reference and an unpublished destination.
 * Published Pgrp.ocontext pointers never change; OContext locks protect maps.
 * Allocate first so failure leaves both groups and their references intact.
 */
void
pgrpcpy(Pgrp *to, Pgrp *from, int copyview)
{
	void *c, *old;

	assert(to != from && to->ref.ref == 1);
	c = copyview ? octxcopy(from->ocontext) : octxcopy(nil);
	rlock(&from->ns);
	to->flags = from->flags;
	runlock(&from->ns);
	old = to->ocontext;
	to->ocontext = c;
	octxput(old);
}

/*
 * RFNOMNT is a monotonic restriction of one live Pgrp: a sealed group cannot
 * gain new role bindings or attach additional namespace views. It does not
 * delete existing bindings, revoke opened Handles, or block removal,
 * visibility or detach. Every process sharing the group observes the seal.
 *
 * Admission and the following OContext mutation must happen under one read
 * hold. A successful begin returns with the gate held and must be paired with
 * exactly one end, including on error paths. This makes the check and the
 * extension one linearizable step against pgrpsealnamespace(). Lock order is
 * Pgrp.ns before OContext locks; no caller may hold an OContext or authority
 * gate when calling begin.
 */
void
pgrpsealnamespace(Pgrp *pgrp)
{
	assert(pgrp != nil);
	wlock(&pgrp->ns);
	pgrp->flags |= PgrpNsSealed;
	wunlock(&pgrp->ns);
}

int
pgrpnamespaceextendbegin(Pgrp *pgrp)
{
	if(pgrp == nil)
		return 0;
	rlock(&pgrp->ns);
	if(pgrp->flags & PgrpNsSealed){
		runlock(&pgrp->ns);
		return 0;
	}
	return 1;
}

void
pgrpnamespaceextendend(Pgrp *pgrp)
{
	assert(pgrp != nil);
	runlock(&pgrp->ns);
}

/*
 * Legacy RFORK FD-group presence token.  The canonical kernel has no
 * producer of a first live file descriptor, so this token only distinguishes
 * an absent group from a present, shareable one.  RFFDG/RFCFDG allocate a
 * fresh token; a no-flag child shares the parent's token via incref(); each
 * Proc owns exactly one reference.  Allocation remains fallible.
 */
Ref*
newfdgrp(void)
{
	Ref *r;

	r = malloc(sizeof(Ref));
	if(r == nil)
		error(Enomem);
	r->ref = 1;
	return r;
}

void
closefdgrp(Ref *r)
{
	if(r == nil || decref(r))
		return;
	free(r);
}


#ifdef PLAN2001_LIFECYCLE_DIAG
/*
 * Both counters are sampled under one lock so each event line is a
 * consistent snapshot.  print() runs after unlocking; kernel event lines can
 * still interleave with a blocking user console write, so the T30 host
 * parser removes complete event lines before matching test markers.
 */
static Lock lifelk;
static long pgrplive, octxlive, pgrpseq;
static ulong lifeseq;

static void
lifeprint(char kind, int delta, ulong seq, ulong id, long pgrp, long octx)
{
	print("T30LIFE %c%c seq=%lud id=%lud pgrp=%ld octx=%ld\n",
		kind, delta > 0 ? '+' : '-', seq, id, pgrp, octx);
}

void
lifepgrp(void *v, int delta)
{
	Pgrp *pgrp = v;
	long p, o;
	ulong id, seq;

	lock(&lifelk);
	if(delta > 0)
		pgrp->diagid = ++pgrpseq;
	pgrplive += delta;
	id = pgrp->diagid;
	p = pgrplive;
	o = octxlive;
	seq = ++lifeseq;
	unlock(&lifelk);
	lifeprint('g', delta, seq, id, p, o);
}

void
lifecontrol(void *v, int delta)
{
	Pgrp *pgrp = v;
	long p, o;
	ulong id, seq;

	lock(&lifelk);
	id = pgrp->diagid;
	p = pgrplive;
	o = octxlive;
	seq = ++lifeseq;
	unlock(&lifelk);
	lifeprint('c', delta, seq, id, p, o);
}

void
lifeoctx(int delta)
{
	long p, o;
	ulong seq;

	lock(&lifelk);
	octxlive += delta;
	p = pgrplive;
	o = octxlive;
	seq = ++lifeseq;
	unlock(&lifelk);
	lifeprint('o', delta, seq, 0, p, o);
}
#endif

void
resrcwait(char *reason)
{
	static ulong lastwhine;
	ulong now;
	char *p;

	if(up == nil)
		panic("resrcwait: %s", reason);

	p = up->psstate;
	if(reason != nil) {
		if(waserror()){
			up->psstate = p;
			nexterror();
		}
		up->psstate = reason;
		now = seconds();
		/* don't tie up the console with complaints */
		if(now - lastwhine > Whinesecs) {
			lastwhine = now;
			print("%s\n", reason);
		}
	}
	tsleep(&up->sleep, return0, 0, 100+nrand(200));
	if(reason != nil) {
		up->psstate = p;
		poperror();
	}
}
