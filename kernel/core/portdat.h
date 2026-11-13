typedef struct Alarms	Alarms;
typedef struct Block	Block;
typedef struct Bpool	Bpool;
typedef struct Cmdbuf	Cmdbuf;
typedef struct Cmdtab	Cmdtab;
typedef struct Confmem	Confmem;
typedef struct Edf	Edf;
typedef struct Egrp	Egrp;
typedef struct Evalue	Evalue;
typedef struct DevConf	DevConf;
typedef struct Lock	Lock;
typedef struct Log	Log;
typedef struct Logflag	Logflag;
typedef struct Note	Note;
typedef struct Page	Page;
typedef struct Palloc	Palloc;
typedef struct Perf	Perf;
typedef struct PhysUart	PhysUart;
typedef struct Physseg	Physseg;
typedef struct Proc	Proc;
typedef struct Pte	Pte;
typedef struct PMach	PMach;
typedef struct QLock	QLock;
typedef struct Queue	Queue;
typedef struct Ref	Ref;
typedef struct Rendez	Rendez;
typedef struct Rendezq	Rendezq;
typedef struct RWLock	RWLock;
typedef struct Sargs	Sargs;
typedef struct Schedq	Schedq;
typedef struct Segment	Segment;
typedef struct Segio	Segio;
typedef struct Sysstat	Sysstat;
typedef struct Timer	Timer;
typedef struct Timers	Timers;
typedef struct Tos	Tos;
typedef struct Uart	Uart;
typedef struct Procstat	Procstat;
typedef struct Pwait	Pwait;
typedef struct ObjProcessInfo ObjProcessInfo;
typedef struct Watchpt	Watchpt;
typedef struct Watchdog	Watchdog;

#pragma incomplete DevConf
#pragma incomplete Edf
#pragma incomplete Queue
#pragma incomplete Timers
#pragma incomplete Tos

#include <fcall.h>

struct Lock
{
	ulong	key;
	ulong	sr;
	uintptr	pc;
	Proc	*p;
	Mach	*m;
	ushort	isilock;
};

struct Ref
{
	long	ref;
};

struct Rendez
{
	Lock	lk;	/* compile-only name; keeps the 6c field offsets */
	Proc	*p;
};

/*
 * Rendez embeds a Lock.  6c lets lock()/unlock() take the Rendez pointer
 * directly.  The GCC compile-only path names that embedded Lock `lk` (to
 * avoid a promoted-name collision) and reaches it explicitly.
 */
#define RENDEZLOCK(r)		lock(&(r)->lk)
#define RENDEZUNLOCK(r)		unlock(&(r)->lk)
#define RENDEZCANLOCK(r)	canlock(&(r)->lk)

struct QLock
{
	Lock	use;		/* to access Qlock structure */
	Proc	*head;		/* next process waiting for object */
	Proc	*tail;		/* last process waiting for object */
	uintptr	pc;		/* pc of owner */
	int	locked;		/* flag */
};

struct Rendezq
{
	QLock qlk;
	Rendez rz;
};

struct RWLock
{
	Lock	use;
	Proc	*head;		/* list of waiting processes */
	Proc	*tail;
	uintptr	wpc;		/* pc of writer */
	int	writer;		/* number of writers */
	int	readers;	/* number of readers */
};

struct Alarms
{
	QLock	lk;	/* compile-only name; keeps the 6c field offsets */
	Proc	*head;
};

struct Sargs
{
	uchar	args[MAXSYSARG*BY2WD];
};

/*
 * Syscall argument transport.  The a.out AMD64 syscall ABI supplies
 * 64-bit argument slots, while Plan 9 `long`/`ulong` arguments are
 * 32-bit values.
 */
typedef struct Sysargs	Sysargs;
struct Sysargs
{
	uchar	*args;
	int	n;
	int	bad;
};

static inline void
sysargs_init(Sysargs *a, uchar *args)
{
	a->args = args;
	a->n = 0;
	a->bad = 0;
}

static inline u64int
sysarg_next(Sysargs *a)
{
	u64int v;
	int i;

	if(a->n >= MAXSYSARG){
		a->bad = 1;
		return 0;
	}
	v = 0;
	for(i = 0; i < BY2WD; i++)
		v |= (u64int)a->args[a->n*BY2WD + i] << (8*i);
	a->n++;
	return v;
}

#define sysva_list		Sysargs*
#define sysva_arg(l, type)	((type)_Generic((type)0, \
	long: (long)(int)(u32int)sysarg_next(l), \
	unsigned long: (unsigned long)(u32int)sysarg_next(l), \
	default: sysarg_next(l)))

/* flag values */
enum
{
	BINTR	=	(1<<0),
	BFREE	=	(1<<1),
	Bipck	=	(1<<2),		/* ip checksum */
	Budpck	=	(1<<3),		/* udp checksum */
	Btcpck	=	(1<<4),		/* tcp checksum */
	Bpktck	=	(1<<5),		/* packet checksum */
};

struct Block
{
	Block*	next;
	Block*	list;
	uchar*	rp;			/* first unconsumed byte */
	uchar*	wp;			/* first empty byte */
	uchar*	lim;			/* 1 past the end of the buffer */
	uchar*	base;			/* start of the buffer */
	Bpool*	pool;
	ushort	flag;
	ushort	checksum;		/* IP checksum of complete packet (minus media header) */
};

#define BLEN(s)	((s)->wp - (s)->rp)
#define BALLOC(s) ((s)->lim - (s)->base)

struct Bpool
{
	ulong	size;			/* block size */
	ulong	align;			/* block alignment */

	Lock lk;
	Block	*head;			/* freelist head */
};

/*
 * The canonical O-only kernel has no Dev registry and no legacy
 * Chan/Walkqid types: no compiled source has a pointer signature,
 * allocation or field reader for them.  Qid itself remains in lib.h
 * because ge/storage/dump.c formats it; it is not a driver directory id.
 */
enum
{
	NUser,				/* note provided externally */
	NExit,				/* deliver note quietly */
	NDebug,				/* print debug message */
};

struct Note
{
	char	msg[ERRMAX];
	int	flag;			/* whether system posted it */
	Ref ref;
};

enum
{
	PG_MOD		= 0x01,		/* software modified bit */
	PG_REF		= 0x02,		/* software referenced bit */
	PG_PRIV		= 0x04,		/* private page */
};

struct Page
{
	Ref ref;
	Page	*next;			/* Free list or Hash chains */
	uintptr	pa;			/* Physical address in memory */
	uintptr	va;			/* Virtual address for user */
	char	modref;			/* Simulated modify/reference bits */
	char	color;			/* Cache coloring */

#ifndef inittxtflush
	/* Flush icache bitmap for putmmu() */
	ulong	txtflush[(MAXMACH+31)/32];

#define inittxtflush(p)	memset((p)->txtflush, 0, sizeof((p)->txtflush))
#define settxtflush(p, c) if(c) memset((p)->txtflush, ~0, sizeof((p)->txtflush))
#define needtxtflush(p)	((p)->txtflush[m->machno>>5] & (1 << (m->machno&0x1F)))
#define donetxtflush(p)	((p)->txtflush[m->machno>>5] &= ~(1 << (m->machno&0x1F)))
#endif
};

struct Pte
{
	Page	*pages[PTEPERTAB];	/* Page map for this chunk of pte */
	Page	**first;		/* First used entry */
	Page	**last;			/* Last used entry */
};

/* Segment types */
enum
{
	SG_TYPE		= 017,		/* Mask type of segment */
	SG_TEXT		= 00,
	SG_DATA		= 01,
	SG_BSS		= 02,
	SG_STACK	= 03,
	/* 04 was SG_SHARED, the named anonymous shared segment.  Volatile
	 * capability-protected regions use SG_REGION and are never attachable
	 * by name. */
	SG_PHYSICAL	= 05,
	SG_FIXED	= 06,
	SG_STICKY	= 07,
	SG_REGION	= 010,		/* capability-region mapping (internal) */

	SG_RONLY	= 0040,		/* Segment is read only */
	SG_CEXEC	= 0100,		/* Detach at exec */
	SG_FAULT	= 0200,		/* Fault on access */
	SG_CACHED	= 0400,		/* Normal cached memory */
	SG_DEVICE	= 01000,	/* Memory mapped device */
	SG_NOEXEC	= 02000,	/* No execute */
	SG_NOSWAP	= 04000,	/* No swap image (retained, no paging behavior) */
};

/*
 * There is no swap and no file-backed image pager: a PTE entry is either a
 * resident Page or nil.  A nil entry in a canonical native O segment is an
 * invariant violation, not a demand-load request.
 */
#define pagedout(s)	(((uintptr)s)==0)

#define SEGMAXSIZE	(1ULL*SEGMAPSIZE*PTEMAPMEM)

struct Physseg
{
	int	attr;			/* Segment attributes */
	char	*name;			/* Attach name */
	uintptr	pa;			/* Physical address */
	uintptr	size;			/* Maximum segment size in bytes */
};

struct Segment
{
	Ref ref;
	QLock qlk;
	int	type;		/* segment type */
	ulong	size;		/* size in pages */

	uintptr	base;		/* virtual base */
	uintptr	top;		/* virtual top */

	int	flushme;	/* maintain icache for this segment */
	Physseg *pseg;
	ulong	*profile;	/* Tick profile area */
	Pte	**map;
	int	mapsize;
	Pte	*ssegmap[SSEGMAPSIZE];

	ulong	used;

	/*
	 * SG_REGION mappings hold one VkindRegion reference and record whether
	 * this address space owns the single writable mapping.  The reference
	 * keeps the region and its Page backing alive after the process closes
	 * its Handles; regionsegput() drops it and releases the writer slot.
	 */
	void	*region;
	int	regionwriter;
};

struct Segio
{
	QLock qlk;
	Rendez	cmdwait;
	Rendez	replywait;

	Proc	*p;		/* segmentio kproc */
	Segment	*s;

	char	*data;
	char	*addr;
	int	dlen;
	int	cmd;
	char	*err;
};

enum
{
	ENVLOG =	5,
	ENVHASH =	1<<ENVLOG,	/* Egrp hash for variable lookup */
};
struct Evalue
{
	char	*value;
	int	len;
	Evalue	*hash;
	char	name[];
};

struct Egrp
{
	Ref ref;
	RWLock rwlk;
	Evalue	**ent;
	int	nent;			/* numer of slots in ent[] */
	int	low;			/* lowest free index in ent[] */
	int	alloc;			/* bytes allocated for env */
	Evalue	*hash[ENVHASH];		/* hashtable for name lookup */
};

struct Palloc
{
	Lock lk;
	Page	*head;			/* freelist head */
	ulong	freecount;		/* how many pages on free list now */
	ulong	user;			/* how many user pages */
	Page	*pages;			/* array of all pages */
	Rendezq	pwait[2];		/* Queues of procs waiting for memory */
};

/*
 * Process completion is a separate, reference-counted resource so it can
 * outlive the scheduler Proc and its address space.  A Proc owns one
 * reference while it exists; every VkindProcess Handle owns another.  The
 * record is immutable after the single completion transition.
 */
enum {
	PstatCreated	= 0,
	PstatRunning	= 1,
	PstatCompleted	= 2,
};

enum {
	PcauseNone	= 0,
	PcauseExit	= 1,
	PcauseTerminated = 2,
	PcauseFault	= 3,
};

struct Procstat
{
	Ref	ref;
	Lock	lk;
	int	state;
	int	cause;
	ulong	pid;
	uvlong	generation;
	uvlong	owner;		/* immutable program/process OwnerID */
	ulong	time[3];	/* user, sys, real (ms) */
	char	msg[128];
	Proc	*proc;		/* live target, nil after completion */
	Pwait	*wait;		/* observers blocked on completion */
};

/*
 * fasttick timer interrupts
 */
enum {
	/* Mode */
	Trelative,	/* timer programmed in ns from now */
	Tperiodic,	/* periodic timer, period in ns */
};

struct Timer
{
	/* Public interface */
	int	tmode;		/* See above */
	vlong	tns;		/* meaning defined by mode */
	void	(*tf)(Ureg*, Timer*);
	void	*ta;
	/* Internal */
	Lock lk;
	Mach	*tactive;	/* The cpu that tf is active on */
	Timers	*tt;		/* Timers queue this timer runs on */
	Tval	tticks;		/* tns converted to ticks */
	Tval	twhen;		/* ns represented in fastticks */
	Timer	*tnext;
};

enum
{
	RFNAMEG		= (1<<0),
	RFENVG		= (1<<1),
	RFFDG		= (1<<2),
	RFNOTEG		= (1<<3),
	RFPROC		= (1<<4),
	RFMEM		= (1<<5),
	RFCNAMEG	= (1<<10),
	RFCENVG		= (1<<11),
	RFCFDG		= (1<<12),
	RFNOMNT		= (1<<14),
};

/*
 *  process memory segments - NSEG always last !
 */
enum
{
	SSEG, TSEG, DSEG, BSEG, ESEG, LSEG, SEG1, SEG2, SEG3, SEG4,
	SEG5, SEG6, SEG7, SEG8, SEG9, SEG10, SEG11, SEG12, SEG13, SEG14,
	SEG15, SEG16, NSEG
};

enum
{
	Dead = 0,		/* Process states */
	Moribund,
	New,
	Ready,
	Scheding,
	Running,
	Queueing,
	QueueingR,
	QueueingW,
	Wakeme,
	Stopped,
	Waitrelease,

	Proc_stopme = 1, 	/* devproc requests */
	Proc_exitme,
	Proc_traceme,
	Proc_exitbig,
	Proc_tracesyscall,

	TUser = 0, 		/* Proc.time */
	TSys,
	TReal,
	TCUser,
	TCSys,
	TCReal,

	NERR = 64,		/* gefs nests its error labels as deep (Estacksz) */
	NNOTE = 5,

	Npriq		= 20,		/* number of scheduler priority levels */
	Nrq		= Npriq+2,	/* number of priority levels including real time */
	PriRelease	= Npriq,	/* released edf processes */
	PriEdf		= Npriq+1,	/* active edf processes */
	PriNormal	= 10,		/* base priority for normal processes */
	PriExtra	= Npriq-1,	/* edf processes at high best-effort pri */
	PriKproc	= 13,		/* base priority for kernel processes */
	PriRoot		= 13,		/* base priority for root processes */
};

struct Schedq
{
	Lock lk;
	Proc*	head;
	Proc*	tail;
	int	n;
};

enum {
	Iohout		= 1,	/* native stdout stream */
	Iohdiag		= 2,	/* native diagnostic stream */
	Iomout		= 1<<0,
	Iomdiag		= 1<<1,
};

struct Proc
{
	Label	sched;		/* known to l.s */

	Mach	*mach;		/* machine running this proc */
	char	*text;
	char	*user;

	char	*args;
	int	nargs;		/* number of bytes of args */
	int	setargs;	/* process changed its args */

	Proc	*rnext;		/* next process in run queue */
	Proc	*qnext;		/* next process on queue for a QLock */

	char	*psstate;	/* What /proc/#/status reports */
	int	state;

	ulong	pid;
	ulong	noteid;		/* Equivalent of note group */
	ulong	index;

	Procstat *stat;		/* completion resource, nil only before creation */
	int	boot;		/* explicit initial/boot process, never ancestry */
	int	console_slot;	/* kernel-owned boot console, 0 = no session */
	uvlong	owner;		/* immutable process/program OwnerID */

	QLock	seglock;	/* locked whenever seg[] changes */
	Segment	*seg[NSEG];

	Egrp 	*egrp;		/* Environment group */
	Ref	*fdgrp;		/* legacy RFORK FD-group presence token, nil = absent */
	void	*vobjhandles;	/* Process-local typed, revocable capability roots */
	ulong	iomask;		/* Process-local native output grants */

	int	insyscall;
	ulong	time[6];	/* User, Sys, Real; child U, S, R */

	uvlong	kentry;		/* Kernel entry time stamp (for profiling) */
	/*
	 * pcycles: cycles spent in this process (updated on procswitch)
	 * when this is the current proc and we're in the kernel
	 * (procrestores outnumber procsaves by one)
	 * the number of cycles spent in the proc is pcycles + cycles()
	 * when this is not the current process or we're in user mode
	 * (procrestores and procsaves balance), it is pcycles.
	 */
	vlong	pcycles;

	QLock	debug;		/* to access debugging elements of User */
	Proc	*pdbg;		/* the debugging process */
	ulong	procmode;	/* proc device default file mode */
	int	privatemem;	/* proc does not let anyone read mem */
	int	noswap;		/* permit the last free page (allocation priority) */
	int	hang;		/* hang at next exec for debug */
	int	terminate;	/* sticky capability termination; debug protects writes */
	int	procctl;	/* Control for /proc debugging */

	Lock	rlock;		/* sync sleep/wakeup with procinterrupt */
	Rendez	*r;		/* rendezvous point slept on */
	Rendez	sleep;		/* place for syssleep/debug */
	int	notepending;	/* note issued but not acted on */
	int	kp;		/* true if a kernel process */
	Proc	*palarm;	/* Next alarm time */
	ulong	alarm;		/* Time of call */
	int	newtlb;		/* Pager has changed my pte's, I must flush */

	Timer timer;		/* For tsleep and real-time */
	Rendez	*trend;
	int	(*tfn)(void*);
	void	(*kpfun)(void*);
	void	*kparg;

	Sargs	s;		/* syscall arguments */
	int	scallnr;	/* sys call number */
	int	nerrlab;
	Label	errlab[NERR];
	char	*syserrstr;	/* last error from a system call, errbuf0 or 1 */
	char	*errstr;	/* reason we're unwinding the error stack, errbuf1 or 0 */
	char	errbuf0[ERRMAX];
	char	errbuf1[ERRMAX];
	char	genbuf[128];	/* buffer used e.g. for a name error in validname */

	Note	*lastnote;
	Note	*note[NNOTE];
	short	nnote;
	short	notified;	/* sysnoted is due */
	int	(*notify)(void*, char*);

	Lock	*lastlock;	/* debugging */
	Lock	*lastilock;	/* debugging */

	int	nlocks;		/* number of locks held by proc */

	ulong	delaysched;
	ulong	priority;	/* priority level */
	ulong	basepri;	/* base priority level */
	uchar	fixedpri;	/* priority level doesn't change */
	uchar	wired;
	int	affinity;	/* machno this process last ran on */
	ulong	cpu;		/* cpu average */
	ulong	lastupdate;

	int	firstuser;	/* print first-scheduled line in sched() */
	int	reqcpu;		/* cpu requested for the firstuser line, -1 if none */
	int	exiting;	/* set before publishing an exit notification */
	uvlong	membudget;	/* committed user page budget, 0 = unlimited */
	ulong	mempages;	/* currently charged committed user pages */
	Lock	memlock;	/* protects membudget/mempages */

	Edf	*edf;		/* if non-null, real-time proc, edf contains scheduling params */
	int	trace;		/* process being traced? */

	uintptr	qpc;		/* pc calling last blocking qlock */
	QLock	*eql;		/* interruptable eqlock */

	Ureg	*noteureg;	/* User registers for notes */
	Ureg	*dbgreg;	/* User registers for devproc */

	PFPU pfpu;		/* machine specific fpu state */
	PMMU pmmu;		/* machine specific mmu state */

	char	*syscalltrace;	/* syscall trace */
	
	Watchpt	*watchpt;	/* watchpoints */
	int	nwatchpt;
};

enum
{
	PRINTSIZE =	256,
	NUMSIZE	=	12,		/* size of formatted number */
	MB =		(1024*1024),
	/* READSTR was 1000, which is way too small for usb's ctl file */
	READSTR =	8000,		/* temporary buffer size for device reads */
};

extern	Conf	conf;
extern	char*	conffile;
extern	int	cpuserver;
extern	char*	eve;
extern	char	hostdomain[];
extern	Queue*	kprintoq;
extern	int	nsyscall;
extern	Palloc	palloc;
extern	int	panicking;
extern	Queue*	serialoq;
extern	char*	statename[];
extern	char*	sysname;
extern	vlong	fasthz;
extern	uint	qiomaxatomic;
extern	char*	sysctab[];

enum
{
	LRESPROF	= 3,
};

/*
 *  action log
 */
struct Log {
	Lock lk;
	int	opens;
	char*	buf;
	char	*end;
	char	*rptr;
	int	len;
	int	nlog;
	int	minread;

	int	logmask;	/* mask of things to debug */

	QLock	readq;
	Rendez	readr;
};

struct Logflag {
	char*	name;
	int	mask;
};

enum
{
	NCMDFIELD = 128
};

struct Cmdbuf
{
	char	*buf;
	char	**f;
	int	nf;
};

struct Cmdtab
{
	int	index;	/* used by client to switch on result */
	char	*cmd;	/* command name */
	int	narg;	/* expected #args; 0 ==> variadic */
};

/*
 *  routines to access UART hardware
 */
struct PhysUart
{
	char*	name;
	Uart*	(*pnp)(void);
	void	(*enable)(Uart*, int);
	void	(*disable)(Uart*);
	void	(*kick)(Uart*);
	void	(*dobreak)(Uart*, int);
	int	(*baud)(Uart*, int);
	int	(*bits)(Uart*, int);
	int	(*stop)(Uart*, int);
	int	(*parity)(Uart*, int);
	void	(*modemctl)(Uart*, int);
	void	(*rts)(Uart*, int);
	void	(*dtr)(Uart*, int);
	char*	(*status)(Uart*, char*, char*);
	void	(*fifo)(Uart*, int);
	void	(*power)(Uart*, int);
	int	(*getc)(Uart*);	/* polling versions, for iprint, rdb */
	void	(*putc)(Uart*, int);
};

enum {
	Stagesize=	2048
};

/*
 *  software UART
 */
struct Uart
{
	void*	regs;			/* hardware stuff */
	void*	saveregs;		/* place to put registers on power down */
	char*	name;			/* internal name */
	ulong	freq;			/* clock frequency */
	int	bits;			/* bits per character */
	int	stop;			/* stop bits */
	int	parity;			/* even, odd or no parity */
	int	baud;			/* baud rate */
	PhysUart*phys;
	int	console;		/* used as a serial console */
	int	special;		/* internal kernel device */
	Uart*	next;			/* list of allocated uarts */

	QLock qlk;
	int	type;			/* ?? */
	int	dev;
	int	opens;

	int	enabled;
	Uart	*elist;			/* next enabled interface */

	int	perr;			/* parity errors */
	int	ferr;			/* framing errors */
	int	oerr;			/* rcvr overruns */
	int	berr;			/* no input buffers */
	int	serr;			/* input queue overflow */

	/* buffers */
	int	(*putc)(Queue*, int);
	Queue	*iq;
	Queue	*oq;

	Lock	rlock;
	uchar	istage[Stagesize];
	uchar	*iw;
	uchar	*ir;
	uchar	*ie;

	Lock	tlock;			/* transmit */
	uchar	ostage[Stagesize];
	uchar	*op;
	uchar	*oe;
	int	drain;

	int	modem;			/* hardware flow control on */
	int	xonoff;			/* software flow control on */
	int	blocked;
	int	cts, dsr, dcd;	/* keep track of modem status */ 
	int	ctsbackoff;
	int	hup_dsr, hup_dcd;	/* send hangup upstream? */
	int	dohup;

	Rendez	r;
};

extern	Uart*	consuart;

/*
 *  performance timers, all units in perfticks
 */
/*
 * Typed multi-CPU monitoring snapshot for sysstatget().  The legacy #c
 * adapter formats these values; this is not a userspace ABI.
 */
struct Sysstat
{
	int	id;
	int	cs;
	int	intr;
	int	syscall;
	int	pfault;
	int	tlbfault;
	int	tlbpurge;
	int	load;
	ulong	idle;		/* percent of performance period in idle */
	ulong	inintr;		/* percent of performance period in interrupt */
};

struct Perf
{
	ulong	intrts;		/* time of last interrupt */
	ulong	inintr;		/* time since last clock tick in interrupt handlers */
	ulong	avg_inintr;	/* avg time per clock tick in interrupt handlers */
	ulong	inidle;		/* time since last clock tick in idle loop */
	ulong	avg_inidle;	/* avg time per clock tick in idle loop */
	ulong	last;		/* value of perfticks() at last clock tick */
	ulong	period;		/* perfticks() per clock tick */
};

struct Watchdog
{
	void	(*enable)(void);	/* watchdog enable */
	void	(*disable)(void);	/* watchdog disable */
	void	(*restart)(void);	/* watchdog restart */
	void	(*stat)(char*, char*);	/* watchdog statistics */
};

struct Watchpt
{
	enum {
		WATCHRD = 1,
		WATCHWR = 2,
		WATCHEX = 4,
	} type;
	uintptr addr, len;
};

struct PMach
{
	Proc*	readied;		/* for runproc */
	Label	sched;			/* scheduler wakeup */
	ulong	ticks;			/* of the clock since boot time */
	ulong	schedticks;		/* next forced context switch */

	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	int	ilockdepth;

	int	flushmmu;		/* make current proc flush it's mmu state */

	int	tlbfault;
	int	tlbpurge;

	Perf	perf;			/* performance counters */

	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */
};

/* queue state bits,  Qmsg, Qcoalesce, and Qkick can be set in qopen */
enum
{
	/* Queue.state */
	Qstarve		= (1<<0),	/* consumer starved */
	Qmsg		= (1<<1),	/* message stream */
	Qclosed		= (1<<2),	/* queue has been closed/hungup */
	Qflow		= (1<<3),	/* producer flow controlled */
	Qcoalesce	= (1<<4),	/* coallesce packets on read */
	Qkick		= (1<<5),	/* always call the kick routine after qwrite */
};

#pragma	varargck	type	"I"	uchar*
#pragma	varargck	type	"V"	uchar*
#pragma	varargck	type	"E"	uchar*
#pragma	varargck	type	"M"	uchar*

/*
 * Log console output so it can be retrieved via /dev/kmesg.
 * This is good for catching boot-time messages after the fact.
 */
struct {
	Lock lk;
	uint n;
	char buf[16384];
} kmesg;
