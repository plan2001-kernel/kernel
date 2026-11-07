#define nil		((void*)0)
typedef	unsigned short	ushort;
typedef	unsigned char	uchar;
/*
 * Plan 9 `ulong` is 32-bit on AMD64 while host GCC `long` is 64-bit.
 * Keep the Plan 9 width for this typedef; literal C `long` remains 64-bit
 * and is audited separately.
 */
typedef unsigned int	ulong;
typedef unsigned int	uint;
typedef signed char	schar;
typedef	long long	vlong;
typedef	unsigned long long uvlong;
typedef long long	intptr;
typedef unsigned long long uintptr;
typedef unsigned long long usize;
typedef	uint		Rune;
typedef union FPdbleword FPdbleword;
#ifdef PLAN2001_GCC_USER
/*
 * Linux GCC user build: the compiler needs the AMD64 SysV callee-saved
 * registers as well as SP/PC.  Index 0/1 keep the Plan 9 JMPBUFSP/PC
 * meaning (used by notejmp.c); indexes 2..7 are RBX/RBP/R12..R15.
 */
typedef uintptr		jmp_buf[8];
#else
typedef uintptr		jmp_buf[2];
#endif
#define	JMPBUFSP	0
#define	JMPBUFPC	1
#define	JMPBUFDPC	0
typedef unsigned int	mpdigit;	/* for /sys/include/mp.h */
typedef unsigned char	u8int;
typedef unsigned short	u16int;
typedef unsigned int	u32int;
typedef unsigned long long u64int;
typedef signed char s8int;
typedef signed short s16int;
typedef signed int s32int;
typedef signed long long s64int;

/* MXCSR */
/* fcr */
#define	FPFTZ	(1<<15)	/* amd64 */
#define	FPINEX	(1<<12)
#define	FPUNFL	(1<<11)
#define	FPOVFL	(1<<10)
#define	FPZDIV	(1<<9)
#define	FPDNRM	(1<<8)	/* amd64 */
#define	FPINVAL	(1<<7)
#define	FPDAZ	(1<<6)	/* amd64 */
#define	FPRNR	(0<<13)
#define	FPRZ	(3<<13)
#define	FPRPINF	(2<<13)
#define	FPRNINF	(1<<13)
#define	FPRMASK	(3<<13)
#define	FPPEXT	0
#define	FPPSGL	0
#define	FPPDBL	0
#define	FPPMASK	0
/* fsr */
#define	FPAINEX	(1<<5)
#define	FPAUNFL	(1<<4)
#define	FPAOVFL	(1<<3)
#define	FPAZDIV	(1<<2)
#define	FPADNRM	(1<<1)	/* not in plan 9 */
#define	FPAINVAL	(1<<0)
union FPdbleword
{
	double	x;
	struct {	/* little endian */
		uint lo;
		uint hi;
	};
};

/*
 * Use the SysV AMD64 builtin varargs ABI.
 */
typedef	__builtin_va_list	va_list;
#define va_start(list, start)	__builtin_va_start(list, start)
#define va_end(list)		__builtin_va_end(list)
#define va_arg(list, mode)	__builtin_va_arg(list, mode)
#define va_copy(dst, src)	__builtin_va_copy(dst, src)
