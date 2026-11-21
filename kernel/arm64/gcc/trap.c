/*
 * Plan2001-O experimental ARM64 exception boundary: the first controlled
 * EL0 execution and SVC round-trip on the ARM64 boot kernel.
 *
 * This is a bounded architecture milestone, not a userspace port.  The
 * one EL0 program is the in-tree smoke image in el0prog.S; there is no
 * scheduler, process structure, syscall table or public ABI here.
 *
 * Memory layout while the smoke program runs (TTBR0, 39-bit VA, 4 KiB
 * granule, level-1 root):
 *
 *   0x00400000  code   read-only and executable at EL0 (AP=11, UXN=0, PXN=1)
 *   0x00401000  data   read/write at EL0 (AP=01, UXN=1, PXN=1)
 *   0x00402000  stack  read/write at EL0, SP starts at 0x00403000
 *
 * Only those three 4 KiB pages are mapped in the 0x00400000-0x005fffff
 * 2 MiB slot; every other L3 entry in the slot, and every other TTBR0
 * slot, is invalid, so EL0 gets translation faults outside its three
 * pages.  TTBR1 keeps the kernel high map, which entry.S builds with
 * AP=00: read/write at EL1 and no access at EL0, so EL0 cannot reach
 * kernel text, kernel data, boot page tables or the PL011/GIC MMIO pages
 * even though EL1 uses them normally.
 *
 * The three probes in the smoke program therefore prove isolation, not
 * just that some mapping exists: probe 0 reads kernel text, probe 1 reads
 * the PL011 MMIO page and probe 2 writes kernel text.  Each must fail
 * with a permission fault (DFSC 0b0011xx) at the expected FAR and with
 * the expected WnR direction; the handler resumes after the faulting
 * instruction with that probe's bit returned in x0.  A translation fault,
 * a different address or an unexpected trap is fatal, never a PASS.
 */
#include "u.h"
#include "bootmem.h"
#include "console.h"
#include "trap.h"

enum {
	EcSvcA64	= 0x15,	/* SVC instruction from AArch64 */
	EcDataAbortLower = 0x24,/* data abort taken from a lower EL */

	DfscPermissionMask = 0x3c,	/* DFSC[5:2] */
	DfscPermission	= 0x0c,	/* 0b0011xx: permission fault */

	El0ProbeCount	= 3,
	El0ProbeMask	= (1 << El0ProbeCount) - 1,
	El0SvcMagic	= 0x2001,

	/* one 2 MiB L2 slot at 0x00400000, pages at its start */
	El0Base		= 0x00400000,
	El0L2Index	= (El0Base >> 21) & 0x1ff,
	El0Code		= El0Base,
	El0Data		= El0Base + PageSize,
	El0StackTop	= El0Base + 3*PageSize,
};

/*
 * 4 KiB stage-1 descriptors.  MAIR attr0 is Normal-WB, attr1 is Device.
 * A table and a last-level page both use type 0b11; blocks use 0b01 and
 * are only valid at levels 1 and 2, so a level-3 leaf must be DESC_PAGE.
 */
#define DESC_TABLE	3ULL
#define DESC_PAGE	3ULL
#define DESC_SH_INNER	(3ULL << 8)
#define DESC_AF		(1ULL << 10)
#define DESC_NG		(1ULL << 11)	/* per-ASID mapping, as EL0 wants */
#define DESC_AP_RO	(3ULL << 6)	/* AP=11: RO at EL0 and EL1 */
#define DESC_AP_RW	(1ULL << 6)	/* AP=01: RW at EL0 and EL1 */
#define DESC_PXN	(1ULL << 53)	/* EL1 may not execute */
#define DESC_UXN	(1ULL << 54)	/* EL0 may not execute */

#define DESC_EL0_CODE	(DESC_PAGE | DESC_AP_RO | DESC_SH_INNER | \
			 DESC_AF | DESC_NG | DESC_PXN)
#define DESC_EL0_DATA	(DESC_PAGE | DESC_AP_RW | DESC_SH_INNER | \
			 DESC_AF | DESC_NG | DESC_PXN | DESC_UXN)

static u64int probes[El0ProbeCount];
static int el0done;

static void
bug(const char *why)
{
	uartputs("PLAN2001-O: ARM64 EL0 FAULT: ");
	uartputs(why);
	uartputc('\n');
	arm64_halt();
}

void
arm64_fault(u64int esr, u64int elr, u64int far, u64int spsr)
{
	uartputs("\nPLAN2001-O: ARM64 FAULT\n");
	uartputs("  esr="), uarthex(esr), uartputc('\n');
	uartputs("  elr="), uarthex(elr), uartputc('\n');
	uartputs("  far="), uarthex(far), uartputc('\n');
	uartputs("  spsr="), uarthex(spsr), uartputc('\n');
	arm64_halt();
}

static void
el0dataabort(Frame *f, u64int esr, u64int far)
{
	u64int idx, wnr;

	if(f->elr < El0Code || f->elr >= El0Code + PageSize)
		bug("faulting instruction is outside the EL0 code page");
	idx = f->x[2];
	if(idx >= El0ProbeCount)
		bug("probe index out of range");
	if(probes[idx] != far)
		bug("probe address mismatch");
	if((esr & DfscPermissionMask) != DfscPermission)
		bug("expected a permission fault, not a translation fault");
	wnr = (esr >> 6) & 1;
	if(wnr != (idx == 2))
		bug("probe read/write direction mismatch");

	uartputs("PLAN2001-O: ARM64 EL0: probe ");
	uartdec(idx);
	uartputs(" denied by permission fault\n");
	uartputs("PLAN2001-O: ARM64 EL0: probe ");
	uartdec(idx);
	uartputs(" evidence esr=");
	uarthex(esr);
	uartputs(" far=");
	uarthex(far);
	uartputc('\n');

	f->x[0] = 1ULL << idx;
	f->elr += 4;		/* AArch64 instructions are fixed 4 bytes */
}

static void
el0svc(Frame *f, u64int imm)
{
	switch(imm){
	case 0:
		if(f->elr < El0Code || f->elr >= El0Code + PageSize)
			bug("entry SVC outside the EL0 code page");
		if(f->sp != El0StackTop)
			bug("entry SVC with an unexpected user stack");
		uartputs("PLAN2001-O: ARM64 EL0: entered at EL0\n");
		uartputs("PLAN2001-O: ARM64 EL0: frame elr=");
		uarthex(f->elr);
		uartputs(" sp=");
		uarthex(f->sp);
		uartputc('\n');
		return;
	case 1:
		uartputs("PLAN2001-O: ARM64 EL0: SVC handled in EL1\n");
		f->x[0] = El0SvcMagic;
		return;
	case 2:
		if(f->x[1] != 1)
			bug("EL0 did not accept the SVC return value");
		if(f->x[0] != El0ProbeMask)
			bug("EL0 did not catch all denied probes");
		uartputs("PLAN2001-O: ARM64 EL0: return value 0x2001 accepted in EL0\n");
		uartputs("PLAN2001-O: ARM64 EL0: three isolation probes denied\n");
		uartputs("PLAN2001-O: ARM64 EL0: smoke program completed\n");
		el0done = 1;
		f->elr = (u64int)(uintptr)arm64_el0resume;
		/* EL1h with DAIF masked; arm64_el0run re-enables IRQs */
		f->spsr = 0x3c0 | Arm64PSRModeEl1h;
		return;
	default:
		bug("unknown EL0 SVC immediate");
	}
}

void
arm64_trap(Frame *f, u64int esr, u64int far)
{
	u64int ec;

	if((f->spsr & 0xf) != Arm64PSRModeEl0t){
		/* kernel faults stay fatal; there is no recovery in early boot */
		arm64_fault(esr, f->elr, far, f->spsr);
		return;
	}
	ec = esr >> 26;
	switch(ec){
	case EcSvcA64:
		el0svc(f, esr & 0xffff);
		break;
	case EcDataAbortLower:
		el0dataabort(f, esr, far);
		break;
	default:
		arm64_fault(esr, f->elr, far, f->spsr);
		break;
	}
}

static void
el0mapenable(u64int l1pa)
{
	u64int tcr;

	__asm__ volatile("msr ttbr0_el1, %0" : : "r"(l1pa) : "memory");
	__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
	tcr &= ~(1ULL << 7);	/* EPD0 = 0: walk the low half */
	__asm__ volatile("msr tcr_el1, %0" : : "r"(tcr) : "memory");
	__asm__ volatile("tlbi vmalle1\n\tdsb sy\n\tisb" : : : "memory");
}

static void
el0mapdisable(void)
{
	u64int tcr;

	__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
	tcr |= 1ULL << 7;	/* EPD0 = 1: no low-half walks at all */
	__asm__ volatile("msr tcr_el1, %0" : : "r"(tcr) : "memory");
	__asm__ volatile("msr ttbr0_el1, xzr\n\ttlbi vmalle1\n\tdsb sy\n\tisb"
	    : : : "memory");
	__asm__ volatile("msr sp_el0, xzr" : : : "memory");
}

/*
 * The code page was written through the EL1 cacheable high map, so clean
 * those lines to the point of coherency and drop the instruction cache
 * before EL0 fetches the program.
 */
static void
flushcode(u64int pa)
{
	u64int a, e, ctr, line;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
	line = 4ULL << ((ctr >> 16) & 0xf);
	a = pa + KZERO;
	e = a + PageSize;
	for(; a < e; a += line)
		__asm__ volatile("dc civac, %0" : : "r"(a) : "memory");
	__asm__ volatile("dsb sy\n\tic iallu\n\tdsb sy\n\tisb" : : : "memory");
}

void
arm64_el0run(void)
{
	Frame f;
	volatile u64int *l1, *l2, *l3;
	u64int *fp;
	u64int l1pa, l2pa, l3pa, codepa, datapa, stackpa, n, i;
	char *dst;

	/*
	 * All six pages come from the boot allocator, so they lie in
	 * RAMBASE..RAMEND and outside the kernel image and BootInfo blob
	 * that arm64meminit already reserved.  They are returned when the
	 * test ends and TTBR0 is disabled again.
	 */
	l1pa = arm64pagealloc();
	l2pa = arm64pagealloc();
	l3pa = arm64pagealloc();
	codepa = arm64pagealloc();
	datapa = arm64pagealloc();
	stackpa = arm64pagealloc();
	if(l1pa == 0 || l2pa == 0 || l3pa == 0 || codepa == 0
	|| datapa == 0 || stackpa == 0)
		bug("no free pages for the EL0 mappings");

	l1 = (volatile u64int*)(uintptr)(l1pa + KZERO);
	l2 = (volatile u64int*)(uintptr)(l2pa + KZERO);
	l3 = (volatile u64int*)(uintptr)(l3pa + KZERO);
	l1[0] = l2pa | DESC_TABLE;
	l2[El0L2Index] = l3pa | DESC_TABLE;
	l3[0] = codepa | DESC_EL0_CODE;
	l3[1] = datapa | DESC_EL0_DATA;
	l3[2] = stackpa | DESC_EL0_DATA;

	dst = (char*)(uintptr)(codepa + KZERO);
	n = (u64int)(arm64_el0prog_end - arm64_el0prog_start);
	if(n == 0 || n > PageSize)
		bug("EL0 program does not fit its executable page");
	for(i = 0; i < n; i++)
		dst[i] = arm64_el0prog_start[i];
	flushcode(codepa);

	/*
	 * Probe addresses handed to EL0 in its data page.  Probes 0 and 2
	 * use this kernel text address (TTBR1, AP=00); probe 1 is the
	 * PL011 MMIO page.  All three are mapped for EL1 and denied to EL0.
	 */
	probes[0] = (u64int)(uintptr)arm64_el0run;
	probes[1] = KZERO + 0x09000000ULL;
	probes[2] = probes[0];
	((volatile u64int*)(uintptr)(datapa + KZERO))[0] = probes[0];
	((volatile u64int*)(uintptr)(datapa + KZERO))[1] = probes[1];
	__asm__ volatile("dsb sy" : : : "memory");

	el0mapenable(l1pa);

	fp = (u64int*)(uintptr)&f;
	for(i = 0; i < sizeof(Frame)/sizeof(u64int); i++)
		fp[i] = 0;
	f.x[0] = El0Data;		/* probe address array */
	f.x[2] = 0;			/* first probe index */
	f.sp = El0StackTop;
	f.elr = El0Code;
	f.spsr = 0;			/* EL0t, DAIF clear: IRQs stay live */
	arm64_enter_el0(&f);

	if(!el0done)
		bug("EL0 returned without completing the smoke program");
	uartputs("PLAN2001-O: ARM64 EL0: returned to EL1\n");
	el0mapdisable();
	uartputs("PLAN2001-O: ARM64 EL0: user mappings removed\n");
	if(!arm64pagefree(l3pa) || !arm64pagefree(l2pa)
	|| !arm64pagefree(l1pa) || !arm64pagefree(codepa)
	|| !arm64pagefree(datapa) || !arm64pagefree(stackpa))
		bug("could not free the EL0 pages");
	__asm__ volatile("msr daifclr, #2" : : : "memory");
}
