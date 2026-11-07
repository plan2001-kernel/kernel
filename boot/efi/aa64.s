#define	SYSREG(op0,op1,Cn,Cm,op2)	SPR(((op0)<<19|(op1)<<16|(Cn)<<12|(Cm)<<8|(op2)<<5))
#define	SCTLR_EL1			SYSREG(3,0,1,0,0)

#define NSH	(1<<2 | 3)
#define NSHST	(1<<2 | 2)
#define	SY	(3<<2 | 3)

TEXT start(SB), 1, $-4
_base:
	MOV	R0, R3
	MOV	R1, R4

	MOV	$setSB(SB), R0
	BL	rebase(SB)
	MOV	R0, R28

	MOV	R3, R0
	MOV	R4, 0x08(FP)
	B	efimain(SB)

TEXT rebase(SB), 1, $-4
	ADR	_base, R1
	SUB	$0x8200, R0
	ADD	R1, R0
	RETURN

TEXT eficall(SB), 1, $-4
	MOV	R0, R8
	MOV	0x08(FP), R0
	MOV	0x10(FP), R1
	MOV	0x18(FP), R2
	MOV	0x20(FP), R3
	MOV	0x28(FP), R4
	MOV	0x30(FP), R5
	MOV	0x38(FP), R6
	MOV	0x40(FP), R7
	B	(R8)

TEXT mmudisable<>(SB), 1, $-4
#define SCTLRCLR \
	/* RES0 */	( 3<<30 \
	/* RES0 */	| 1<<27 \
	/* UCI */	| 1<<26 \
	/* EE */	| 1<<25 \
	/* RES0 */	| 1<<21 \
	/* E0E */	| 1<<24 \
	/* WXN */	| 1<<19 \
	/* nTWE */	| 1<<18 \
	/* RES0 */	| 1<<17 \
	/* nTWI */	| 1<<16 \
	/* UCT */	| 1<<15 \
	/* DZE */	| 1<<14 \
	/* RES0 */	| 1<<13 \
	/* RES0 */	| 1<<10 \
	/* UMA */	| 1<<9 \
	/* SA0 */	| 1<<4 \
	/* SA */	| 1<<3 \
	/* A */		| 1<<1 )
#define SCTLRSET \
	/* RES1 */	( 3<<28 \
	/* RES1 */	| 3<<22 \
	/* RES1 */	| 1<<20 \
	/* RES1 */	| 1<<11 )
#define SCTLRMMU \
	/* I */		( 1<<12 \
	/* C */		| 1<<2 \
	/* M */		| 1<<0 )

	/* initialise SCTLR, MMU and caches off */
	ISB	$SY
	MRS	SCTLR_EL1, R0
	BIC	$(SCTLRCLR | SCTLRMMU), R0
	ORR	$SCTLRSET, R0
	ISB	$SY
	MSR	R0, SCTLR_EL1
	ISB	$SY

	DSB	$NSHST
	TLBI	R0, 0,8,7,0	/* VMALLE1 */
	DSB	$NSH
	ISB	$SY
	RETURN

/*
 * Enter the kernel: MMU and caches off (mmudisable), then Plan2001 Boot
 * ABI v1, ARM64 entry (docs/boot-abi-arm64.md): X0 = BootInfo, X1 = 0.
 * jump(entry, bootinfo), called by archjump() in archaa64.c.
 */
TEXT jump(SB), 1, $-4
	MOV	R0, R3
	MOV	0x08(FP), R4
	BL	mmudisable<>(SB)
	MOV	R4, R0
	MOV	$0, R1
	B	(R3)
