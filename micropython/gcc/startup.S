/* Freestanding entry contract for OEXECUTE-loaded GCC user ELF.
 * At entry RAX = kernel Tos*, RSP -> argc, RSP+8 -> argv[0..argc-1], NULL.
 * The kernel stack layout is the existing execo()/execregs() one.
 */
.text
.globl _start
.type _start,@function
_start:
	movq	%rax, _tos(%rip)
	movq	(%rsp), %rdi
	leaq	8(%rsp), %rsi
	andq	$-16, %rsp
	call	p9_start
	ud2
.size _start, .-_start
.data
.globl _tos
_tos:	.quad 0
.globl argv0
argv0:	.quad 0
.section .note.GNU-stack,"",@progbits
