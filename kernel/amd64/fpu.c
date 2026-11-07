#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include "io.h"

enum {
	CR4Osfxsr  = 1 << 9,
	CR4Oxmmex  = 1 << 10,
	CR4Oxsave  = 1 << 18,
};

/*
 * SIMD Floating Point.
 * Assembler support to get at the individual instructions
 * is in l.s.
 */
extern void _clts(void);
extern void _fldcw(u16int);
extern void _fnclex(void);
extern void _fninit(void);
extern void _fxrstor(void*);
extern void _fxsave(void*);
extern void _xrstor(void*);
extern void _xsave(void*);
extern void _xsaveopt(void*);
extern void _fwait(void);
extern void _ldmxcsr(u32int);
extern void _stts(void);

static void mathemu(Ureg *ureg, void*);

static void
fpssesave(FPsave *s)
{
	_fxsave(s);
	_stts();
}
static void
fpsserestore(FPsave *s)
{
	_clts();
	_fxrstor(s);
}

static void
fpxsave(FPsave *s)
{
	_xsave(s);
	_stts();
}
static void
fpxrestore(FPsave *s)
{
	_clts();
	_xrstor(s);
}

static void
fpxsaves(FPsave *s)
{
	_xsaveopt(s);
	_stts();
}
static void
fpxrestores(FPsave *s)
{
	_clts();
	_xrstor(s);
}

static void
fpxsaveopt(FPsave *s)
{
	_xsaveopt(s);
	_stts();
}

/*
 *  Turn the FPU on and initialise it for use.
 *  Set the precision and mask the exceptions
 *  we don't care about from the generic Mach value.
 */
void
fpinit(void)
{
	_clts();
	_fninit();
	_fwait();
	_fldcw(0x0232);
	_ldmxcsr(0x1900);
}

static char* mathmsg[] =
{
	nil,	/* handled below */
	"denormalized operand",
	"division by zero",
	"numeric overflow",
	"numeric underflow",
	"precision loss",
};

static void
mathnote(ulong status, uintptr pc, int kernel)
{
	char *msg, note[ERRMAX];
	int i;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	msg = "unknown exception";
	for(i = 1; i <= 5; i++){
		if(!((1<<i) & status))
			continue;
		msg = mathmsg[i];
		break;
	}
	if(status & 0x01){
		if(status & 0x40){
			if(status & 0x200)
				msg = "stack overflow";
			else
				msg = "stack underflow";
		}else
			msg = "invalid operation";
	}
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=0x%lux", msg, pc, status);
	if(kernel)
		panic("%s", note);

	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil)
			mathnote(m->fpsave->save.avx.sse.fsw, m->fpsave->save.avx.sse.rip, 1);
		else
			mathnote(up->pfpu.kfpsave->save.avx.sse.fsw, up->pfpu.kfpsave->save.avx.sse.rip, 1);
		return;
	}
	if(up->pfpu.fpstate != FPinactive){
		_clts();
		fpsave(&up->pfpu.fpsave->save);
		up->pfpu.fpstate = FPinactive;
	}
	mathnote(up->pfpu.fpsave->save.avx.sse.fsw, up->pfpu.fpsave->save.avx.sse.rip, 0);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil)
			mathnote(m->fpsave->save.avx.sse.mxcsr & 0x3f, ureg->pc, 1);
		else
			mathnote(up->pfpu.kfpsave->save.avx.sse.mxcsr & 0x3f, ureg->pc, 1);
		return;
	}
	if(up->pfpu.fpstate != FPinactive){
		_clts();
		fpsave(&up->pfpu.fpsave->save);
		up->pfpu.fpstate = FPinactive;
	}
	mathnote(up->pfpu.fpsave->save.avx.sse.mxcsr & 0x3f, ureg->pc, 0);
}

/*
 *  math coprocessor segment overrun
 */
static void
mathover(Ureg *ureg, void*)
{
	if(!userureg(ureg))
		panic("math overrun");

	pexit("math overrun", 0);
}

void
mathinit(void)
{
	trapenable(VectorCERR, matherror, 0, "matherror");
	if(m->cpuidfamily == 3)
		intrenable(IrqIRQ13, matherror, 0, BUSUNKNOWN, "matherror");
	trapenable(VectorCNA, mathemu, 0, "mathemu");
	trapenable(VectorCSO, mathover, 0, "mathover");
	trapenable(VectorSIMD, simderror, 0, "simderror");
}

/*
 *  fpuinit(), called from cpuidentify() for each cpu.
 */
void
fpuinit(void)
{
	u64int cr4;
	ulong regs[4];

	m->xcr0 = 0;
	cr4 = getcr4() | CR4Osfxsr|CR4Oxmmex;
	if((m->cpuidcx & (Xsave|Avx)) == (Xsave|Avx) && getconf("*noavx") == nil){
		cr4 |= CR4Oxsave;
		putcr4(cr4);

		m->xcr0 = 7;	/* x87, sse, avx */
		putxcr0(m->xcr0);

		cpuid(0xd, 1, regs);
		if(regs[0] & Xsaves){
			fpsave = fpxsaves;
			fprestore = fpxrestores;
		} else {
			if(regs[0] & Xsaveopt)
				fpsave = fpxsaveopt;
			else
				fpsave = fpxsave;
			fprestore = fpxrestore;
		}
	} else {
		cr4 &= ~CR4Oxsave;
		putcr4(cr4);

		fpsave = fpssesave;
		fprestore = fpsserestore;
	}

	m->fpsave = nil;
	m->fpstate = FPinit;
	_stts();
}

static FPalloc*
fpalloc(FPalloc *link)
{
	FPalloc *a;

	while((a = mallocalign(sizeof(FPalloc), FPalign, 0, 0)) == nil){
		int x = spllo();
		if(up != nil && !waserror()){
			resrcwait("no memory for FPalloc");
			poperror();
		}
		splx(x);
	}
	a->link = link;
	return a;
}

static void
fpfree(FPalloc *a)
{
	free(a);
}

void
fpuprocsetup(Proc *p)
{
	FPalloc *a;

	p->pfpu.fpstate = FPinit;
	while((a = p->pfpu.fpsave) != nil){
		p->pfpu.fpsave = a->link;
		fpfree(a);
	}
}

void
fpuprocfork(Proc *p)
{
	int s;

	s = splhi();
	switch(up->pfpu.fpstate & ~FPnotify){
	case FPprotected:
		_clts();
		/* wet floor */
	case FPactive:
		fpsave(&up->pfpu.fpsave->save);
		up->pfpu.fpstate = FPinactive;
		/* wet floor */
	case FPinactive:
		if(p->pfpu.fpsave == nil)
			p->pfpu.fpsave = fpalloc(nil);
		memmove(p->pfpu.fpsave, up->pfpu.fpsave, sizeof(FPsave));
		p->pfpu.fpstate = FPinactive;
	}
	splx(s);
}

void
fpuprocsave(Proc *p)
{
	if(p->state == Moribund){
		FPalloc *a;

		if(p->pfpu.fpstate == FPactive || p->pfpu.kfpstate == FPactive){
			_fnclex();
			_stts();
		}
		p->pfpu.fpstate = p->pfpu.kfpstate = FPinit;
		while((a = p->pfpu.fpsave) != nil){
			p->pfpu.fpsave = a->link;
			fpfree(a);
		}
		while((a = p->pfpu.kfpsave) != nil){
			p->pfpu.kfpsave = a->link;
			fpfree(a);
		}
		return;
	}
	if(p->pfpu.kfpstate == FPactive){
		fpsave(&p->pfpu.kfpsave->save);
		p->pfpu.kfpstate = FPinactive;
		return;
	}
	if(p->pfpu.fpstate == FPprotected)
		_clts();
	else if(p->pfpu.fpstate != FPactive)
		return;
	fpsave(&p->pfpu.fpsave->save);
	p->pfpu.fpstate = FPinactive;
}

void
fpuprocrestore(Proc*)
{
	/*
	 * when the scheduler switches,
	 * we can discard its fp state.
	 */
	switch(m->fpstate){
	case FPactive:
		_fnclex();
		_stts();
		/* wet floor */
	case FPinactive:
		fpfree(m->fpsave);
		m->fpsave = nil;
		m->fpstate = FPinit;
	}
}

/*
 *  Protect or save FPU state and setup new state
 *  (lazily in the case of user process) for the kernel.
 *  All syscalls, traps and interrupts (except mathemu()!)
 *  are handled between fpukenter() and fpukexit(),
 *  so they can use floating point and vector instructions.
 */
void
fpukenter(Ureg *)
{
	if(up == nil){
		switch(m->fpstate){
		case FPactive:
			fpsave(&m->fpsave->save);
			/* wet floor */
		case FPinactive:
			m->fpstate = FPinit;
		}
		return;
	}

	switch(up->pfpu.fpstate){
	case FPactive:
		up->pfpu.fpstate = FPprotected;
		_stts();
		/* wet floor */
	case FPprotected:
		return;
	}

	switch(up->pfpu.kfpstate){
	case FPactive:
		fpsave(&up->pfpu.kfpsave->save);
		/* wet floor */
	case FPinactive:
		up->pfpu.kfpstate = FPinit;
	}
}

void
fpukexit(Ureg *ureg)
{
	FPalloc *a;

	if(up == nil){
		switch(m->fpstate){
		case FPactive:
			_fnclex();
			_stts();
			/* wet floor */
		case FPinactive:
			a = m->fpsave;
			m->fpsave = a->link;
			fpfree(a);
		}
		m->fpstate = m->fpsave != nil? FPinactive: FPinit;
		return;
	}

	if(up->pfpu.fpstate == FPprotected){
		if(userureg(ureg)){
			up->pfpu.fpstate = FPactive;
			_clts();
		}
		return;
	}

	switch(up->pfpu.kfpstate){
	case FPactive:
		_fnclex();
		_stts();
		/* wet floor */
	case FPinactive:
		a = up->pfpu.kfpsave;
		up->pfpu.kfpsave = a->link;
		fpfree(a);
	}
	up->pfpu.kfpstate = up->pfpu.kfpsave != nil? FPinactive: FPinit;
}

void
fpunotify(Proc *p)
{
	fpuprocsave(p);
	p->pfpu.fpstate |= FPnotify;
}

void
fpunoted(Proc *p)
{
	FPalloc *o;

	if(p->pfpu.fpstate & FPnotify) {
		p->pfpu.fpstate &= ~FPnotify;
	} else if((o = p->pfpu.fpsave->link) != nil) {
		fpfree(p->pfpu.fpsave);
		p->pfpu.fpsave = o;
		p->pfpu.fpstate = FPinactive;
	} else {
		p->pfpu.fpstate = FPinit;
	}
}

FPsave*
notefpsave(Proc *p)
{
	if(p->pfpu.fpsave == nil)
		return nil;
	if(p->pfpu.fpstate == (FPinactive|FPnotify)){
		p->pfpu.fpsave = fpalloc(p->pfpu.fpsave);
		memmove(p->pfpu.fpsave, p->pfpu.fpsave->link, sizeof(FPsave));
		p->pfpu.fpstate = FPinactive;
	}
	return (FPsave*)p->pfpu.fpsave->link;
}

/*
 *  Before restoring the state, check for any pending
 *  exceptions, there's no way to restore the state without
 *  generating an unmasked exception.
 *  More attention should probably be paid here to the
 *  exception masks and error summary.
 */
static int
fpcheck(FPsave *save, int kernel)
{
	ulong status, control;

	status = save->avx.sse.fsw;
	control = save->avx.sse.fcw;
	if((status & ~control) & 0x07F){
		mathnote(status, save->avx.sse.rip, kernel);
		return 1;
	}
	return 0;
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil){
			switch(m->fpstate){
			case FPinit:
				m->fpsave = fpalloc(m->fpsave);
				m->fpstate = FPactive;
				fpinit();
				break;
			case FPinactive:
				fpcheck(&m->fpsave->save, 1);
				fprestore(&m->fpsave->save);
				m->fpstate = FPactive;
				break;
			default:
				panic("floating point error in irq");
			}
			return;
		}

		if(up->pfpu.fpstate == FPprotected){
			_clts();
			fpsave(&up->pfpu.fpsave->save);
			up->pfpu.fpstate = FPinactive;
		}

		switch(up->pfpu.kfpstate){
		case FPinit:
			up->pfpu.kfpsave = fpalloc(up->pfpu.kfpsave);
			up->pfpu.kfpstate = FPactive;
			fpinit();
			break;
		case FPinactive:
			fpcheck(&up->pfpu.kfpsave->save, 1);
			fprestore(&up->pfpu.kfpsave->save);
			up->pfpu.kfpstate = FPactive;
			break;
		default:
			panic("floating point error in trap");
		}
		return;
	}

	switch(up->pfpu.fpstate){
	case FPinit|FPnotify:
		/* wet floor */
	case FPinit:
		if(up->pfpu.fpsave == nil)
			up->pfpu.fpsave = fpalloc(nil);
		up->pfpu.fpstate = FPactive;
		fpinit();
		break;
	case FPinactive|FPnotify:
		spllo();
		qlock(&up->debug);
		notefpsave(up);
		qunlock(&up->debug);
		splhi();
		/* wet floor */
	case FPinactive:
		if(fpcheck(&up->pfpu.fpsave->save, 0))
			break;
		fprestore(&up->pfpu.fpsave->save);
		up->pfpu.fpstate = FPactive;
		break;
	case FPprotected:
		up->pfpu.fpstate = FPactive;
		_clts();
		break;
	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}
