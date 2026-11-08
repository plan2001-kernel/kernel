/*
 * GNU assembler port of pc64/rebootcode.s.
 * Linked at physical REBOOTADDR (0x11000).  The kernel's legacy kexec path
 * currently refuses before using this, but the real blob is generated and
 * retained for the future 64-bit handoff.
 */
	.set SELCODE64, 8
	.set SELDATA, 16
	.set SELCODE32, 24

	.code64
	.globl	_reboot_entry
_reboot_entry:
	movl	%edi, %edi		/* destination physical */
	movl	%esi, %esi		/* source physical */
	movl	%edx, %ebx		/* byte count */
	movl	$_idtptr64p, %eax
	lidt	(%rax)
	movl	$_gdtptr64p, %eax
	lgdt	(%rax)
	pushq	$SELCODE32
	pushq	$_warp32
	lretq

	.code32
_warp32:
	movl	$SELDATA, %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	movl	%cr0, %eax
	andl	$0x7fffffff, %eax
	movl	%eax, %cr0
	xorl	%eax, %eax
	movl	%eax, %cr3
	movl	$0xc0000080, %ecx
	rdmsr
	andl	$0xfffffeff, %eax
	wrmsr
	movl	%cr4, %eax
	andl	$0xffffff5f, %eax
	movl	%eax, %cr4
	movl	%ebx, %ecx
	movl	%edi, %eax
	movl	%eax, %esp
	testl	%eax, %eax
	jz	2f
	cmpl	%edi, %esi
	jg	1f
	movl	%esi, %edx
	addl	%ecx, %edx
	cmpl	%edi, %edx
	jg	3f
1:	cld
	rep movsb
4:	jmp	*%eax
3:	addl	%ecx, %edi
	addl	%ecx, %esi
	subl	$1, %edi
	subl	$1, %esi
	std
	rep movsb
	jmp	4b
2:	hlt
	jmp	2b

	.align	8
_gdt:
	.quad	0
	.quad	0x00AF9A000000FFFF
	.quad	0x00CF92000000FFFF
	.quad	0x00CF9A000000FFFF
_gdtptr64p:
	.word	31
	.quad	_gdt
_idtptr64p:
	.word	0
	.quad	0

	.section .note.GNU-stack,"",@progbits
