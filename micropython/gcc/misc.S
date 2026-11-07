/* Plan 9 internal helpers missing from the Linux/GCC libc build. */
.text
.globl _tas
.type _tas,@function
_tas:
	movl	$1, %eax
	xchgl	%eax, (%rdi)
	ret
.size _tas, .-_tas
.section .note.GNU-stack,"",@progbits
