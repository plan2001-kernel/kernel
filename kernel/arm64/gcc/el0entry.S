/*
 * Plan2001-O experimental ARM64 EL1<->EL0 transition.
 *
 * arm64_enter_el0 loads a Frame prepared by trap.c and erets to EL0.  The
 * exception vectors save the same Frame shape for every trap, so an SVC
 * exit only has to rewrite elr/spsr to arm64_el0resume with EL1h and DAIF
 * masked; the normal restore path then erets back into this routine on the
 * kernel stack.  Callee-saved registers and LR are kept on the kernel
 * stack across the eret, so the C caller sees nothing but a normal call.
 *
 * Frame offsets: x[0..30] 0..240, sp 248, elr 256, spsr 264.
 */
#define FRAME_X9	72
#define FRAME_SP	248
#define FRAME_ELR	256
#define FRAME_SPSR	264

	.text
	.align	2
	.global	arm64_enter_el0
	.type	arm64_enter_el0,%function
arm64_enter_el0:
	sub	sp, sp, #112
	stp	x19, x20, [sp, #0]
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	stp	x25, x26, [sp, #48]
	stp	x27, x28, [sp, #64]
	stp	x29, x30, [sp, #80]
	str	x18, [sp, #96]

	mov	x9, x0			/* frame */
	ldr	x10, [x9, #FRAME_SP]
	msr	sp_el0, x10
	ldr	x10, [x9, #FRAME_ELR]
	msr	elr_el1, x10
	ldr	x10, [x9, #FRAME_SPSR]
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
	ldr	x9, [x9, #FRAME_X9]	/* last: x9 was the base */
	eret

/*
 * The exit SVC changes ELR_EL1 to this label, so the eret above lands
 * here after the EL0 program finished.  Restore the kernel's callee-saved
 * set from the kernel stack and return to arm64_el0run.
 */
	.align	2
	.global	arm64_el0resume
	.type	arm64_el0resume,%function
arm64_el0resume:
	ldp	x19, x20, [sp, #0]
	ldp	x21, x22, [sp, #16]
	ldp	x23, x24, [sp, #32]
	ldp	x25, x26, [sp, #48]
	ldp	x27, x28, [sp, #64]
	ldp	x29, x30, [sp, #80]
	ldr	x18, [sp, #96]
	add	sp, sp, #112
	ret
