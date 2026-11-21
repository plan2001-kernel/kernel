/*
 * Plan2001-O experimental ARM64 EL0 smoke program image.
 *
 * This is not a userspace product: trap.c copies these bytes into a
 * read-only EL0 executable page and runs them once.  The code is
 * position-independent and touches nothing but its argument (a pointer to
 * the probe-address array in the EL0 data page) and its own registers, so
 * the same bytes are valid at any VA.
 *
 * SVC immediate 0: report that the program is executing at EL0.
 * SVC immediate 1: synchronous round-trip; EL1 returns 0x2001 in x0.
 * SVC immediate 2: exit; x0 = denied-probe mask, x1 = 1 iff the SVC
 *                  return value was 0x2001.  Never returns.
 *
 * Each probe stores its index in x2 before touching memory.  The EL1 trap
 * handler uses that index to match the reported FAR and WnR against the
 * probe it expects, and resumes at the following instruction with the
 * probe's bit set in x0.  A wrong FAR, a translation fault instead of a
 * permission fault, or an unexpected write/read direction is fatal.
 */
	.section .rodata.el0,"a",%progbits
	.align	2
	.global	arm64_el0prog_start
	.global	arm64_el0prog_end
	.type	arm64_el0prog_start,%function
arm64_el0prog_start:
	mov	x19, x0			/* probe address array */
	ldr	x20, [x19, #0]		/* kernel text address */
	ldr	x21, [x19, #8]		/* PL011 MMIO address */

	mov	x0, #1
	svc	#0			/* EL0 entry marker */

	mov	x22, #0			/* denied-probe mask */
	mov	x2, #0
	ldr	x3, [x20]		/* probe 0: read kernel text */
	orr	x22, x22, x0
	mov	x2, #1
	ldr	x3, [x21]		/* probe 1: read device MMIO */
	orr	x22, x22, x0
	mov	x2, #2
	str	x3, [x20]		/* probe 2: write kernel text */
	orr	x22, x22, x0

	mov	x0, #0
	svc	#1			/* EL1 returns 0x2001 in x0 */
	mov	x23, #0x2001
	cmp	x0, x23
	cset	x1, eq
	mov	x0, x22
	svc	#2			/* exit, back to EL1 */
1:	b	1b
arm64_el0prog_end:
