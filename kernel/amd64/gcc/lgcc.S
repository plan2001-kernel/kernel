/*
 * Plan2001-O N9.2 GNU assembler foundation.
 *
 * SysV AMD64 calling convention.  This is a new, separate port; the 9front
 * pc64/l.s remains untouched and is not mixed with these objects.
 *
 * GCC global register variables (dat.h) reserve %r15 = m and %r14 = up.
 * These routines use %r15 only where the 6c original did (spl* saved PC).
 *
 * Implemented here: CPU flags/spl, serialisation, atomics, setlabel/gotolabel,
 * timestamp/port I/O and a few primitives.  Entry/interrupt/syscall frames
 * are not yet ported; see docs/GCC-PORT.md for the explicit missing-symbol
 * table.
 */

	.text

/* void splhi(void) */
	.globl	splhi
	.type	splhi,@function
splhi:
	pushfq
	popq	%rax
	testq	$0x200, %rax
	jz	1f
	movq	(%rsp), %rcx
	movq	%rcx, 8(%r15)		/* m->splpc */
	cli
1:	ret
	.size	splhi, .-splhi

/* void spllo(void) */
	.globl	spllo
	.type	spllo,@function
spllo:
	pushfq
	popq	%rax
	testq	$0x200, %rax
	jnz	1f
	movq	$0, 8(%r15)		/* m->splpc */
	sti
1:	ret
	.size	spllo, .-spllo

/* void splx(int s) */
	.globl	splx
	.type	splx,@function
splx:
	testl	$0x200, %edi
	jnz	spllo
	jmp	splhi
	.size	splx, .-splx

	.globl	spldone
	.type	spldone,@function
spldone:
	ret
	.size	spldone, .-spldone

/* int islo(void) */
	.globl	islo
	.type	islo,@function
islo:
	pushfq
	popq	%rax
	andq	$0x200, %rax
	ret
	.size	islo, .-islo

/* int tas(void *p) */
	.globl	tas
	.globl	_tas
	.type	tas,@function
tas:
_tas:
	movl	$0xdeaddead, %eax
	xchgl	%eax, (%rdi)
	ret
	.size	tas, .-tas

/* int cas(void *p, int exp, int new) */
	.globl	cas
	.globl	cmpswap486
	.type	cas,@function
cas:
cmpswap486:
	movq	%rsi, %rax
	lock cmpxchgq %rdx, (%rdi)
	movl	$0, %eax
	sete	%al
	ret
	.size	cas, .-cas

/* void setlabel(Label *l)  -- Label { uintptr sp; uintptr pc; } */
	.globl	setlabel
	.type	setlabel,@function
setlabel:
	movq	%rsp, 0(%rdi)
	movq	(%rsp), %rax
	movq	%rax, 8(%rdi)
	movq	%rbx, 16(%rdi)
	movq	%rbp, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	xorl	%eax, %eax
	ret
	.size	setlabel, .-setlabel

/* int gotolabel(Label *l) */
	.globl	gotolabel
	.type	gotolabel,@function
gotolabel:
	movq	16(%rdi), %rbx
	movq	24(%rdi), %rbp
	movq	32(%rdi), %r12
	movq	40(%rdi), %r13
	movq	0(%rdi), %rsp
	movq	8(%rdi), %rax
	movq	%rax, (%rsp)
	movl	$1, %eax
	ret
	.size	gotolabel, .-gotolabel

/* Serialisation primitives. */
	.globl	lfence
	.type	lfence,@function
lfence:
	lfence
	ret
	.size	lfence, .-lfence

	.globl	mfence
	.type	mfence,@function
mfence:
	mfence
	ret
	.size	mfence, .-mfence

	.globl	sfence
	.type	sfence,@function
sfence:
	sfence
	ret
	.size	sfence, .-sfence

	.globl	mb386
	.globl	mb586
	.type	mb586,@function
mb586:
mb386:
	xorl	%eax, %eax
	cpuid
	ret
	.size	mb586, .-mb586

/* void _cycles(uvlong *p) -- RDTSC lo/hi */
	.globl	_cycles
	.type	_cycles,@function
_cycles:
	rdtsc
	movl	%eax, 0(%rdi)
	movl	%edx, 4(%rdi)
	ret
	.size	_cycles, .-_cycles

/* int inb(int port) */
	.globl	inb
	.type	inb,@function
inb:
	movl	%edi, %edx
	inb	%dx, %al
	movzbl	%al, %eax
	ret
	.size	inb, .-inb

/* void outb(int port, int value) */
	.globl	outb
	.type	outb,@function
outb:
	movl	%edi, %edx
	movl	%esi, %eax
	outb	%al, %dx
	ret
	.size	outb, .-outb

/* void idle(void) -- park this processor */
	.globl	idle
	.type	idle,@function
idle:
	sti
1:	hlt
	jmp	1b
	.size	idle, .-idle

/* void halt(void) -- original checks nrdy before halting */
	.globl	halt
	.type	halt,@function
halt:
	cli
	cmpl	$0, nrdy(%rip)
	je	1f
	sti
	ret
1:	sti
	hlt
	ret
	.size	halt, .-halt

	.section .note.GNU-stack,"",@progbits
