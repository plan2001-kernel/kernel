/*
 * Host setjmp/longjmp for the formatter's Plan 9 error stack.
 *
 * The Plan 9 jmp_buf used by the GE sources is uintptr[8] with
 * PLAN2001_GCC_USER (include/amd64/u.h): SP, PC, then the SysV
 * callee-saved registers.  glibc's jmp_buf has a different layout and
 * must not be mixed with it, so the host formatter provides its own.
 */
	.text
	.globl	setjmp
	.type	setjmp,@function
setjmp:
	movq	%rsp, 0(%rdi)
	movq	(%rsp), %rax
	movq	%rax, 8(%rdi)
	movq	%rbx, 16(%rdi)
	movq	%rbp, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	movq	%r14, 48(%rdi)
	movq	%r15, 56(%rdi)
	xorl	%eax, %eax
	ret

	.globl	longjmp
	.type	longjmp,@function
longjmp:
	movl	%esi, %eax
	testl	%eax, %eax
	jne	1f
	movl	$1, %eax
1:
	movq	16(%rdi), %rbx
	movq	24(%rdi), %rbp
	movq	32(%rdi), %r12
	movq	40(%rdi), %r13
	movq	48(%rdi), %r14
	movq	56(%rdi), %r15
	movq	0(%rdi), %rsp
	movq	8(%rdi), %rdx
	movq	%rdx, (%rsp)
	ret
	.size	setjmp, .-setjmp
	.size	longjmp, .-longjmp
