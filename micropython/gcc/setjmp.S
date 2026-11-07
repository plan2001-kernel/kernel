/*
 * AMD64 SysV setjmp/longjmp for the freestanding Plan2001-O GCC user
 * build.  The Plan 9 C implementation saves only SP and PC; GCC code
 * keeps live values in RBX/RBP/R12-R15, so those are saved too.
 *
 * jmp_buf layout (amd64/include/u.h, PLAN2001_GCC_USER):
 *   0 SP, 1 PC (return address), 2 RBX, 3 RBP, 4 R12, 5 R13, 6 R14, 7 R15
 * Indexes 0/1 keep the meaning notejmp.c expects.
 */
.text
.globl setjmp
.type setjmp,@function
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
.size setjmp, .-setjmp

.globl longjmp
.type longjmp,@function
longjmp:
	movl	%esi, %eax
	testl	%eax, %eax
	jne	1f
	movl	$1, %eax
1:
	movq	0(%rdi), %rsp
	movq	8(%rdi), %rdx
	movq	16(%rdi), %rbx
	movq	24(%rdi), %rbp
	movq	32(%rdi), %r12
	movq	40(%rdi), %r13
	movq	48(%rdi), %r14
	movq	56(%rdi), %r15
	movq	%rdx, (%rsp)
	ret
.size longjmp, .-longjmp
.section .note.GNU-stack,"",@progbits
