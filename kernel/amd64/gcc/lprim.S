/*
 * Plan2001-O N9.3 GCC kernel: low-level SysV AMD64 primitives.
 * Separate from the untouched Plan 9 pc64/l.s.  No Plan 9 a.out objects.
 */

	.text

/* CPUID(int func, int sub, ulong *regs) */
	.globl	cpuid
	.type	cpuid,@function
cpuid:
	pushq	%rbx
	movq	%rdx, %r10		/* output pointer survives CPUID */
	movl	%edi, %eax
	movl	%esi, %ecx
	cpuid
	movl	%eax, 0(%r10)
	movl	%ebx, 4(%r10)
	movl	%ecx, 8(%r10)
	movl	%edx, 12(%r10)
	popq	%rbx
	ret
	.size	cpuid, .-cpuid

/* void delayloop(int n) -- approximate the original divide loop */
	.globl	delayloop
	.type	delayloop,@function
delayloop:
	pushq	%rbx
	movl	%edi, %ecx
	movl	$1, %ebx
	xorl	%edx, %edx
1:	movl	$-1, %eax
	divl	%ebx
	divl	%ebx
	divl	%ebx
	divl	%ebx
	divl	%ebx
	divl	%ebx
	divl	%ebx
	divl	%ebx
	loop	1b
	popq	%rbx
	ret
	.size	delayloop, .-delayloop

/* void mul64fract(uvlong *r, uvlong a, uvlong b) */
	.globl	mul64fract
	.type	mul64fract,@function
mul64fract:
	movq	%rsi, %rax
	mulq	%rdx
	shrdq	$32, %rdx, %rax
	movq	%rax, (%rdi)
	ret
	.size	mul64fract, .-mul64fract

/* Control registers. */
	.globl	getcr0
getcr0:	movq	%cr0, %rax; ret
	.globl	getcr2
getcr2:	movq	%cr2, %rax; ret
	.globl	getcr3
getcr3:	movq	%cr3, %rax; ret
	.globl	getcr4
getcr4:	movq	%cr4, %rax; ret
	.globl	putcr0
putcr0:	movq	%rdi, %cr0; ret
	.globl	putcr2
putcr2:	movq	%rdi, %cr2; ret
	.globl	putcr3
putcr3:	movq	%rdi, %cr3; ret
	.globl	putcr4
putcr4:	movq	%rdi, %cr4; ret
	.globl	putxcr0
putxcr0:
	xorl	%ecx, %ecx
	movl	%edi, %eax
	shrq	$32, %rdi
	movl	%edi, %edx
	xsetbv
	ret
	.globl	getdr6
getdr6:	movq	%dr6, %rax; ret
	.globl	putdr6
putdr6:	movq	%rdi, %dr6; ret

/* Descriptor tables. */
	.globl	lgdt
lgdt:	lgdt	(%rdi); ret
	.globl	lidt
lidt:	lidt	(%rdi); ret
	.globl	ltr
ltr:	movl	%edi, %eax; ltr	%ax; ret

/* Port I/O not already in lgcc.S. */
	.globl	ins
ins:	movl	%edi, %edx; inw	%dx, %ax; movzwl	%ax, %eax; ret
	.globl	inl
inl:	movl	%edi, %edx; inl	%dx, %eax; ret
	.globl	outs
outs:	movl	%edi, %edx; movl	%esi, %eax; outw	%ax, %dx; ret
	.globl	outl
outl:	movl	%edi, %edx; movl	%esi, %eax; outl	%eax, %dx; ret

	.globl	invlpg
invlpg:	invlpg	(%rdi); ret
	.globl	wbinvd
wbinvd:	wbinvd; ret

/*
 * MSR access with the original fault-recovery marker.  trap() compares the
 * faulting PC with _rdmsrinst/_wrmsrinst, sets BP=-1 and skips the 2-byte
 * instruction.  RBP is saved/restored around the marker.
 */
	.globl	rdmsr
rdmsr:
	pushq	%rbp
	movl	%edi, %ecx
	xorl	%ebp, %ebp
	.globl	_rdmsrinst
_rdmsrinst:
	rdmsr
	movl	%eax, 0(%rsi)
	movl	%edx, 4(%rsi)
	movq	%rbp, %rax
	popq	%rbp
	ret
	.globl	wrmsr
wrmsr:
	pushq	%rbp
	movl	%edi, %ecx
	movl	%esi, %eax
	shrq	$32, %rsi
	movl	%esi, %edx
	xorl	%ebp, %ebp
	.globl	_wrmsrinst
_wrmsrinst:
	wrmsr
	movq	%rbp, %rax
	popq	%rbp
	ret

/* int peek(void *src, void *dst, int cnt) */
	.globl	peek
peek:
	movq	%rdi, %rax
	movq	%rsi, %rdi
	movq	%rax, %rsi
	movl	%edx, %ecx
	cld
	.globl	_peekinst
_peekinst:
	rep movsb
	movl	%ecx, %eax
	ret

/* Debug registers: putdr(u64int d[8]); putdr7(u64int); putdr01236(u64int d[8]). */
	.globl	putdr
putdr:
	movq	56(%rdi), %rax
	movq	%rax, %dr7
	/* fall through */
	.globl	putdr01236
putdr01236:
	movq	0(%rdi), %rax
	movq	%rax, %dr0
	movq	8(%rdi), %rax
	movq	%rax, %dr1
	movq	16(%rdi), %rax
	movq	%rax, %dr2
	movq	24(%rdi), %rax
	movq	%rax, %dr3
	movq	48(%rdi), %rax
	movq	%rax, %dr6
	ret
	.globl	putdr7
putdr7:
	movq	%rdi, %dr7
	ret

/* void rdrandbuf(void *p, ulong n) */
	.globl	rdrandbuf
rdrandbuf:
	testq	%rsi, %rsi
	jz	2f
1:	rdrand	%rax
	jnc	3f
	jmp	4f
3:	xorl	%eax, %eax
4:	cmpq	$8, %rsi
	jb	5f
	movq	%rax, (%rdi)
	addq	$8, %rdi
	subq	$8, %rsi
	ja	1b
	ret
5:	movb	%al, (%rdi)
	incq	%rdi
	shrq	$8, %rax
	decq	%rsi
	jnz	5b
2:	ret

/* void mwait(void *p) */
	.globl	mwait
mwait:
	movq	%rdi, %rax
	movl	(%rax), %ecx
	testl	%ecx, %ecx
	jnz	1f
	xorl	%edx, %edx
	monitor
	movl	(%rax), %ecx
	testl	%ecx, %ecx
	jnz	1f
	xorl	%eax, %eax
	mwait
1:	ret

/* FPU / SSE state. */
	.globl	_clts
_clts:	clts; ret
	.globl	_fldcw
_fldcw:	subq	$8, %rsp; movw	%di, (%rsp); fldcw	(%rsp); addq	$8, %rsp; ret
	.globl	_fnclex
_fnclex: fclex; ret
	.globl	_fninit
_fninit: finit; ret
	.globl	_fwait
_fwait:	fwait; ret
	.globl	_fxrstor
_fxrstor: fxrstor64	(%rdi); ret
	.globl	_fxsave
_fxsave: fxsave64	(%rdi); ret
	.globl	_ldmxcsr
_ldmxcsr: subq	$8, %rsp; movl	%edi, (%rsp); ldmxcsr	(%rsp); addq	$8, %rsp; ret
	.globl	_stts
_stts:
	movq	%cr0, %rax
	orq	$8, %rax
	movq	%rax, %cr0
	ret
	.globl	_xrstor
_xrstor:
	movl	$7, %eax
	xorl	%edx, %edx
	xrstor64	(%rdi)
	ret
	.globl	_xrstors
_xrstors:
	movl	$7, %eax
	xorl	%edx, %edx
	xrstors64	(%rdi)
	ret
	.globl	_xsave
_xsave:
	movl	$7, %eax
	xorl	%edx, %edx
	xsave64	(%rdi)
	ret
	.globl	_xsaveopt
_xsaveopt:
	movl	$7, %eax
	xorl	%edx, %edx
	xsaveopt64	(%rdi)
	ret
	.globl	_xsaves
_xsaves:
	movl	$7, %eax
	xorl	%edx, %edx
	xsaves64	(%rdi)
	ret

	.section .note.GNU-stack,"",@progbits
