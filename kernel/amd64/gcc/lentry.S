/*
 * Plan2001-O N9.3 GCC kernel entry, syscall/interrupt frames and user
 * transitions.  SysV AMD64 C ABI; R15=m and R14=up are reserved by GCC.
 * Original Plan 9 pc64/l.s remains the reference and is not linked here.
 */

	.set KZERO, 0xffffffff80000000
	.set KTZERO, 0xffffffff80110000
	.set CPU0PML4, KZERO+0x13000
	.set CPU0END, KZERO+0x1c000
	.set CPU0MACH, KZERO+0x18000
	.set PTEVALID, 1
	.set PTEWRITE, 2
	.set PTEACCESSED, 0x20
	.set PTEDIRTY, 0x40
	.set PTESIZE, 0x80
	.set PTEGLOBAL, 0x100
	.set PTSZ, 4096
	.set PGLSZ1, (1<<(9+12))
	.set KESEL, 8
	.set UDSEL, 35
	.set UESEL, 43
	.set UTZERO, 0x200000

	.set PML4O0, (((0 >> 39) & 0x1ff) << 3)
	.set PML4OK, (((KZERO >> 39) & 0x1ff) << 3)
	.set PDPO0, (((0 >> 30) & 0x1ff) << 3)
	.set PDPOK, (((KZERO >> 30) & 0x1ff) << 3)
	.set PDPOK1, (((KZERO + (1<<30)) >> 30) & 0x1ff) << 3
	.set PDO0, (((0 >> 21) & 0x1ff) << 3)
	.set PDOK, (((KZERO >> 21) & 0x1ff) << 3)

	.section .text.entry,"ax",@progbits

/* UEFI loader entry.  RDI = BootInfo physical address. */
	.globl	_efi64
	.type	_efi64,@function
_efi64:
	cli
	cld
	movl	$bootinfopa-KZERO, %eax
	movq	%rdi, (%rax)
	movl	$_gdtptr64p-KZERO, %eax
	lgdt	(%rax)
	movw	$16, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	pushq	$KESEL
	movl	$_efi64b-KZERO, %eax
	pushq	%rax
	retfq
	.size	_efi64, .-_efi64

_efi64b:
	movl	$((CPU0END-CPU0PML4)>>2), %ecx
	movl	$(CPU0PML4-KZERO), %esi
	movl	%esi, %edi
	xorl	%eax, %eax
	rep stosl

	movl	%esi, %eax
	movl	%eax, %edx
	addl	$(PTEACCESSED|PTEDIRTY|PTSZ|PTEWRITE|PTEVALID), %edx
	movl	%edx, PML4O0(%eax)
	movl	%edx, PML4OK(%eax)

	addl	$PTSZ, %eax
	addl	$PTSZ, %edx
	movl	%edx, PDPO0(%eax)
	movl	%edx, PDPOK(%eax)

	addl	$PTSZ, %edx
	movl	%edx, PDPOK1(%eax)

	addl	$PTSZ, %eax
	movl	$(PTEACCESSED|PTEDIRTY|PTESIZE|PTEGLOBAL|PTEWRITE|PTEVALID), %edx
	movl	%edx, PDO0(%eax)

	addl	$PDOK, %eax
	movl	$end-KZERO, %ecx
	addl	$(PGLSZ1-1), %ecx
	andl	$~(PGLSZ1-1), %ecx
	shrl	$(9+12), %ecx
1:	movl	%edx, (%eax)
	addl	$PGLSZ1, %edx
	addl	$8, %eax
	loop	1b

	movq	%rsi, %cr3
	jmp	2f
2:	movq	%cr4, %rax
	andl	$~0x10, %eax
	orl	$0xa0, %eax
	movq	%rax, %cr4
	movq	%cr0, %rdx
	andl	$~0x6000000a, %edx
	orl	$0x80010000, %edx
	movq	%rdx, %cr0
	movabsq	$_start64v, %rax
	jmp	*%rax

	.align	8
_gdt:
	.quad	0
	.quad	0x00AF9A000000FFFF
	.quad	0x00CF92000000FFFF
	.quad	0x00CF9A000000FFFF
_gdtptr64p:
	.word	31
	.quad	_gdt-KZERO
_gdtptr64v:
	.word	31
	.quad	_gdt-KZERO

_start64v:
	lgdt	_gdtptr64v(%rip)
	xorl	%eax, %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	lldt	%ax
	movabsq	$(CPU0MACH+0x4000), %rsp
	movabsq	$CPU0MACH, %r15
	xorl	%r14d, %r14d
	movabsq	$edata, %rdi
	movabsq	$end, %rcx
	subq	%rdi, %rcx
	shrq	$2, %rcx
	cld
	xorl	%eax, %eax
	rep stosl
	/* MemMin is BSS and was just cleared; set the same 2MiB-rounded
	 * physical end used by the page-table loop. */
	movl	$end-KZERO, %eax
	addl	$(PGLSZ1-1), %eax
	andl	$~(PGLSZ1-1), %eax
	movl	%eax, MemMin(%rip)
	xorl	%eax, %eax
	pushq	%rax
	popfq
	call	main
3:	sti
	hlt
	jmp	3b

	.text

/* SYSCALL entry.  User stack is in RSP; m is in GS:0 after swapgs. */
	.globl	syscallentry
	.type	syscallentry,@function
syscallentry:
	swapgs
	movq	%gs:0, %rax
	movq	%r13, %r8		/* preserve user R13 */
	movq	%rbx, %r9		/* preserve user RBX */
	movq	16(%rax), %rbx
	movq	%rsp, %r13
	movq	%rbx, %rsp
	pushq	$UDSEL
	pushq	%r13
	pushq	%r11
	pushq	$UESEL
	pushq	%rcx
	pushq	$0
	pushq	$64
	subq	$(8 + 23*8 - 7*8), %rsp
	movq	%r15, (15*8)(%rsp)
	movq	%r14, (14*8)(%rsp)
	movq	%r9, (2*8)(%rsp)	/* Ureg.bx */
	movq	%rbp, (8*8)(%rsp)	/* user RBP in Ureg.r8 slot */
	movq	%r12, (12*8)(%rsp)	/* Ureg.r12 */
	movq	%r8, (13*8)(%rsp)	/* Ureg.r13 */
	movq	%rbp, (7*8)(%rsp)	/* 6c a.out ABI: syscall number in BP */
	movq	%rax, %r15
	movq	%rbx, %r14
	leaq	8(%rsp), %rdi
	call	syscall
	testl	%eax, %eax
	jne	noteret		/* note entry/return: restore all registers */

	.globl	forkret
	.type	forkret,@function
forkret:
	swapgs
	movq	8(%rsp), %rax
	movq	(15*8)(%rsp), %r15
	movq	(14*8)(%rsp), %r14
	movq	(2*8)(%rsp), %rbx
	movq	(8*8)(%rsp), %rbp
	movq	(12*8)(%rsp), %r12
	movq	(13*8)(%rsp), %r13
	movq	(19*8)(%rsp), %rcx
	movq	(21*8)(%rsp), %r11
	movq	(22*8)(%rsp), %rsp
	xorl	%edx, %edx
	xorl	%esi, %esi
	xorl	%edi, %edi
	xorl	%r8d, %r8d
	xorl	%r9d, %r9d
	xorl	%r10d, %r10d
	sysretq

/* Enter user mode.  RDI = user stack pointer. */
	.globl	touser
	.type	touser,@function
touser:
	swapgs
	movl	$0, %r15d
	movl	$0, %r14d
	movq	$(UTZERO+0x28), %rcx
	movl	$0x200, %r11d
	movq	%rdi, %rsp
	sysretq

/* RDI = struct { uintptr sp; uintptr arg; } ; arg -> user RDI */
	.globl	otouser
	.type	otouser,@function
otouser:
	movq	%rdi, %rax
	movq	0(%rax), %rbx
	movq	8(%rax), %rdx
	swapgs
	movl	$0, %r15d
	movl	$0, %r14d
	movq	$(UTZERO+0x28), %rcx
	movl	$0x200, %r11d
	movq	%rbx, %rsp
	movq	%rdx, %rdi
	sysretq

/* Interrupt/exception common frame. */
	.globl	_strayintr
_strayintr:
	pushq	%rax
	movq	8(%rsp), %rax
	jmp	_intrcommon
	.globl	_strayintrx
_strayintrx:
	xchgq	%rax, (%rsp)
_intrcommon:
	movzbl	(%rax), %eax
	xchgq	%rax, (%rsp)
	subq	$24, %rsp
	cmpw	$KESEL, 48(%rsp)
	je	1f
	movq	%r14, 0(%rsp)
	movq	%r15, 8(%rsp)
	swapgs
	movq	%gs:0, %r15
	movq	16(%r15), %r14
1:	pushq	%r13
	pushq	%r12
	pushq	%r11
	pushq	%r10
	pushq	%r9
	pushq	%r8
	pushq	%rbp
	pushq	%rdi
	pushq	%rsi
	pushq	%rdx
	pushq	%rcx
	pushq	%rbx
	pushq	%rax
	movq	%rsp, %rbp
	movq	%rsp, %rdi
	andq	$-16, %rsp
	call	trap
	movq	%rbp, %rsp
	subq	$8, %rsp		/* dummy/ureg[-1] slot expected by noteret */

	.globl	noteret
noteret:
	popq	%rax
	popq	%rax
	popq	%rbx
	popq	%rcx
	popq	%rdx
	popq	%rsi
	popq	%rdi
	popq	%rbp
	popq	%r8
	popq	%r9
	popq	%r10
	popq	%r11
	popq	%r12
	popq	%r13
	cmpw	$KESEL, 48(%rsp)
	je	2f
	swapgs
	movq	8(%rsp), %r15
	movq	0(%rsp), %r14
2:	addq	$40, %rsp
	iretq

	.globl	vectortable
vectortable:
	.set	vecno, 0
	.rept	256
	.if	(vecno == 8) || (vecno >= 10 && vecno <= 14) || (vecno == 17)
	call	_strayintrx
	.else
	call	_strayintr
	.endif
	.byte	vecno
	.set	vecno, vecno+1
	.endr

	.data
	.globl	bootinfopa
bootinfopa:
	.quad	0

	.section .note.GNU-stack,"",@progbits
