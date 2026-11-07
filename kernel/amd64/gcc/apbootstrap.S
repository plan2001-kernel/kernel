/*
 * GNU assembler port of pc64/apbootstrap.s.
 * Linked at physical APBOOTSTRAP (0x7000); no KZERO subtraction.
 * The kernel copies these bytes to APBOOTSTRAP before starting an AP.
 */
	.set SELCODE64, 8
	.set SELDATA, 16
	.set SELCODE32, 24
	.set MACHSIZE, 0x4000

	.code16
	.globl	_ap_start
_ap_start:
	.byte	0xea
	.word	_apbootstrap
	.word	0
	nop
	nop
	nop
	.globl	_apvector
_apvector:	.quad 0
	.globl	_appml4
_appml4:	.quad 0
	.globl	_apapic
_apapic:	.quad 0
	.globl	_apmach
_apmach:	.quad 0
	.globl	_apefer
_apefer:	.quad 0x100

_apbootstrap:
	movw	%cs, %ax
	xorw	%ax, %ax
	movw	%ax, %ds
	lgdt	_gdtptr32p
	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	jmp	1f
1:
	.byte	0xea
	.word	_ap32
	.word	SELCODE32

	.code32
_ap32:
	movw	$SELDATA, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	movl	_appml4, %eax
	movl	%eax, %cr3
	jmp	1f
1:
	movl	%cr4, %eax
	andl	$~0x10, %eax
	orl	$0xa0, %eax
	movl	%eax, %cr4
	movl	$0xc0000080, %ecx
	rdmsr
	orl	_apefer, %eax
	wrmsr
	movl	%cr0, %edx
	andl	$~0x6000000a, %edx
	orl	$0x80010000, %edx
	movl	%edx, %cr0
	.byte	0xea
	.long	_ap64
	.word	SELCODE64

	.code64
_ap64:
	lgdt	_gdtptr64v(%rip)
	xorl	%eax, %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	lldt	%ax
	movq	_apmach(%rip), %rsp
	xorl	%r14d, %r14d
	movq	%rsp, %r15
	addq	$MACHSIZE, %rsp
	pushq	%rax
	popfq
	movq	_apvector(%rip), %rax
	movq	_apapic(%rip), %rdi
	call	*%rax
1:	hlt
	jmp	1b

	.align	8
_gdt:
	.quad	0
	.quad	0x00AF9A000000FFFF
	.quad	0x00CF92000000FFFF
	.quad	0x00CF9A000000FFFF
_gdtptr32p:
	.word	31
	.long	_gdt
_gdtptr64v:
	.word	31
	.quad	_gdt

	.section .note.GNU-stack,"",@progbits
