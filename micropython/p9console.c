/* The console: native keyboard input and the native output streams. */
#include "p9compat.h"
#include <keyboard.h>
#include "../../runtime/abi/obj.h"
#include "p9console.h"

/*
 * The kernel grants the native output authority to the O-server process via
 * its Proc iomask (or an explicit stdout Pipe).  This round replaces the
 * retired IOWRITE(62) syscall with one OBJ console Handle per stream; the
 * Handle carries Vwrite and is opened with OBJOPEN(ObjOpenConsole).
 */
enum {
	Iohout	= 1,
	Iohdiag	= 2,
};

static int nativeout = 0;
static int nativein = 0;
static int kbdh = -1;
static int outh = -1;
static int diagh = -1;

/* Region session transport.  The input region is read-only here (the console
 * owner is its only writer); the output and diagnostic regions are writable
 * here (the owner maps them read-only).  Each region carries an ObjRingHeader
 * and fixed slots. */
static int sessin = -1, sessout = -1, sessdiag = -1;
static char sesserr[128];
static uintptr_t inbase, outbase, diagbase;
static unsigned long long inlast, outseq, diagseq;
static unsigned char inbuf[OBJ_RING_DATA];
static unsigned inbufpos, inbuflen;

static unsigned long long
ring_load(unsigned long long *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void
ring_store(unsigned long long *p, unsigned long long v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static int
ringvalid(uintptr_t base)
{
	ObjRingHeader *h = (ObjRingHeader*)base;

	return h->version == OBJ_RING_VERSION &&
		h->magic == OBJ_RING_MAGIC &&
		h->slot_size == OBJ_RING_SLOT && h->slots == OBJ_RING_SLOTS;
}

static int
ringwrite(uintptr_t base, unsigned long long *seqp, const void *data, size_t n, unsigned flags)
{
	const unsigned char *p = data;
	unsigned long long seq, off;
	unsigned int len;

	while(n != 0){
		len = n > OBJ_RING_DATA ? OBJ_RING_DATA : (unsigned int)n;
		seq = ++*seqp;
		off = OBJ_RING_HEADER +
			((seq-1) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
		ring_store((unsigned long long*)(base+off), 2*seq-1);
		*(volatile unsigned int*)(base+off+8) = len;
		*(volatile unsigned int*)(base+off+12) = flags;
		memmove((void*)(base+off+16), p, len);
		__atomic_thread_fence(__ATOMIC_RELEASE);
		ring_store((unsigned long long*)(base+off), 2*seq);
		ring_store((unsigned long long*)(base+OBJ_RING_PUBOFF), seq);
		region_wake(base, OBJ_RING_PUBOFF, 1);
		p += len;
		n -= len;
	}
	return 0;
}

long
p9_console_attach(int inh, int outhnd, int diaghnd)
{
	uintptr_t ib, ob, db;
	ObjRingHeader *h;

	if(inh < 0 || outhnd < 0)
		return -1;
	ib = (uintptr_t)obj_region_map(inh, 0);
	if((long)ib < 0){
		rerrstr(sesserr, sizeof sesserr);
		return -2;
	}
	ob = (uintptr_t)obj_region_map(outhnd, 0);
	if((long)ob < 0){
		rerrstr(sesserr, sizeof sesserr);
		return -3;
	}
	db = 0;
	if(diaghnd >= 0){
		db = (uintptr_t)obj_region_map(diaghnd, 0);
		if((long)db < 0)
			return -4;
		if(!ringvalid(db))
			return -5;
	}
	if(!ringvalid(ib))
		return -6;
	if(!ringvalid(ob))
		return -7;
	sessin = inh;
	sessout = outhnd;
	sessdiag = diaghnd;
	inbase = ib;
	outbase = ob;
	diagbase = db;
	h = (ObjRingHeader*)ib;
	inlast = ring_load(&h->write_seq);
	h = (ObjRingHeader*)ob;
	outseq = ring_load(&h->write_seq);
	if(db != 0){
		h = (ObjRingHeader*)db;
		diagseq = ring_load(&h->write_seq);
	}else
		diagseq = 0;
	inbufpos = inbuflen = 0;
	return 0;
}

const char *
p9_console_session_error(void)
{
	return sesserr;
}

static int
session_read(void)
{
	ObjRingHeader *h;
	unsigned long long seq, slot;
	unsigned int len;
	long r;

	for(;;){
		if(inbufpos < inbuflen)
			return inbuf[inbufpos++];
		h = (ObjRingHeader*)inbase;
		seq = ring_load(&h->write_seq);
		while(inlast < seq){
			inlast++;
			slot = OBJ_RING_HEADER +
				((inlast-1) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
			if(ring_load((unsigned long long*)(inbase+slot)) != 2*inlast)
				continue;
			len = *(volatile unsigned int*)(inbase+slot+8);
			if(len > OBJ_RING_DATA)
				continue;
			memmove(inbuf, (void*)(inbase+slot+16), len);
			if(ring_load((unsigned long long*)(inbase+slot)) != 2*inlast)
				continue;
			inbufpos = 0;
			inbuflen = len;
			if(len != 0)
				return inbuf[inbufpos++];
		}
		r = region_wait(inbase, OBJ_RING_PUBOFF, inlast, -1);
		if(r == 3)
			return -1;	/* writer/session ended */
		if(r < 0)
			return -2;
	}
}

static int
console_open(int stream)
{
	ObjOpenConsoleReq req;

	req.stream = stream;
	req.reserved = 0;
	return (int)obj_open(ObjOpenConsole, &req, sizeof req, 0, nil);
}

void
p9_console_native(int on)
{
	nativeout = on ? -1 : 0;
}

/* A spawned child may have the native output grant without an -O volume.
 * Probe it without writing: the grant itself authorizes the stream. */
void
p9_console_probe(void)
{
	/* Manifest boot: the kernel owns focus, keyboard and three region
	 * streams.  Open only our own process-local capability grants. */
	int ih = console_open(ObjConsoleSessionInput);
	if(ih >= 0){
		int oh = console_open(ObjConsoleSessionOutput);
		int dh = console_open(ObjConsoleSessionDiag);
		if(oh >= 0 && dh >= 0 && p9_console_attach(ih, oh, dh) == 0){
			nativeout = 0;
			return;
		}
		if(dh >= 0) obj_close(dh);
		if(oh >= 0) obj_close(oh);
		obj_close(ih);
	}
	if(nativeout == 0){
		if(outh < 0)
			outh = console_open(Iohout);
		if(outh >= 0)
			nativeout = 1;
	}
}

int
p9_native_output(void)
{
	if(nativeout < 0){
		if(outh < 0)
			outh = console_open(Iohout);
		nativeout = outh >= 0;
	}
	return nativeout;
}

void p9_console_restore(void) {
    sessin = sessout = sessdiag = -1;
    if (kbdh >= 0) {
        obj_close(kbdh);
        kbdh = -1;
    }
    nativein = 0;
}

// The native keyboard is raw by construction; there is no consctl channel.
void p9_console_raw(int on) {
    (void)on;
}

// Bytes still to hand out: a key's escape sequence, or a rune passed on.
static char pend[8];
static int npend, ipend;

static int pending(char *b, int n) {
    memmove(pend, b, n);
    npend = n;
    ipend = 1;
    return (uchar)pend[0];
}

enum {
	KeymapSize = 128,
};

/* Finnish PC QWERTY (PS/2 set 1), including the standard AltGr symbols. */
static uchar keymap[2][KeymapSize] = {
	[0] {
	[0x01]	0x1b,	'1',	'2',	'3',	'4',	'5',	'6',	'7',
	[0x09]	'8',	'9',	'0',	'+',	0,	'\b',	'\t',	'q',
	[0x11]	'w',	'e',	'r',	't',	'y',	'u',	'i',	'o',
	[0x19]	'p',	0,	0,	'\r',	0,	'a',	's',	'd',
	[0x21]	'f',	'g',	'h',	'j',	'k',	'l',	0,	0,
	[0x29]	0,	0,	'\'',	'z',	'x',	'c',	'v',	'b',
	[0x31]	'n',	'm',	',',	'.',	'-',	0,	'*',	0,
	[0x39]	' ',	0,	0,	0,	0,	0,	0,	0,
	[0x41]	0,	0,	0,	0,	0,	0,	'7',	'8',
	[0x49]	'9',	'-',	'4',	'5',	'6',	'+',	'1',	'2',
	[0x51]	'3',	'0',	'.',	0,	0,	'<',	0,	0,
	},
	[1] {
	[0x01]	0x1b,	'!',	'"',	'#',	0,	'%',	'&',	'/',
	[0x09]	'(',	')',	'=',	'?',	0,	'\b',	'\t',	'Q',
	[0x11]	'W',	'E',	'R',	'T',	'Y',	'U',	'I',	'O',
	[0x19]	'P',	0,	0,	'\r',	0,	'A',	'S',	'D',
	[0x21]	'F',	'G',	'H',	'J',	'K',	'L',	0,	0,
	[0x29]	0,	0,	'*',	'Z',	'X',	'C',	'V',	'B',
	[0x31]	'N',	'M',	';',	':',	'_',	0,	'*',	0,
	[0x39]	' ',	0,	0,	0,	0,	0,	0,	0,
	[0x41]	0,	0,	0,	0,	0,	0,	'7',	'8',
	[0x49]	'9',	'-',	'4',	'5',	'6',	'+',	'1',	'2',
	[0x51]	'3',	'0',	'.',	0,	0,	'>',	0,	0,
	},
};

static int kbd_shiftl, kbd_shiftr, kbd_ctl, kbd_caps, kbd_num, kbd_altgr;
static int kbd_e0, kbd_skip;

static char *
navseq(int sc, int *n)
{
	switch(sc){
	case 0x47: *n = 3; return "\x1b[H";
	case 0x48: *n = 3; return "\x1b[A";
	case 0x4b: *n = 3; return "\x1b[D";
	case 0x4d: *n = 3; return "\x1b[C";
	case 0x4f: *n = 3; return "\x1b[F";
	case 0x50: *n = 3; return "\x1b[B";
	case 0x53: *n = 4; return "\x1b[3~";
	}
	return nil;
}

/* -1: no byte; otherwise the byte, with any rest queued in pend. */
static int
keydecode(int c)
{
	int down, sc, b, up, n;
	char *s;

	if(kbd_skip > 0){
		kbd_skip--;
		return -1;
	}
	if(c == 0xe1){		/* Pause: E1 1D 45 E1 9D C5 */
		kbd_e0 = 0;
		kbd_skip = 5;
		return -1;
	}
	if(c == 0xe2){		/* unsupported three-byte prefix */
		kbd_e0 = 0;
		kbd_skip = 2;
		return -1;
	}
	if(c == 0xe0){
		kbd_e0 = 1;
		return -1;
	}

	down = (c & 0x80) == 0;
	sc = c & 0x7f;

	if(kbd_e0){
		kbd_e0 = 0;
		if(sc == 0x1d){		/* right Ctrl */
			kbd_ctl = down;
			return -1;
		}
		if(sc == 0x38){
			kbd_altgr = down;
			return -1;
		}
		if(sc == 0x2a || sc == 0x36)
			return -1;
		if(!down)
			return -1;
		if(sc == 0x1c)		/* keypad Enter */
			return '\r';
		if(sc == 0x35)		/* keypad / */
			return '/';
		if((s = navseq(sc, &n)) != nil)
			return pending(s, n);
		return -1;
	}

	if(sc == 0x2a){
		kbd_shiftl = down;
		return -1;
	}
	if(sc == 0x36){
		kbd_shiftr = down;
		return -1;
	}
	if(sc == 0x1d){
		kbd_ctl = down;
		return -1;
	}
	if(sc == 0x38)		/* left Alt: unsupported */
		return -1;
	if(sc == 0x3a){
		if(down)
			kbd_caps ^= 1;
		return -1;
	}
	if(sc == 0x45){
		if(down)
			kbd_num ^= 1;
		return -1;
	}
	if(!down)
		return -1;
	/* QEMU sends the keypad navigation codes without E0. */
	if(!kbd_num && (s = navseq(sc, &n)) != nil)
		return pending(s, n);

	b = keymap[0][sc];
	if(b == 0 && keymap[1][sc] == 0
	&& sc != 0x0d && sc != 0x1a && sc != 0x1b
	&& sc != 0x27 && sc != 0x28)
		return -1;
	if(kbd_ctl){
		if(b >= 'a' && b <= 'z')
			return b - 'a' + 1;
		switch(sc){
		case 0x1a: return 0x1b;	/* Ctrl-[ */
		case 0x1b: return 0x1d;	/* Ctrl-] */
		case 0x2b: return 0x1c;	/* Ctrl-\ */
		}
		return -1;
	}
	if(kbd_altgr){
		switch(sc){
		case 0x03: return '@';
		case 0x04: return pending("\xc2\xa3", 2); /* £ */
		case 0x05: return '$';
		case 0x06: return pending("\xe2\x82\xac", 3); /* € */
		case 0x08: return '{';
		case 0x09: return '[';
		case 0x0a: return ']';
		case 0x0b: return '}';
		case 0x0c: return '\\';
		case 0x56: return '|';
		}
	}
	if(sc == 0x1a) return pending(kbd_shiftl || kbd_shiftr ? "\xc3\x85" : "\xc3\xa5", 2); /* Åå */
	if(sc == 0x27) return pending(kbd_shiftl || kbd_shiftr ? "\xc3\x96" : "\xc3\xb6", 2); /* Öö */
	if(sc == 0x28) return pending(kbd_shiftl || kbd_shiftr ? "\xc3\x84" : "\xc3\xa4", 2); /* Ää */
	if(sc == 0x1b) return pending("\xc2\xa8", 2); /* ¨ */
	if(sc == 0x0d) return pending("\xc2\xb4", 2); /* ´ */
	if(b >= 'a' && b <= 'z'){
		up = kbd_caps ^ (kbd_shiftl || kbd_shiftr);
		return up ? b - 'a' + 'A' : b;
	}
	if(kbd_shiftl || kbd_shiftr)
		b = keymap[1][sc] ? keymap[1][sc] : b;
	return b ? b : -1;
}

/* -1 at EOF, -2 on a read error. */
static int
native_read(void)
{
	uchar c;
	long n;
	int b;

	for(;;){
		if(kbdh < 0)
			return -2;
		n = obj_read(kbdh, &c, 1, 0);
		if(n == 0)
			return -1;
		if(n < 0)
			return -2;
		if((b = keydecode(c)) >= 0)
			return b;
	}
}

int
p9_console_init_native(void)
{
	ObjDeviceOpenOptions opt;

	if(sessin >= 0)
		return 0;		/* clients read the granted session region */
	if(nativein)
		return 0;
	memset(&opt, 0, sizeof opt);
	opt.version = OBJ_DEVICE_OPT_VERSION;
	opt.flags = OBJ_DEVICE_KBD_EXCLUSIVE;
	kbdh = obj_catalog_device_open(ObjDeviceKbd, 0, 0, 0, &opt,
		sizeof opt);
	if(kbdh < 0)
		return -1;
	nativein = 1;
	kbd_shiftl = kbd_shiftr = kbd_ctl = kbd_caps = kbd_num = kbd_altgr = 0;
	kbd_e0 = kbd_skip = 0;
	npend = ipend = 0;
	return 0;
}

// -1 at EOF/without the native keyboard, -2 on a read error.
int p9_console_read(void) {
    if (ipend < npend) return (uchar)pend[ipend++];
    if (sessin >= 0) return session_read();
    if (!nativein) return -1;
    return native_read();
}

/* VM-loop hook: consume one pending Ctrl-C record from the session input and
 * let MicroPython raise KeyboardInterrupt.  Normal typed-ahead input is left
 * untouched for the REPL to read. */
int p9_console_poll_interrupt(void)
{
	ObjRingHeader *h;
	unsigned long long seq, slot;
	unsigned int len, flags;
	unsigned char c;

	if(sessin < 0)
		return 0;
	if(inbufpos < inbuflen){
		if(inbuf[inbufpos] != 3)
			return 0;
		inbufpos = inbuflen;
		return 1;
	}
	h = (ObjRingHeader*)inbase;
	seq = ring_load(&h->write_seq);
	if(inlast >= seq)
		return 0;
	slot = OBJ_RING_HEADER +
		((inlast) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
	if(ring_load((unsigned long long*)(inbase+slot)) != 2*(inlast+1))
		return 0;
	len = *(volatile unsigned int*)(inbase+slot+8);
	flags = *(volatile unsigned int*)(inbase+slot+12);
	if(len != 1 || flags != 0)
		return 0;
	c = *(volatile unsigned char*)(inbase+slot+16);
	if(c != 3)
		return 0;
	inlast++;
	return 1;
}

int p9_console_write(const char *data, size_t size) {
    long n;
    if (sessout >= 0)
        return ringwrite(outbase, &outseq, data, size, 0);
    if (p9_native_output()) {
        while (size) {
            size_t m = size > 4096 ? 4096 : size;
            n = obj_write(outh, (void *)data, (unsigned long long)m, 0);
            if (n != (long)m) return -1;
            data += m;
            size -= m;
        }
        return 0;
    }
    return -1;
}

/*
 * Bounded diagnostic formatter using the authorized DIAGNOSTIC Handle.
 */
void
p9_dprintf(char *fmt, ...)
{
    char buf[2048];
    va_list va;
    int n;

    va_start(va, fmt);
    n = vsnprint(buf, sizeof buf, fmt, va);
    va_end(va);
    if (n <= 0) return;
    if (sessdiag >= 0) {
        ringwrite(diagbase, &diagseq, buf, (size_t)n, 1);
        return;
    }
    if (sessout >= 0) {
        ringwrite(outbase, &outseq, buf, (size_t)n, 1);
        return;
    }
    if (diagh < 0)
        diagh = console_open(Iohdiag);
    if (diagh >= 0)
        obj_write(diagh, buf, (unsigned long long)n, 0);
}

unsigned long long p9_console_msec(void) {
    return (unsigned long long)nsec() / 1000000;
}

void p9_console_exit(int failed) {
    p9_console_restore();
    exits(failed ? "micropython" : nil);
}
