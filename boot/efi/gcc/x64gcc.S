/*
 * Microsoft x64 ABI replacements for the Plan 9 x64.s start/reloc/eficall/
 * jump64 helpers.  PE32+ relocations are handled by firmware, so start/reloc
 * are not needed; eficall is in efi-gcc.c.
 */
	.text

/* void jump64(void *pc, void *bootinfo) */
	.globl	jump64
jump64:
	movq	%rdx, %rdi		/* BootInfo physical pointer */
	xorl	%esi, %esi
	cli
	leaq	idtptr(%rip), %rax
	lidt	(%rax)
	jmp	*%rcx

	.globl	getcr3
getcr3:
	movq	%cr3, %rax
	ret

	.globl	getcr4
getcr4:
	movq	%cr4, %rax
	ret

	.globl	rdtsc
rdtsc:
	rdtsc
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret

	.data
	.align	8
idtptr:
	.word	0
	.quad	0
