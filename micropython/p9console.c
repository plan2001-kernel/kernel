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
	if(nativein)
		return 0;
	kbdh = obj_kbd_open();
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
    if (!nativein) return -1;
    return native_read();
}

int p9_console_write(const char *data, size_t size) {
    long n;
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
