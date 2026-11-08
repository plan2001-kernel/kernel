/*
 * Plan2001-O ARM64 exception vectors.  VBAR_EL1 points here (physical
 * before the MMU is on, high afterwards); the table is 2 KiB aligned and
 * each entry is 128 bytes.  Synchronous exceptions print a diagnostic and
 * halt.  The IRQ entry preserves the caller-saved integer registers and
 * the frame pointer/link register around the C handler; SPSR_EL1 restores
 * NZCV on eret.
 */
	.text
	.align 11
	.global arm64_vectors
arm64_vectors:
	/* current EL with SP0 */
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_irq
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_sync_exc

	/* current EL with SPx */
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_irq
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_sync_exc

	/* lower EL, AArch64 */
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_irq
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_sync_exc

	/* lower EL, AArch32 */
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_irq
	.align 7
	b	arm64_sync_exc
	.align 7
	b	arm64_sync_exc

arm64_sync_exc:
	msr	daifset, #0xf
	mrs	x0, esr_el1
	mrs	x1, elr_el1
	mrs	x2, far_el1
	mrs	x3, spsr_el1
	bl	arm64_fault
	b	arm64_halt

arm64_irq:
	sub	sp, sp, #176
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
	stp	x29, x30, [sp, #160]
	bl	arm64_irq_handler
	ldp	x29, x30, [sp, #160]
	ldp	x18, x19, [sp, #144]
	ldp	x16, x17, [sp, #128]
	ldp	x14, x15, [sp, #112]
	ldp	x12, x13, [sp, #96]
	ldp	x10, x11, [sp, #80]
	ldp	x8, x9, [sp, #64]
	ldp	x6, x7, [sp, #48]
	ldp	x4, x5, [sp, #32]
	ldp	x2, x3, [sp, #16]
	ldp	x0, x1, [sp, #0]
	add	sp, sp, #176
	eret
