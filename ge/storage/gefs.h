/*
 * GE engine kernel glue, adapted from 9front's /sys/src/cmd/gefs.
 * The native GE/O engine uses this without a 9P service (see o.c,
 * obj.c); the inherited FS/9P service sources are excluded.
 * This is
 * what its sources include instead of libc's headers: the kernel's,
 * then the names gefs takes from libc, made the kernel's.  kern.c has
 * the functions.
 */
#include "u.h"
#include "../../kernel/core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../../kernel/core/error.h"
#include <avl.h>

/* the GE instance (gdat.h); the protos below need it */
typedef struct Gefs Gefs;

/* libc.h's that the kernel's lib.h lacks */
#define	DMAUTH		0x08000000
#define	DMTMP		0x04000000
#define	QTTMP		0x04
#define	STATMAX		65535U

/* libc's flags of a Fmt (libc.h), for dump.c's verbs */
enum {
	FmtWidth	= 1,
	FmtLeft		= FmtWidth << 1,
	FmtPrec		= FmtLeft << 1,
	FmtSharp	= FmtPrec << 1,
	FmtSpace	= FmtSharp << 1,
	FmtSign		= FmtSpace << 1,
	FmtZero		= FmtSign << 1,
	FmtUnsigned	= FmtZero << 1,
	FmtShort	= FmtUnsigned << 1,
	FmtLong		= FmtShort << 1,
	FmtVLong	= FmtLong << 1,
	FmtComma	= FmtVLong << 1,
	FmtByte		= FmtComma << 1,
};

/*
 * libc's atomics (libc.h), in kern.c: libc's atomic.s has a coherence,
 * which is the kernel's too
 */
typedef struct Along	Along;
typedef struct Avlong	Avlong;
typedef struct Aptr	Aptr;
struct Along {
	long	v;
};
struct Avlong {
	vlong	v;
};
struct Aptr {
	void	*v;
};
extern	long	agetl(Along*);
extern	vlong	agetv(Avlong*);
extern	void*	agetp(Aptr*);
extern	long	aswapl(Along*, long);
extern	vlong	aswapv(Avlong*, vlong);
extern	void*	aswapp(Aptr*, void*);
extern	long	aincl(Along*, long);
extern	vlong	aincv(Avlong*, vlong);
extern	int	acasl(Along*, long, long);
extern	int	acasv(Avlong*, vlong, vlong);
extern	int	acasp(Aptr*, void*, void*);

/*
 * libc's Rendez: rsleep with r->l held, which it lets go while it
 * sleeps; any number of sleepers (the kernel's Rendez takes one).
 */
typedef struct Grendez	Grendez;
typedef struct Gsleeper	Gsleeper;
struct Gsleeper {
	Rendez	rz;
	int	woken;
	Gsleeper	*next;
};
struct Grendez {
	QLock	*l;
	Lock	wl;		/* the kernel's spin lock, for the sleepers */
	Gsleeper	*head;
	Gsleeper	*tail;
};
void	rsleep(Grendez*);
int	rwakeup(Grendez*);
int	rwakeupall(Grendez*);

/* gefs's message channel between its procs (not the kernel's Chan) */
#define	Chan	Mchan
#define	Path	Gpath		/* tree.c's */
#define	Rendez	Grendez

/*
 * gefs's Locks are held across allocation and its own sleeps:
 * qlocks, not the kernel's spin locks.
 */
#define	Lock	QLock
#define	lock	qlock
#define	unlock	qunlock
#define	canlock	canqlock
int	canwlock(RWLock*);

/* errors: the kernel's waserror and error stack; error() has a format */
#define	error		gerror
#define	werrstr		gwerrstr
#define	errmsg()	(up->errstr)
#define	estacksz()	(up->nerrlab)
_Noreturn void	gerror(char*, ...);
void	gwerrstr(char*, ...);
#pragma	varargck	argpos	gerror	1
#pragma	varargck	argpos	gwerrstr	1
#pragma	varargck	type	"r"	void	/* up->errstr (kern.c) */

#define	fprint(fd, ...)	gfprint(fs, (fd), __VA_ARGS__)
#define	sysfatal	gsysfatal
#define	abort()		panic("gefs: abort")
#define	calloc(n, sz)	mallocz((n)*(sz), 1)
#define	memcpy(d, s, n)	memmove(d, s, n)
#define	atol(s)		strtol(s, nil, 10)
#define	nsec()		todget(nil, nil)
#define	sleep(ms)	gsleep(ms)
int	gfprint(Gefs*, int, char*, ...);
_Noreturn void	gsysfatal(char*, ...);
void	gsleep(long);
#pragma	varargck	argpos	gfprint	3
#pragma	varargck	argpos	gsysfatal	1

/* kern.c, for fs.c */
void	gflushdev(Gefs*);
void	gefshalted(Gefs*);

/* gefs's names that are the kernel's functions too */
#define	cachedel(a)	gcachedel(fs, (a))

/* mem.h's, as gdat.h's enum (the same values) */
#undef	KiB
#undef	MiB
#undef	GiB
#undef	TiB
#undef	PiB

#include "gdat.h"
#include "gfns.h"
