#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

void	(*consdebug)(void) = nil;
void	(*screenputs)(char*, int) = nil;

Queue*	serialoq;		/* serial console output */
Queue*	kprintoq;		/* console output, for /dev/kprint */
static ulong	kprintinuse;	/* test and set whether /dev/kprint is open */
int	iprintscreenputs = 1;

int	panicking;

char	*sysname;
vlong	fasthz;

static int	consservicesinited;

void
printinit(void)
{
}

int
consactive(void)
{
	if(serialoq)
		return qlen(serialoq) > 0;
	return 0;
}

void
prflush(void)
{
	ulong now;

	now = m->pmach.ticks;
	while(consactive())
		if(m->pmach.ticks - now >= HZ)
			break;
}

void
kmesgwrite(char *str, int n)
{
	uint nn, d;

	ilock(&kmesg.lk);
	/* take the tail of huge writes */
	if(n > sizeof kmesg.buf){
		d = n - sizeof kmesg.buf;
		str += d;
		n -= d;
	}

	/* slide the buffer down to make room */
	nn = kmesg.n;
	if(nn + n >= sizeof kmesg.buf){
		d = nn + n - sizeof kmesg.buf;
		if(d)
			memmove(kmesg.buf, kmesg.buf+d, sizeof kmesg.buf-d);
		nn -= d;
	}

	/* copy the data in */
	memmove(kmesg.buf+nn, str, n);
	nn += n;
	kmesg.n = nn;
	iunlock(&kmesg.lk);
}

/*
 * Read a snapshot of the kernel log ring.  This deliberately keeps the
 * unlocked inherited policy: kmesg.n never shrinks, so the worst case is a
 * slightly inconsistent snapshot while a writer appends.
 */
long
kmesgreadat(void *va, long n, vlong off)
{
	char *buf;

	if(n <= 0)
		return n;
	buf = va;
	if(off >= kmesg.n)
		return 0;
	if(off+n > kmesg.n)
		n = kmesg.n - off;
	memmove(buf, kmesg.buf+off, n);
	return n;
}

/*
 *   Print a string on the console.  Convert \n to \r\n for serial
 *   line consoles.  Locking of the queues is left up to the screen
 *   or uart code.  Multi-line messages to serial consoles may get
 *   interspersed with other messages.
 *
 *   usewrite selects qwrite (may sleep) rather than qiwrite for a
 *   process-context caller.  It is not a putstrn/print semantic.
 */
void
consputstr(char *str, int n, int usewrite)
{
	int m;
	char *t;
	int (*wq)(Queue*, void*, int);

	/*
	 *  how many different output devices do we need?
	 *
	 *  Plan2001: a write that starts with ESC is terminal control (the
	 *  line editor in aux/kbdfs redrawing the input line), not a kernel
	 *  message: keep it out of kmesg, which would otherwise fill up with
	 *  editing echo and push the boot log out.
	 */
	if(n <= 0 || str[0] != '\033')
		kmesgwrite(str, n);

	/*
	 *  if someone is reading /dev/kprint,
	 *  put the message there.
	 *  if not and there's an attached bit mapped display,
	 *  put the message there.
	 *
	 *  if there's a serial line being used as a console,
	 *  put the message there.
	 */
	wq = usewrite && islo() ? qwrite : qiwrite;
	if(kprintoq != nil && !qisclosed(kprintoq))
		(*wq)(kprintoq, str, n);
	else if(screenputs != nil)
		screenputs(str, n);

	if(serialoq == nil){
		uartputs(str, n);
		return;
	}

	while(n > 0) {
		t = memchr(str, '\n', n);
		if(t != nil) {
			m = t-str;
			(*wq)(serialoq, str, m);
			(*wq)(serialoq, "\r\n", 2);
			n -= m+1;
			str = t+1;
		} else {
			(*wq)(serialoq, str, n);
			break;
		}
	}
}

void
putstrn(char *str, int n)
{
	consputstr(str, n, 0);
}

int
print(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	putstrn(buf, n);

	return n;
}

/*
 * Want to interlock iprints to avoid interlaced output on 
 * multiprocessor, but don't want to deadlock if one processor
 * dies during print and another has something important to say.
 * Make a good faith effort.
 */
static Lock iprintlock;
static int
iprintcanlock(Lock *l)
{
	int i;
	
	for(i=0; i<1000; i++){
		if(canlock(l))
			return 1;
		if(l->m == MACHP(m->machno))
			return 0;
		microdelay(100);
	}
	return 0;
}

int
iprint(char *fmt, ...)
{
	int n, s, locked;
	va_list arg;
	char buf[PRINTSIZE];

	s = splhi();
	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	locked = iprintcanlock(&iprintlock);
	if(screenputs != nil && iprintscreenputs)
		screenputs(buf, n);
	uartputs(buf, n);
	if(locked)
		unlock(&iprintlock);
	splx(s);

	return n;
}

_Noreturn void
panic(char *fmt, ...)
{
	int s;
	va_list arg;
	char buf[PRINTSIZE];

	kprintoq = nil;	/* don't try to write to /dev/kprint */

	if(panicking)
		for(;;);
	panicking = 1;

	s = splhi();
	strcpy(buf, "panic: ");
	va_start(arg, fmt);
	vseprint(buf+strlen(buf), buf+sizeof(buf), fmt, arg);
	va_end(arg);
	iprint("%s\n", buf);
	if(consdebug)
		(*consdebug)();
	splx(s);
	prflush();
	dumpstack();

	/* reboot cpu servers and headless machines when not debugging */
	if(getconf("*debug") == nil)
	if(cpuserver || !conf.monitor)
		exit(1);

	/* otherwise, just hang */
	while(islo()) idlehands();
	for(;;);
}

/* libmp at least contains a few calls to sysfatal; simulate with panic */
void
sysfatal(char *fmt, ...)
{
	char err[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(err, err + sizeof err, fmt, arg);
	va_end(arg);
	panic("sysfatal: %s", err);
}

void
_assert(char *fmt)
{
	panic("assert failed at %#p: %s", getcallerpc(&fmt), fmt);
}

/*
 * Legacy per-process stderr output.  pprint is retained because inherited
 * fault/trap paths call it.  The canonical O system has no producer of a
 * first file descriptor, so stderr fd 2 cannot exist: there is no
 * per-process diagnostic channel and pprint returns 0 without output, as
 * it did in this state.  It is not used by the pathless O-only boot.
 */
int
pprint(char *fmt, ...)
{
	USED(fmt);
	return 0;
}

int
readnum(ulong off, char *buf, ulong n, ulong val, int size)
{
	char tmp[64];

	snprint(tmp, sizeof(tmp), "%*lud", size-1, val);
	tmp[size-1] = ' ';
	if(off >= size)
		return 0;
	if(off+n > size)
		n = size-off;
	memmove(buf, tmp+off, n);
	return n;
}

int
readstr(ulong off, char *buf, ulong n, char *str)
{
	int size;

	size = strlen(str);
	if(off >= size)
		return 0;
	if(off+n > size)
		n = size-off;
	memmove(buf, str+off, n);
	return n;
}

/*
 * Initialize the console-adjacent kernel services that are independent of
 * the legacy #c Dev.  todinit() is already called by chandevreset(); the
 * guard here keeps randominit() to one start while allowing the legacy Dev
 * reset slot and the boot core to share this function.
 */
void
consinit(void)
{
	if(consservicesinited)
		return;
	consservicesinited = 1;
	todinit();
	randominit();
	if(getconf("oconsinit") != nil)
		print("cons: init tod=1 random=1\n");
}

/*
 * /dev/kprint is a legacy exclusive console stream.  These operations keep
 * its queue and exclusivity rules independent of Chan/Qid so the #c adapter
 * only supplies open/read/close plumbing.
 *
 * kprintacquire: 0 acquired, -1 already open, -2 allocation failure.
 */
int
kprintacquire(void)
{
	if(tas(&kprintinuse) != 0)
		return -1;
	if(kprintoq == nil){
		kprintoq = qopen(8*1024, Qcoalesce, 0, 0);
		if(kprintoq == nil)
			return -2;
		qnoblock(kprintoq, 1);
	}else
		qreopen(kprintoq);
	return 0;
}

long
kprintread(void *buf, long n)
{
	if(kprintoq == nil)
		return 0;
	return qread(kprintoq, buf, n);
}

void
kprintrelease(void)
{
	kprintinuse = 0;
	if(kprintoq != nil)
		qhangup(kprintoq, nil);
}

void
cpushutdown(void)
{
	int ms, once;

	once = active.machs[m->machno];
	active.machs[m->machno] = 0;
	active.exiting = 1;

	if(once)
		iprint("cpu%d: exiting\n", m->machno);

	/* wait for any other processors to shutdown */
	spllo();
	for(ms = 5*1000; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(memchr(active.machs, 1, MAXMACH) == nil && consactive() == 0)
			break;
	}
}
