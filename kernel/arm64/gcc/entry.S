/*
 * Plan2001-O experimental ARM64 boot entry.
 *
 * The UEFI loader enters at the kernel's physical address with the MMU and
 * caches off, X0 = BootInfo physical address, X1 = 0 (Plan2001 Boot ABI v1,
 * ARM64).  We may still be at EL2 (QEMU virt's EDK2 runs there), so the
 * first job is to drop to EL1.  Then build a 2 GiB identity map (TTBR0) and
 * a 2 GiB high map at KZERO (TTBR1), turn the MMU on, and continue at the
 * high address.
 *
 * The map covers physical 0..2 GiB as 2 MiB Normal-WB blocks, except
 * 0x08000000..0x0A000000 (GIC, PL011, RTC) which are Device-nGnRnE.  QEMU
 * virt's RAM starts at 0x40000000, so the kernel at 0x40100000 and the
 * BootInfo blob are inside it.
 */
	.section .text.entry,"ax",%progbits
	.global _efi64
	.type _efi64,%function

#define STACKSZ 16384

	.equ SCTLR_EL1_RESET, 0x30d00800
	.equ SCTLR_EL1_ON,    0x30d01805	/* RES1 | M | C | I */
	.equ TCR_EL1_VALUE,   (25) | (25<<16) | (1<<8) | (1<<10) | (3<<12) \
			    | (1<<24) | (1<<26) | (3<<28) | (1<<30) | (2<<32)

_efi64:
	msr	daifset, #0xf
	mov	x19, x0			/* BootInfo physical address */

	mrs	x0, currentel
	and	x0, x0, #0xc
	cmp	x0, #0x8
	b.eq	el2drop
	cmp	x0, #0x4
	b.eq	el1entry
	b	arm64_halt_early

el2drop:
	/* EL1 is AArch64, and the physical timer is usable from EL1 */
	mov	x0, #(1 << 31)		/* HCR_EL2.RW */
	msr	hcr_el2, x0
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3		/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr

	ldr	x0, =SCTLR_EL1_RESET	/* MMU and caches off, RES1 set */
	msr	sctlr_el1, x0
	isb

	mov	x0, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x0
	adr	x0, el1entry
	msr	elr_el2, x0
	mov	x0, x19
	mov	x1, xzr
	eret

el1entry:
	/* boot stack, addressed physically: the MMU is still off */
	adrp	x0, boot_stack
	add	x0, x0, :lo12:boot_stack
	add	x0, x0, #STACKSZ
	mov	sp, x0

	/* early faults before the high map exists still have a vector */
	adrp	x0, arm64_vectors
	add	x0, x0, :lo12:arm64_vectors
	msr	vbar_el1, x0
	isb

	/* zero .bss physically (it also holds the boot page tables) */
	adrp	x0, __bss_start
	add	x0, x0, :lo12:__bss_start
	adrp	x1, __bss_end
	add	x1, x1, :lo12:__bss_end
1:	cmp	x0, x1
	b.hs	2f
	str	xzr, [x0], #8
	b	1b
2:

	/*
	 * T0SZ/T1SZ = 25 (39-bit VA) with 4 KiB granules makes the hardware
	 * root a level-1 table: each entry covers 1 GiB.  The low root gets
	 * entries 0 and 1, the high root entries 510 (KZERO) and 511.
	 */
	adrp	x0, boot_l1_low
	add	x0, x0, :lo12:boot_l1_low
	adrp	x2, boot_l2_0
	add	x2, x2, :lo12:boot_l2_0
	orr	x3, x2, #3
	str	x3, [x0]			/* low L1[0] -> phys 0..1 GiB */
	adrp	x2, boot_l2_1
	add	x2, x2, :lo12:boot_l2_1
	orr	x3, x2, #3
	str	x3, [x0, #8]			/* low L1[1] -> phys 1..2 GiB */

	adrp	x0, boot_l1_high
	add	x0, x0, :lo12:boot_l1_high
	adrp	x2, boot_l2_0
	add	x2, x2, :lo12:boot_l2_0
	orr	x3, x2, #3
	mov	x4, #510			/* KZERO is level-1 index 510 */
	str	x3, [x0, x4, lsl #3]
	adrp	x2, boot_l2_1
	add	x2, x2, :lo12:boot_l2_1
	orr	x3, x2, #3
	add	x4, x4, #1
	str	x3, [x0, x4, lsl #3]

	/* L2_0: 512 2 MiB Normal blocks covering physical 0..1 GiB */
	adrp	x0, boot_l2_0
	add	x0, x0, :lo12:boot_l2_0
	mov	x3, #0
	mov	x4, #0
	mov	x6, #1
	lsl	x6, x6, #54			/* UXN: never executable at EL0 */
3:	orr	x5, x3, x6
	add	x5, x5, #0x701			/* valid, inner-shareable, AF, Normal */
	str	x5, [x0, x4, lsl #3]
	add	x3, x3, #0x200000
	add	x4, x4, #1
	cmp	x4, #512
	b.ne	3b

	/* physical 0x08000000..0x0A000000: GIC, PL011, RTC are Device-nGnRnE */
	mov	x4, #64
	ldr	x7, =0x08000000
4:	orr	x5, x7, x6
	add	x5, x5, #0x705			/* valid, Device attr index 1, AF */
	str	x5, [x0, x4, lsl #3]
	add	x7, x7, #0x200000
	add	x4, x4, #1
	cmp	x4, #80
	b.ne	4b

	/* L2_1: 512 2 MiB Normal blocks covering physical 1..2 GiB */
	adrp	x0, boot_l2_1
	add	x0, x0, :lo12:boot_l2_1
	ldr	x3, =0x40000000
	mov	x4, #0
5:	orr	x5, x3, x6
	add	x5, x5, #0x701
	str	x5, [x0, x4, lsl #3]
	add	x3, x3, #0x200000
	add	x4, x4, #1
	cmp	x4, #512
	b.ne	5b

	/*
	 * The loader wrote .bss zeroes through its caches.  Those clean (but
	 * present) lines would shadow the page tables we just wrote with the
	 * MMU off, so invalidate the whole .bss range to the point of
	 * coherency before enabling the data cache.
	 */
	adrp	x0, __bss_start
	add	x0, x0, :lo12:__bss_start
	adrp	x1, __bss_end
	add	x1, x1, :lo12:__bss_end
	mrs	x2, ctr_el0
	ubfx	x2, x2, #16, #4		/* DminLine */
	mov	x3, #4
	lsl	x3, x3, x2		/* bytes per line */
	neg	x4, x3
	and	x0, x0, x4
6:	cmp	x0, x1
	b.hs	7f
	dc	ivac, x0
	add	x0, x0, x3
	b	6b
7:	dsb	sy
	ic	iallu
	dsb	sy
	isb

	/* MAIR attr0 = Normal WB, attr1 = Device-nGnRnE */
	ldr	x0, =0xff
	msr	mair_el1, x0
	ldr	x0, =TCR_EL1_VALUE
	msr	tcr_el1, x0
	adrp	x0, boot_l1_low
	add	x0, x0, :lo12:boot_l1_low
	msr	ttbr0_el1, x0
	adrp	x0, boot_l1_high
	add	x0, x0, :lo12:boot_l1_high
	msr	ttbr1_el1, x0
	dsb	sy
	isb

	mrs	x0, sctlr_el1
	ldr	x1, =0x02080002		/* WXN | EE | A */
	bic	x0, x0, x1
	ldr	x1, =SCTLR_EL1_ON
	orr	x0, x0, x1
	msr	sctlr_el1, x0
	isb

	/* continue at the high address */
	ldr	x0, =0xffffffff80000000
	adrp	x1, arm64_high_entry
	add	x1, x1, :lo12:arm64_high_entry
	add	x0, x0, x1
	br	x0

arm64_high_entry:
	adrp	x1, boot_stack
	add	x1, x1, :lo12:boot_stack
	add	x1, x1, #STACKSZ
	mov	sp, x1

	adrp	x1, arm64_vectors
	add	x1, x1, :lo12:arm64_vectors
	msr	vbar_el1, x1
	isb

	/* no low mappings remain in the kernel */
	mrs	x0, tcr_el1
	orr	x0, x0, #(1 << 7)	/* EPD0 */
	msr	tcr_el1, x0
	msr	ttbr0_el1, xzr
	tlbi	vmalle1
	dsb	sy
	isb

	mov	x0, x19
	b	arm64_main
	b	arm64_halt

arm64_halt_early:
	msr	daifset, #0xf
	.global arm64_halt
arm64_halt:
	wfi
	b	arm64_halt

	.section .bss,"aw",%nobits
	.align 12
boot_l1_low:
	.space 4096
boot_l1_high:
	.space 4096
boot_l2_0:
	.space 4096
boot_l2_1:
	.space 4096
	.align 4
boot_stack:
	.space STACKSZ
