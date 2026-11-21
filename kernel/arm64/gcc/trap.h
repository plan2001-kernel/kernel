/*
 * Plan2001-O experimental ARM64 exception boundary and EL0 test entry.
 *
 * The only EL0 path in the ARM64 boot kernel is a fixed in-tree smoke
 * program: it runs in its own TTBR0 mappings, performs two denied probes
 * and a permitted SVC round-trip, then returns to EL1.  There is no
 * scheduler, no process structure and no syscall ABI behind this file.
 */
#ifndef PLAN2001_ARM64_TRAP_H
#define PLAN2001_ARM64_TRAP_H

/* includers provide u.h first, as every kernel source does */

/*
 * The frame vectors.S builds on every exception (synchronous or IRQ, from
 * EL0 or EL1).  Offsets are part of the assembly contract:
 * x[0..30] at 0..240, sp (SP_EL0) at 248, elr (ELR_EL1) at 256, spsr
 * (SPSR_EL1) at 264; the frame is 272 bytes and 16-byte aligned.
 */
typedef struct Frame Frame;
struct Frame {
	u64int	x[31];	/* x0..x30; x[30] is LR */
	u64int	sp;	/* SP_EL0 of the interrupted context */
	u64int	elr;	/* ELR_EL1 */
	u64int	spsr;	/* SPSR_EL1 */
};

enum {
	Arm64PSRModeEl0t = 0x0,	/* M[3:0]: EL0t */
	Arm64PSRModeEl1h = 0x5,	/* M[3:0]: EL1h */
};

/* C side of the exception vectors (vectors.S). */
void	arm64_trap(Frame*, u64int esr, u64int far);
void	arm64_irq_handler(void);	/* GIC/timer policy lives in main.c */

/* Fatal diagnostic used for every trap this boot kernel does not expect. */
void	arm64_fault(u64int esr, u64int elr, u64int far, u64int spsr);
void	arm64_halt(void);

/*
 * Enter the EL0 smoke program with frame f and continue in the caller
 * after the program's exit SVC.  arm64_el0resume is the label the exit
 * SVC erets to; it is inside the same assembly routine, so the kernel
 * stack and the callee-saved registers survive the EL0 excursion.
 */
void	arm64_enter_el0(Frame *f);
extern char arm64_el0resume[];

/* Build the EL0 mappings, run the smoke program and remove the mappings. */
void	arm64_el0run(void);

/* The EL0 program image copied into its executable page (el0prog.S). */
extern char arm64_el0prog_start[], arm64_el0prog_end[];

#endif
