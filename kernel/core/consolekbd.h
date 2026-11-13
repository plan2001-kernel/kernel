/* PS/2 set 1 console decoder; derived from the former p9console decoder. */
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

static int kbd_us, kbd_alt, kbd_capsdown, kbd_numdown;
static int kbd_shiftl, kbd_shiftr, kbd_ctll, kbd_ctlr, kbd_caps, kbd_num, kbd_altgr;
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
			kbd_ctlr = down;
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
		kbd_ctll = down;
		return -1;
	}
	if(sc == 0x38){ kbd_alt = down; return -1; }
	if(sc == 0x3a){
		if(down && !kbd_capsdown)
			kbd_caps ^= 1;
        kbd_capsdown = down;
		return -1;
	}
	if(sc == 0x45){
		if(down && !kbd_numdown)
			kbd_num ^= 1;
        kbd_numdown = down;
		return -1;
	}
	if(!down)
		return -1;
	/* QEMU sends the keypad navigation codes without E0. */
	if(!kbd_num && (s = navseq(sc, &n)) != nil)
		return pending(s, n);

	if(kbd_us && !kbd_ctll && !kbd_ctlr){
        static uchar us[128] = {
            [0x0c]='-', [0x0d]='=', [0x1a]='[', [0x1b]=']',
            [0x27]=';', [0x28]='\'', [0x29]='`', [0x2b]='\\',
            [0x35]='/', [0x56]='\\',
        };
        static uchar ush[128] = {
            [0x03]='@', [0x04]='#', [0x05]='$', [0x07]='^',
            [0x08]='&', [0x09]='*', [0x0a]='(', [0x0b]=')',
            [0x0c]='_', [0x0d]='+', [0x1a]='{', [0x1b]='}',
            [0x27]=':', [0x28]='"', [0x29]='~', [0x2b]='|',
            [0x35]='?', [0x56]='|',
        };
        int v = (kbd_shiftl || kbd_shiftr) ? ush[sc] : us[sc];
        if(v) return v;
    }
    b = keymap[0][sc];
	if(b == 0 && keymap[1][sc] == 0
	&& sc != 0x0d && sc != 0x1a && sc != 0x1b
	&& sc != 0x27 && sc != 0x28 && sc != 0x29)
		return -1;
	if(kbd_ctll || kbd_ctlr){
		if(b >= 'a' && b <= 'z')
			return b - 'a' + 1;
		switch(sc){
		case 0x1a: return 0x1b;	/* Ctrl-[ */
		case 0x1b: return 0x1d;	/* Ctrl-] */
		case 0x2b: return 0x1c;	/* Ctrl-\ */
		}
		return -1;
	}
	if(kbd_altgr && !kbd_us){
		switch(sc){
		case 0x12: return pending("\xe2\x82\xac", 3);
        case 0x1b: return '~';
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
	if(!kbd_us && sc == 0x1a) return pending((kbd_caps ^ (kbd_shiftl || kbd_shiftr)) ? "\xc3\x85" : "\xc3\xa5", 2); /* Åå */
	if(!kbd_us && sc == 0x27) return pending((kbd_caps ^ (kbd_shiftl || kbd_shiftr)) ? "\xc3\x96" : "\xc3\xb6", 2); /* Öö */
	if(!kbd_us && sc == 0x28) return pending((kbd_caps ^ (kbd_shiftl || kbd_shiftr)) ? "\xc3\x84" : "\xc3\xa4", 2); /* Ää */
	if(!kbd_us && sc == 0x1b) return (kbd_shiftl || kbd_shiftr) ? '^' : pending("\xc2\xa8", 2); /* ¨ */
	if(!kbd_us && sc == 0x0d) return (kbd_shiftl || kbd_shiftr) ? '`' : pending("\xc2\xb4", 2); /* ´ */
	if(b >= 'a' && b <= 'z'){
		up = kbd_caps ^ (kbd_shiftl || kbd_shiftr);
		return up ? b - 'a' + 'A' : b;
	}
	if(!kbd_us && sc == 0x05 && (kbd_shiftl || kbd_shiftr)) return pending("\xc2\xa4", 2);
	if(!kbd_us && sc == 0x29) return pending((kbd_shiftl || kbd_shiftr) ? "\xc2\xbd" : "\xc2\xa7", 2);
	if(kbd_shiftl || kbd_shiftr)
		b = keymap[1][sc] ? keymap[1][sc] : b;
	return b ? b : -1;
}
