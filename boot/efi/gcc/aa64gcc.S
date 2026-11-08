/*
 * Plan2001-O ARM64 UEFI loader helpers.
 *
 * jump() enters the kernel with the MMU and caches off, dropping from EL2
 * to EL1 when QEMU's EDK2 leaves us at EL2.  The kernel's physical entry is
 * X0; X1 = 0.  cacheclean()/cachesync() make the bytes the loader wrote
 * visible before that transition.
 */
	.text

	.global jump
jump:
	mov	x19, x0			/* kernel physical entry */
	mov	x20, x1			/* BootInfo physical address */
	msr	daifset, #0xf

	mrs	x0, currentel
	and	x0, x0, #0xc
	cmp	x0, #0x8
	b.ne	1f

	/* EL2 -> EL1 (AArch64), EL1 physical timer enabled */
	mov	x0, #(1 << 31)		/* HCR_EL2.RW */
	msr	hcr_el2, x0
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3		/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr

	ldr	x0, =0x30d00800		/* SCTLR_EL1: MMU/caches off, RES1 */
	msr	sctlr_el1, x0
	isb
	mov	x0, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x0
	mov	x0, x19
	msr	elr_el2, x0
	mov	x0, x20
	mov	x1, xzr
	eret

1:
	/* already EL1: disable MMU/caches and branch to the physical entry */
	ldr	x0, =0x30d00800
	msr	sctlr_el1, x0
	isb
	mov	x0, x20
	mov	x1, xzr
	br	x19

	.global cacheclean
cacheclean:
	cbz	x1, 2f
	mrs	x2, ctr_el0
	ubfx	x2, x2, #16, #4		/* DminLine */
	mov	x3, #4
	lsl	x3, x3, x2		/* bytes per line */
	add	x1, x0, x1
	neg	x4, x3
	and	x0, x0, x4
1:	dc	cvac, x0
	add	x0, x0, x3
	cmp	x0, x1
	b.lo	1b
2:	ret

	.global cachesync
cachesync:
	dsb	sy
	ic	iallu
	dsb	sy
	isb
	ret
