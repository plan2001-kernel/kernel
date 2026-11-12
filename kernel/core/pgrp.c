#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"lifediag.h"


enum {
	Whinesecs = 10,		/* frequency of out-of-resources printing */
};

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
static long fpulive, proclive;
static ulong lifeseq, delegateid;

static void
lifeprint(char kind, int delta, ulong seq, ulong id, long pgrp, long octx,
	long proc)
{
	print("T30LIFE %c%c seq=%lud id=%lud pgrp=%ld octx=%ld proc=%ld\n",
		kind, delta > 0 ? '+' : '-', seq, id, pgrp, octx, proc);
}

/*
 * Procs reserved by newproc() but not yet published by ready() or released
 * by procdiscard().  A leaked New Proc at a test marker is visible here even
 * when no PID was allocated yet.
 */
void
lifeproc(int delta)
{
	long p, o, r;
	ulong seq;

	lock(&lifelk);
	proclive += delta;
	if(proclive < 0)
		proclive = 0;
	p = 0;
	o = 0;
	r = proclive;
	seq = ++lifeseq;
	unlock(&lifelk);
	lifeprint('r', delta, seq, 0, p, o, r);
}

/*
 * Delegated caps=2 Space capabilities and the two resources they own: the
 * opened GE/O transaction and the pinned grant-origin reference.  The host
 * parser rejects an unmatched alloc/free for any of the three kinds, so a
 * leaked internal reference that VobjInfo.holds cannot see still fails.
 */
ulong
lifedelegateid(void)
{
	ulong id;

	lock(&lifelk);
	id = ++delegateid;
	unlock(&lifelk);
	return id;
}

void
lifedelegate(int kind, ulong id, int delta)
{
	long p, o, r;
	ulong seq;

	lock(&lifelk);
	p = 0;
	o = 0;
	r = proclive;
	seq = ++lifeseq;
	unlock(&lifelk);
	lifeprint(kind, delta, seq, id, p, o, r);
}

/* Net live FPalloc buffers.  Reported only when a never-scheduled Proc had
 * an FP context, so repeated prepublication failures can be compared. */
void
lifefpu(int delta)
{
	lock(&lifelk);
	fpulive += delta;
	if(fpulive < 0)
		fpulive = 0;
	unlock(&lifelk);
}

void
lifefpudiscard(int fpsave, int kfpsave)
{
	long n;

	lock(&lifelk);
	n = fpulive;
	unlock(&lifelk);
	print("T30FPU discard fpsave=%d kfpsave=%d live=%ld\n",
		fpsave != 0, kfpsave != 0, n);
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
