/*
 * Thin Plan 9 libc compatibility layer for the Linux host GE/O formatter.
 *
 * The formatter is the repository's real reaming code (ge/format).  It
 * was written against 9front libc; this file supplies only the small
 * surface that the O-only ream/seed call graph needs.  It deliberately
 * is not a copy of Plan 9 libc: allocation and the C string routines are
 * the host's, formatting is the repository's fmt package, and the
 * synchronization primitives are the single-threaded form the host
 * formatter requires (the ream path starts no GE worker processes).
 */
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

Gefs	*fs;
char	*seeddir;
int	debug;
int	usereserve;

extern _Noreturn void	exit(int);
extern _Noreturn void	abort(void);

Errctx	**errctx;
static Errctx	*errctxstore;

/*
 * allocation: the host heap; the Plan 9 prototypes pass 32-bit lengths
 * and are ABI compatible with glibc's size_t arguments.
 */
void*
mallocz(ulong n, int zero)
{
	void *p;

	if(zero)
		p = calloc(1, n);
	else
		p = malloc(n);
	return p;
}

void*
emalloc(usize n, int zero)
{
	void *p;

	if((p = mallocz(n, zero)) == nil)
		error(Enomem);
	return p;
}

uintptr
getcallerpc(void *p)
{
	USED(p);
	return 0;
}

/*
 * synchronization: the formatter is one host thread.  The real GE
 * routines keep their locking structure; with no competing worker the
 * locks are uncontended, and the rendezvous calls never block.  A
 * genuinely exhausted cache would spin, so the caller gives the ream
 * path a cache far larger than any seed image needs.
 */
void lock(Lock *l) { USED(l); }
void unlock(Lock *l) { USED(l); }

void qlock(QLock *l) { USED(l); }
void qunlock(QLock *l) { USED(l); }
int canqlock(QLock *l) { USED(l); return 1; }

void rsleep(Rendez *r) { USED(r); }
int rwakeup(Rendez *r) { USED(r); return 0; }

long agetl(Along *a) { return *(volatile long*)&a->v; }
vlong agetv(Avlong *a) { return *(volatile vlong*)&a->v; }

long
aswapl(Along *a, long v)
{
	long o;

	o = a->v;
	a->v = v;
	return o;
}

long
aincl(Along *a, long d)
{
	return a->v += d;
}

int
acasl(Along *a, long o, long n)
{
	if(a->v != o)
		return 0;
	a->v = n;
	return 1;
}

/*
 * error handling: the formatter's Errctx stack, in the one host thread.
 * longjmp is the formatter's own (setjmp.S).
 */
void**
privalloc(void)
{
	return (void**)&errctxstore;
}

jmp_buf*
_waserror(void)
{
	Errctx *c;

	c = *errctx;
	c->nerrlab++;
	assert(c->nerrlab > 0 && c->nerrlab < Estacksz);
	return &c->errlab[c->nerrlab-1];
}

_Noreturn static void
errorv(char *fmt, va_list ap, int broke_)
{
	Errctx *c;

	c = *errctx;
	vsnprint(c->err, sizeof c->err, fmt, ap);
	if(broke_)
		fprint(2, "%s\n", c->err);
	if(c->nerrlab <= 0){
		fprint(2, "format-o: %s\n", c->err);
		abort();
	}
	longjmp(c->errlab[--c->nerrlab], -1);
}

_Noreturn void
broke(char *fmt, ...)
{
	va_list ap;

	aincl(&fs->rdonly, 1);
	va_start(ap, fmt);
	errorv(fmt, ap, 1);
}

_Noreturn void
error(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	errorv(fmt, ap, 0);
}

_Noreturn void
nexterror(void)
{
	Errctx *c;

	c = *errctx;
	if(c->nerrlab <= 0){
		fprint(2, "format-o: unexpected error: %s\n", c->err);
		abort();
	}
	longjmp(c->errlab[--c->nerrlab], -1);
}

void
_trace(char *msg, Bptr bp, vlong v0, vlong v1)
{
	USED(msg);
	USED(bp);
	USED(v0);
	USED(v1);
}

_Noreturn void
_babort(vlong addr, char *msg)
{
	fprint(2, "format-o: assert failed for [%llx]: %s\n", addr, msg);
	abort();
}

_Noreturn void
sysfatal(char *fmt, ...)
{
	va_list ap;
	char buf[ERRMAX];

	va_start(ap, fmt);
	vsnprint(buf, sizeof buf, fmt, ap);
	va_end(ap);
	fprint(2, "format-o: %s\n", buf);
	exit(1);
}

void
rerrstr(char *buf, uint n)
{
	if(n == 0)
		return;
	strecpy(buf, buf+n, (*errctx)->err);
}

void
_assert(char *s)
{
	fprint(2, "format-o: assertion failed: %s\n", s);
	abort();
}

/*
 * system calls.  The Plan 9 prototypes for open/read/write/pread/pwrite
 * are ABI-compatible with Linux; OREAD/OWRITE/ORDWR are 0/1/2 there as
 * well.  dirfstat only needs the regular file length; the formatter
 * always seeks its own explicit offsets afterwards.
 */
extern long	lseek(int, long, int);
extern int	usleep(uint);

Dir*
dirfstat(int fd)
{
	Dir *d;
	long n, cur;

	cur = lseek(fd, 0, 1);
	if(cur < 0)
		return nil;
	n = lseek(fd, 0, 2);
	if(n < 0)
		return nil;
	if(lseek(fd, cur, 0) < 0)
		return nil;
	if((d = mallocz(sizeof *d, 1)) == nil)
		return nil;
	d->length = n;
	return d;
}

int
sleep(long ms)
{
	if(ms > 0)
		usleep(ms > 1000 ? 1000000 : (uint)ms * 1000);
	return 0;
}

char*
strecpy(char *to, char *e, char *from)
{
	char *p;

	p = to;
	while(p < e-1 && *from != 0)
		*p++ = *from++;
	*p = 0;
	return p;
}

int
isNaN(double d)
{
	return d != d;
}

int
isInf(double d, int sign)
{
	double inf;

	inf = 1.0/0.0;
	if(sign < 0)
		return d == -inf;
	if(sign > 0)
		return d == inf;
	return d == inf || d == -inf;
}

double
pow10(int n)
{
	return pow(10.0, (double)n);
}

/*
 * %B in the GE messages: (address,hash,generation).
 */
int
Bconv(Fmt *f)
{
	Bptr bp;

	bp = va_arg(f->args, Bptr);
	return fmtprint(f, "(%llx,%.16llux,%llx)", bp.addr, bp.hash, bp.gen);
}

void
hostfmtinstall(void)
{
	fmtinstall('B', Bconv);
}
