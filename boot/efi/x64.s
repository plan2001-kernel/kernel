MODE $64

TEXT start(SB), 1, $-4
	/* spill arguments */
	MOVQ CX, 8(SP)
	MOVQ DX, 16(SP)

	CALL reloc(SP)

TEXT reloc(SB), 1, $-4
	MOVQ 0(SP), SI
	SUBQ $reloc-IMAGEBASE(SB), SI
	MOVQ $IMAGEBASE, DI
	MOVQ $edata-IMAGEBASE(SB), CX
	CLD
	REP; MOVSB

	MOVQ 16(SP), BP
	MOVQ $efimain(SB), DI
	MOVQ DI, (SP)
	RET

TEXT eficall(SB), 1, $-4
	MOVQ SP, SI
	MOVQ SP, DI
	MOVL $(8*16), CX
	SUBQ CX, DI
	ANDQ $~15ULL, DI
	LEAQ 16(DI), SP
	CLD
	REP; MOVSB
	SUBQ $(8*16), SI

	MOVQ 0(SP), CX
	MOVQ 8(SP), DX
	MOVQ 16(SP), R8
	MOVQ 24(SP), R9
	CALL BP

	MOVQ SI, SP
	RET

TEXT rebase(SB), 1, $-4
	MOVQ BP, AX
	RET

#include "mem.h"

/*
 * Enter the kernel at its 64-bit entry (_efi64 in sys/src/9/pc64/l.s), in
 * the mode we are in: long mode, firmware page tables.  bootkern has checked
 * that this is safe.  Only the IDT is replaced, by an empty one, as the
 * firmware's handlers are gone after ExitBootServices.  Plan2001 Boot ABI
 * v1 (sys/include/bootinfo.h): RDI = BootInfo, RSI = 0, nothing else.
 */
TEXT jump64(SB), 1, $-4
	MOVQ	bootinfo+8(FP), DI
	XORL	SI, SI
	CLI

	/* load zero length idt */
	MOVL	$_idtptr64p<>(SB), AX
	MOVL	(AX), IDTR

	JMP	*BP

TEXT getcr3(SB), 1, $-4
	MOVQ	CR3, AX
	RET

TEXT getcr4(SB), 1, $-4
	MOVQ	CR4, AX
	RET

TEXT rdtsc(SB), 1, $-4
	RDTSC
	SHLQ	$32, DX
	ORQ	DX, AX
	RET

TEXT _idtptr64p<>(SB), 1, $-4
	WORD	$0
	QUAD	$0

