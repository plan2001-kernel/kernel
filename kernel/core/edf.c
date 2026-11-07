/* EDF scheduling */
#include	<u.h>
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"../core/edf.h"

/* debugging */
enum {
	Dontprint = 1,
};

#define DPRINT	if(Dontprint){}else print

static long	now;	/* Low order 32 bits of time in µs */
extern ulong	delayedscheds;
extern Schedq	runq[Nrq];
extern int	nrdy;
extern ulong	runvec;

/* Statistics stuff */
ulong		nilcount;
ulong		scheds;
ulong		edfnrun;
int		misseddeadlines;

/* Edfschedlock protects modification of admission params */
int		edfinited;
QLock		edfschedlock;
static Lock	thelock;

enum{
	Dl,	/* Invariant for schedulability test: Dl < Rl */
	Rl,
};

static char *testschedulability(Proc*);
static Proc *qschedulability;

enum {
	Onemicrosecond =	1,
	Onemillisecond =	1000,
	Onesecond =		1000000,
	OneRound = 		Onemillisecond/2,
};

static int
timeconv(Fmt *f)
{
	char buf[128], *sign;
	vlong t;

	buf[0] = 0;
	switch(f->r) {
	case 'U':
		t = va_arg(f->args, uvlong);
		break;
	case 't':			/* vlong in nanoseconds */
		t = va_arg(f->args, long);
		break;
	default:
		return fmtstrcpy(f, "(timeconv)");
	}
	if (t < 0) {
		sign = "-";
		t = -t;
	}
	else
		sign = "";
	if (t > Onesecond){
		t += OneRound;
		sprint(buf, "%s%d.%.3ds", sign, (int)(t / Onesecond),
			(int)(t % Onesecond)/Onemillisecond);
	}else if (t > Onemillisecond)
		sprint(buf, "%s%d.%.3dms", sign, (int)(t / Onemillisecond),
			(int)(t % Onemillisecond));
	else
		sprint(buf, "%s%dµs", sign, (int)t);
	return fmtstrcpy(f, buf);
}

long edfcycles;

Edf*
edflock(Proc *p)
{
	Edf *e;

	if (p->edf == nil)
		return nil;
	ilock(&thelock);
	if((e = p->edf) && (e->flags & Admitted)){
		thelock.pc = getcallerpc(&p);
#ifdef EDFCYCLES
		edfcycles -= lcycles();
#endif
		now = µs();
		return e;
	}
	iunlock(&thelock);
	return nil;
}

void
edfunlock(void)
{

#ifdef EDFCYCLES
	edfcycles += lcycles();
#endif
	edfnrun++;
	iunlock(&thelock);
}

void
edfinit(Proc*p)
{
	if(!edfinited){
		fmtinstall('t', timeconv);
		edfinited++;
	}
	now = µs();
	DPRINT("%lud edfinit %lud[%s]\n", now, p->pid, statename[p->state]);
	p->edf = malloc(sizeof(Edf));
	if(p->edf == nil)
		error(Enomem);
	return;
}

static void
deadlineintr(Ureg*, Timer *t)
{
	/* Proc reached deadline */
	Proc *p;

	if(panicking || active.exiting)
		return;

	p = t->ta;
	now = µs();
	DPRINT("%lud deadlineintr %lud[%s]\n", now, p->pid, statename[p->state]);
	/* If we're interrupting something other than the proc pointed to by t->a,
	 * we've already achieved recheduling, so we need not do anything
	 * Otherwise, we must cause a reschedule, but if we call sched()
	 * here directly, the timer interrupt routine will not finish its business
	 * Instead, we cause the resched to happen when the interrupted proc
	 * returns to user space
	 */
	if(p == up){
		up->delaysched++;
 		delayedscheds++;
	}
}

static void
release(Proc *p)
{
	/* Called with edflock held */
	Edf *e;
	long n;

	e = p->edf;
	e->flags &= ~Yield;
	if(e->d - now < 0){
		e->periods++;
		e->r = now;
		if((e->flags & Sporadic) == 0){
			/*
			 * Non sporadic processes stay true to their period;
			 * calculate next release time.
			 * Second test limits duration of while loop.
			 */
			if((n = now - e->t) > 0){
				if(n < e->T)
					e->t += e->T;
				else
					e->t = now + e->T - (n % e->T);
			}
		}else{
			/* Sporadic processes may not be released earlier than
			 * one period after this release
			 */
			e->t = e->r + e->T;
		}
		e->d = e->r + e->D;
		e->S = e->C;
		DPRINT("%lud release %lud[%s], r=%lud, d=%lud, t=%lud, S=%lud\n",
			now, p->pid, statename[p->state], e->r, e->d, e->t, e->S);
	}else{
		DPRINT("%lud release %lud[%s], too late t=%lud, called from %#p\n",
			now, p->pid, statename[p->state], e->t, getcallerpc(&p));
	}
}

static void
releaseintr(Ureg *u, Timer *t)
{
	Proc *p;

	if(panicking || active.exiting)
		return;

	p = t->ta;
	if((edflock(p)) == nil)
		return;
	DPRINT("%lud releaseintr %lud[%s]\n", now, p->pid, statename[p->state]);
	switch(p->state){
	default:
		edfunlock();
		return;
	case Ready:
		/* remove proc from current runq */
		if(dequeueproc(&runq[p->priority], p) != p){
			DPRINT("releaseintr: can't find proc or lock race\n");
			release(p);	/* It'll start best effort */
			edfunlock();
			return;
		}
		/* fall through */
	case Waitrelease:
		p->state = Scheding;
		release(p);
		edfunlock();
		ready(p);
		if(up){
			up->delaysched++;
			delayedscheds++;
		}
		return;
	case Running:
		release(p);
		edfrun(p, 1);
		break;
	case Wakeme:
		release(p);
		edfunlock();
		twakeup(u, t);
		if(up){
			up->delaysched++;
			delayedscheds++;
		}
		return;
	}
	edfunlock();
}

void
edfrecord(Proc *p)
{
	long used;
	Edf *e;

	if((e = edflock(p)) == nil)
		return;
	used = now - e->s;
	if(e->d - now <= 0)
		e->edfused += used;
	else
		e->extraused += used;
	if(e->S > 0){
		if(e->S <= used){
			DPRINT("%lud edfrecord slice used up\n", now);
			e->d = now;
			e->S = 0;
		}else
			e->S -= used;
	}
	e->s = now;
	edfunlock();
}

void
edfrun(Proc *p, int edfpri)
{
	Edf *e;
	long tns;

	e = p->edf;
	/* Called with edflock held */
	if(edfpri){
		tns = e->d - now;
		if(tns <= 0 || e->S == 0){
			/* Deadline reached or resources exhausted,
			 * deschedule forthwith
			 */
			p->delaysched++;
 			delayedscheds++;
			e->s = now;
			return;
		}
		if(e->S < tns)
			tns = e->S;
		if(tns < 20)
			tns = 20;
		e->timer.tns = 1000LL * tns;	/* µs to ns */
		if(e->timer.tt == nil || e->timer.tf != deadlineintr){
			DPRINT("%lud edfrun, deadline=%lud\n", now, tns);
		}else{
			DPRINT("v");
		}
		e->timer.tmode = Trelative;
		e->timer.tf = deadlineintr;
		e->timer.ta = p;
		timeradd(&e->timer);
	}else{
		DPRINT("<");
	}
	e->s = now;
}

char *
edfadmit(Proc *p)
{
	char *err;
	Edf *e;
	int i;
	Proc *r;
	long tns;

	e = p->edf;
	if (e->flags & Admitted)
		return "task state";	/* should never happen */

	/* simple sanity checks */
	if (e->T == 0)
		return "T not set";
	if (e->C == 0)
		return "C not set";
	if (e->D > e->T)
		return "D > T";
	if (e->D == 0)	/* if D is not set, set it to T */
		e->D = e->T;
	if (e->C > e->D)
		return "C > D";

	qlock(&edfschedlock);
	if (err = testschedulability(p)){
		qunlock(&edfschedlock);
		return err;
	}
	e->flags |= Admitted;

	edflock(p);

	/* Look for another proc with the same period to synchronize to */
	for(i=0; (r = proctab(i)) != nil; i++) {
		if(r->state <= New || r == p)
			continue;
		if(r->edf == nil || (r->edf->flags & Admitted) == 0)
			continue;
		if(r->edf->T == e->T)
			break;
	}
	if(r == nil){
		/* Can't synchronize to another proc, release now */
		e->t = now;
		e->d = 0;
		release(p);
		if (p == up){
			DPRINT("%lud edfadmit self %lud[%s], release now: r=%lud d=%lud t=%lud\n",
				now, p->pid, statename[p->state], e->r, e->d, e->t);
			/* We're already running */
			edfrun(p, 1);
		}else{
			/* We're releasing another proc */
			DPRINT("%lud edfadmit other %lud[%s], release now: r=%lud d=%lud t=%lud\n",
				now, p->pid, statename[p->state], e->r, e->d, e->t);
			p->timer.ta = p;
			edfunlock();
			qunlock(&edfschedlock);
			releaseintr(nil, &p->timer);
			return nil;
		}
	}else{
		/* Release in synch to something else */
		e->t = r->edf->t;
		if (p == up){
			DPRINT("%lud edfadmit self %lud[%s], release at %lud\n",
				now, p->pid, statename[p->state], e->t);
			edfunlock();
			qunlock(&edfschedlock);
			return nil;
		}else{
			DPRINT("%lud edfadmit other %lud[%s], release at %lud\n",
				now, p->pid, statename[p->state], e->t);
			if(e->timer.tt == nil){
				tns = e->t - now;
				if(tns < 20)
					tns = 20;
				e->timer.tns = 1000LL * tns;
				e->timer.tmode = Trelative;
				e->timer.tf = releaseintr;
				e->timer.ta = p;
				timeradd(&e->timer);
			}
		}
	}
	edfunlock();
	qunlock(&edfschedlock);
	return nil;
}

void
edfstop(Proc *p)
{
	Edf *e;

	if(e = edflock(p)){
		DPRINT("%lud edfstop %lud[%s]\n", now, p->pid, statename[p->state]);
		e->flags &= ~Admitted;
		timerdel(&e->timer);
		edfunlock();
	}
}

static int
yfn(void *)
{
	now = µs();
	return up->trend == nil || now - up->edf->r >= 0;
}

void
edfyield(void)
{
	/* sleep until next release */
	Edf *e;
	long n;

	if((e = edflock(up)) == nil)
		return;
	if((n = now - e->t) > 0){
		if(n < e->T)
			e->t += e->T;
		else
			e->t = now + e->T - (n % e->T);
	}
	e->r = e->t;
	e->flags |= Yield;
	e->d = now;
	n = e->t - now;
	if(n < 20)
		n = 20;
	up->timer.tns = 1000LL * n;
	up->timer.tmode = Trelative;
	up->timer.tf = releaseintr;
	up->timer.ta = up;
	up->trend = &up->sleep;
	timeradd(&up->timer);
	edfunlock();
	if(waserror()){
		up->trend = nil;
		timerdel(&up->timer);
		nexterror();
	}
	sleep(&up->sleep, yfn, nil);
	up->trend = nil;
	timerdel(&up->timer);
	poperror();
}

int
edfready(Proc *p)
{
	Edf *e;
	long n;

	if((e = edflock(p)) == nil)
		return 0;
	if(e->d - now <= 0){
		/* past deadline, arrange for next release */
		if((e->flags & Sporadic) == 0){
			/*
			 * Non sporadic processes stay true to their period;
			 * calculate next release time.
			 */
			if((n = now - e->t) > 0){
				if(n < e->T)
					e->t += e->T;
				else
					e->t = now + e->T - (n % e->T);
			}
		}
		if(now - e->t < 0){
			/* Next release is in the future, schedule it */
			if(e->timer.tt == nil || e->timer.tf != releaseintr){
				n = e->t - now;
				if(n < 20)
					n = 20;
				e->timer.tns = 1000LL * n;
				e->timer.tmode = Trelative;
				e->timer.tf = releaseintr;
				e->timer.ta = p;
				timeradd(&e->timer);
				DPRINT("%lud edfready %lud[%s], release=%lud\n",
					now, p->pid, statename[p->state], e->t);
			}
			if(p->state == Running && (e->flags & (Yield|Yieldonblock)) == 0 && (e->flags & Extratime)){
				/* If we were running, we've overrun our CPU allocation
				 * or missed the deadline, continue running best-effort at low priority
				 * Otherwise we were blocked.  If we don't yield on block, we continue
				 * best effort
				 */
				DPRINT(">");
				edfunlock();
				procpriority(p, PriExtra, 1);
				return 0;	/* Stick on runq[PriExtra] */
			}
			DPRINT("%lud edfready %lud[%s] wait release at %lud\n",
				now, p->pid, statename[p->state], e->t);
			p->state = Waitrelease;
			edfunlock();
			return -1;	/* Make runnable later */
		}
		DPRINT("%lud edfready %lud %s release now\n", now, p->pid, statename[p->state]);
		/* release now */
		release(p);
	}
	edfunlock();
	DPRINT("^");
	return 1;
}


static void
testenq(Proc *p)
{
	Proc *xp, **xpp;
	Edf *e;

	e = p->edf;
	e->testnext = nil;
	if (qschedulability == nil) {
		qschedulability = p;
		return;
	}
	PLAN2001_SET(xp);
	for (xpp = &qschedulability; *xpp; xpp = &xp->edf->testnext) {
		xp = *xpp;
		if (e->testtime - xp->edf->testtime < 0
		|| (e->testtime == xp->edf->testtime && e->testtype < xp->edf->testtype)){
			e->testnext = xp;
			*xpp = p;
			return;
		}
	}
	assert(xp->edf->testnext == nil);
	xp->edf->testnext = p;
}

static char *
testschedulability(Proc *theproc)
{
	Proc *p;
	long H, G, Cb, ticks;
	int steps, i;

	/* initialize */
	DPRINT("schedulability test %lud\n", theproc->pid);
	qschedulability = nil;
	for(i=0; (p = proctab(i)) != nil; i++) {
		if(p->state == Dead)
			continue;
		if((p->edf == nil || (p->edf->flags & Admitted) == 0) && p != theproc)
			continue;
		p->edf->testtype = Rl;
		p->edf->testtime = 0;
		DPRINT("\tInit: edfenqueue %lud\n", p->pid);
		testenq(p);
	}
	H=0;
	G=0;
	for(steps = 0; steps < Maxsteps; steps++){
		p = qschedulability;
		qschedulability = p->edf->testnext;
		ticks = p->edf->testtime;
		switch (p->edf->testtype){
		case Dl:
			H += p->edf->C;
			Cb = 0;
			DPRINT("\tStep %3d, Ticks %lud, pid %lud, deadline, H += %lud → %lud, Cb = %lud\n",
				steps, ticks, p->pid, p->edf->C, H, Cb);
			if (H+Cb>ticks){
				DPRINT("not schedulable\n");
				return "not schedulable";
			}
			p->edf->testtime += p->edf->T - p->edf->D;
			p->edf->testtype = Rl;
			testenq(p);
			break;
		case Rl:
			DPRINT("\tStep %3d, Ticks %lud, pid %lud, release, G  %lud, C%lud\n",
				steps, ticks, p->pid, p->edf->C, G);
			if(ticks && G <= ticks){
				DPRINT("schedulable\n");
				return nil;
			}
			G += p->edf->C;
			p->edf->testtime += p->edf->D;
			p->edf->testtype = Dl;
			testenq(p);
			break;
		default:
			assert(0);
		}
	}
	DPRINT("probably not schedulable\n");
	return "probably not schedulable";
}
