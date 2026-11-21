/*
 * Plan2001-O ARM64 exception vectors.
 *
 * VBAR_EL1 points here.  The table is 2 KiB aligned and each entry is 128
 * bytes.  Every entry funnels into one of two common paths:
 *
 *   arm64_sync_entry: save the full architectural frame, call arm64_trap
 *                     with ESR_EL1 and FAR_EL1, restore, eret.
 *   arm64_irq_entry:  save the same frame, call the GIC/timer handler,
 *                     restore, eret.
 *
 * The frame is the one described in trap.h: x0..x30, SP_EL0, ELR_EL1 and
 * SPSR_EL1.  Saving all of them, including SP_EL0 and the platform
 * register x18, is required as soon as EL0 can run: a timer IRQ may
 * arrive anywhere in the smoke program, and the previous IRQ path saved
 * only a subset of the caller-saved registers.  The kernel is
 * single-threaded, so the frame is built on the active EL1 stack; an
 * exception from EL0 selects SP_EL1 and never touches the EL0 stack.
 *
 * Synchronous exceptions from EL1 are still fatal (arm64_fault); the only
 * resumed synchronous path is the EL0 SVC/permission-fault dispatch in
 * trap.c.
 */
	.text
	.align	11
	.global	arm64_vectors
arm64_vectors:
	/* current EL with SP0 */
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_irq_entry
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_sync_entry

	/* current EL with SPx */
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_irq_entry
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_sync_entry

	/* lower EL, AArch64 */
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_irq_entry
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_sync_entry

	/* lower EL, AArch32 */
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_irq_entry
	.align	7
	b	arm64_sync_entry
	.align	7
	b	arm64_sync_entry

#define FRAME_SIZE 272

.macro SAVEFRAME
	sub	sp, sp, #FRAME_SIZE
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #240]
	mrs	x0, sp_el0
	str	x0, [sp, #248]
	mrs	x0, elr_el1
	str	x0, [sp, #256]
	mrs	x0, spsr_el1
	str	x0, [sp, #264]
.endm

.macro RESTOREFRAME
	mov	x9, sp
	ldr	x10, [x9, #248]
	msr	sp_el0, x10
	ldr	x10, [x9, #256]
	msr	elr_el1, x10
	ldr	x10, [x9, #264]
	msr	spsr_el1, x10
	ldp	x0, x1, [x9, #0]
	ldp	x2, x3, [x9, #16]
	ldp	x4, x5, [x9, #32]
	ldp	x6, x7, [x9, #48]
	ldr	x8, [x9, #64]
	ldp	x10, x11, [x9, #80]
	ldp	x12, x13, [x9, #96]
	ldp	x14, x15, [x9, #112]
	ldp	x16, x17, [x9, #128]
	ldp	x18, x19, [x9, #144]
	ldp	x20, x21, [x9, #160]
	ldp	x22, x23, [x9, #176]
	ldp	x24, x25, [x9, #192]
	ldp	x26, x27, [x9, #208]
	ldp	x28, x29, [x9, #224]
	ldr	x30, [x9, #240]
	ldr	x9, [x9, #72]
	add	sp, sp, #FRAME_SIZE
.endm

arm64_sync_entry:
	SAVEFRAME
	mov	x0, sp
	mrs	x1, esr_el1
	mrs	x2, far_el1
	bl	arm64_trap
	RESTOREFRAME
	eret

arm64_irq_entry:
	SAVEFRAME
	mov	x0, sp
	bl	arm64_irq_handler
	RESTOREFRAME
	eret
