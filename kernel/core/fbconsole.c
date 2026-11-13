#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "../core/fbconsolefont.h"

/*
 * Native text console on the UEFI GOP framebuffer.
 *
 * A fixed 12x24 monochrome Latin font with character cells in ordinary
 * RAM. The framebuffer mapped by port/bootfb.c is only written, never
 * read: reads from a write-combining GPU aperture make scrolling slow.
 * The boot marker strip that
 * bootfb.c owns is never touched: text starts at the next whole font
 * row below bootmarkheight().  Only the small ANSI subset the line
 * editor emits is understood; colour/style and private sequences are
 * consumed and ignored.  Unsupported UTF-8 is consumed and shown as '?'.
 *
 * No allocation, no sleeping, no draw library: this must stay usable
 * from panic and interrupt context.  Locking follows the inherited
 * vga.c rule: try once in interrupt context and drop output if busy,
 * spin in process context.
 */

enum {
	Fbwidth	= 12,
	Fbheight = 24,
	Maxparam = 4,
	Maxesc	= 9999,
	Tabwidth = 8,
	Maxcols = 512,
	Maxrows = 256,
};

#define	Fg	0xFFFFFFFF
#define	Bg	0x00000000

enum {
	Escnone,
	Escstart,
	EscCsi,
};

static volatile u32int *fbmem;
static int redrawpending;
static ulong fbwidth, fbstride, fbheight, texttop, textrows, textcols;
static int inited;
typedef struct Textview Textview;
struct Textview {
    uchar cells[Maxcols*Maxrows];
    ulong row, col;
    int state, param[Maxparam], nparam, value, have, private;
    char utfbuf[UTFmax];
    int nbuf;
};
static Textview views[9];
static Textview *view = &views[0];
#define textcells (view->cells)
#define currow (view->row)
#define curcol (view->col)
#define escstate (view->state)
#define escparam (view->param)
#define escnparam (view->nparam)
#define escvalue (view->value)
#define eschave (view->have)
#define escprivate (view->private)
#define utf (view->utfbuf)
#define nutf (view->nbuf)
static int hidden;

static Lock fbconsolelock;
static int activeview; /* 0 = permanent SystemConsole, >0 = process slot */

static void
fbfill(ulong x, ulong y, ulong w, ulong h, u32int color)
{
	ulong i, j, x1, y1;

	if(hidden || fbmem == nil || x >= fbwidth || y >= fbheight || fbstride < fbwidth)
		return;
	x1 = x + w;
	if(x1 > fbwidth || x1 < x)
		x1 = fbwidth;
	y1 = y + h;
	if(y1 > fbheight || y1 < y)
		y1 = fbheight;
	for(j = y; j < y1; j++)
		for(i = x; i < x1; i++)
			fbmem[j*fbstride + i] = color;
}

/*
 * bootfbmem() succeeds only after bootfb.c has checked that the mapping
 * covers fbstride*fbheight*4, so these row multiplications cannot reach
 * outside it; the visible-width check keeps text out of the pitch padding
 * and off the right edge.
 */
static void
fbpixel(ulong x, ulong y, u32int color)
{
	if(hidden || x >= fbwidth || y >= fbheight)
		return;
	fbmem[y*fbstride + x] = color;
}

static void
fbglyph(ulong row, ulong col, int c)
{
	const ushort *g;
	ulong i, j, x, y;
	ushort bits;
	int idx;

	if(fbmem == nil || row >= textrows || col >= textcols)
		return;
	if(c >= 0x80 && c <= 0x8c)
		idx = 95 + c - 0x80;
	else
		idx = c - 0x20;
	if(idx < 0 || idx >= nelem(fbconsolefont))
		idx = '?' - 0x20;
	g = fbconsolefont[idx];
	x = col * Fbwidth;
	y = texttop + row * Fbheight;
	for(j = 0; j < Fbheight; j++){
		bits = g[j];
		for(i = 0; i < Fbwidth; i++)
			fbpixel(x+i, y+j, (bits & (1 << (Fbwidth-1-i)))? Fg: Bg);
	}
}

static void
fbscroll(void)
{
	if(fbmem == nil || textrows == 0)
		return;
	if(textrows > 1)
		memmove(textcells, textcells + textcols, (textrows-1)*textcols);
	memset(textcells + (textrows-1)*textcols, ' ', textcols);
	/* Batch repeated scrolls in one output call into one redraw. */
	redrawpending = 1;
}

/* Sequential pixel stores along each scanline; no framebuffer loads. */
static void
fbredraw(void)
{
	volatile u32int *p;
	ulong row, col, i, j;
	int c;
	ushort bits;

	for(row = 0; row < textrows; row++){
		for(j = 0; j < Fbheight; j++){
			p = fbmem + (texttop + row*Fbheight + j)*fbstride;
			for(col = 0; col < textcols; col++){
				c = textcells[row*textcols + col];
				if(c >= 0x80 && c <= 0x8c)
					c = 95 + c - 0x80;
				else
					c -= 0x20;
				bits = fbconsolefont[c][j];
				for(i = 0; i < Fbwidth; i++)
					*p++ = bits & (1 << (Fbwidth-1-i))? Fg: Bg;
			}
		}
	}
	redrawpending = 0;
}

/* Plan 9 libframe tick geometry: one-pixel stem, 3x3 end caps.
 * Restore from RAM cells, never read the write-combining framebuffer. */
static void
fbtick(int on)
{
    ulong col, x, y, i, j, first, last;
    if(hidden || !inited || textcols == 0 || textrows == 0) return;
    col = curcol < textcols ? curcol : textcols-1;
    x = col*Fbwidth;
    first = col > 0 ? col-1 : col;
    last = col+1 < textcols ? col+1 : col;
    if(!on){
        for(i=first; i<=last; i++)
            fbglyph(currow, i, textcells[currow*textcols+i]);
        return;
    }
    y = texttop + currow*Fbheight;
    for(j=0; j<Fbheight; j++)
        for(i=0; i<3; i++)
            if(i == 1 || j < 3 || j >= Fbheight-3)
                fbpixel(x+i, y+j, Fg);
}

void
fbconsolegeometry(uint *cols, uint *rows)
{
    *cols = textcols;
    *rows = textrows;
}

static void
fberase(ulong row, ulong col, ulong n)
{
	memset(textcells + row*textcols + col, ' ', n);
	if(!redrawpending)
		fbfill(col*Fbwidth, texttop + row*Fbheight, n*Fbwidth, Fbheight, Bg);
}

static void
fbclear(void)
{
	memset(textcells, ' ', textrows*textcols);
	fbfill(0, texttop, fbwidth, textrows*Fbheight, Bg);
	redrawpending = 0;
}

static void
fbnewline(void)
{
	curcol = 0;
	if(currow + 1 < textrows)
		currow++;
	else
		fbscroll();
}

static void
fbputglyph(int c)
{
	if(curcol >= textcols)
		fbnewline();
	if(!((c >= 0x20 && c <= 0x7e) || (c >= 0x80 && c <= 0x8c)))
		return;
	textcells[currow*textcols + curcol] = c;
	if(!redrawpending)
		fbglyph(currow, curcol, c);
	curcol++;
}

static void
escsaveparam(void)
{
	if(escnparam < Maxparam){
		escparam[escnparam] = eschave? escvalue: 0;
		escnparam++;
	}
	escvalue = 0;
	eschave = 0;
}

static ulong
escget(int i, ulong dflt)
{
	int v;

	if(i < 0 || i >= escnparam || i >= Maxparam)
		return dflt;
	v = escparam[i];
	if(v <= 0)
		return dflt;
	return (ulong)v;
}

static void
csidispatch(int c)
{
	ulong n, r, col;

	switch(c){
	case 'C':	/* cursor right */
		n = escget(0, 1);
		if(curcol < textcols){
			if(n > textcols - curcol)
				n = textcols - curcol;
			curcol += n;
		}
		break;
	case 'D':	/* cursor left */
		n = escget(0, 1);
		if(n > curcol)
			n = curcol;
		curcol -= n;
		break;
	case 'H':	/* cursor position, 1-based row;col */
		r = escget(0, 1);
		col = escget(1, 1);
		if(r > textrows)
			r = textrows;
		if(col > textcols)
			col = textcols;
		if(r < 1)
			r = 1;
		if(col < 1)
			col = 1;
		currow = r - 1;
		curcol = col - 1;
		break;
	case 'K':	/* erase to end of line */
		if(curcol < textcols)
			fberase(currow, curcol, textcols-curcol);
		break;
	case 'J':	/* clear the text area only, never the marker strip */
		if(escnparam > 0 && escparam[0] == 2){
			fbclear();
			currow = 0;
			curcol = 0;
		}
		break;
	/* 'm' and everything else: consumed and ignored */
	}
}

/*
 * Feed one byte of a CSI sequence.  Returns 1 if it was consumed.  A
 * malformed byte aborts the sequence and returns 0 so the caller can
 * process it as ordinary text; state stays bounded either way.
 */
static int
escfeed(int c)
{
	if(c == 0x1b){
		escstate = Escstart;
		return 1;
	}
	if(c >= '0' && c <= '9'){
		escvalue = escvalue*10 + c - '0';
		if(escvalue > Maxesc)
			escvalue = Maxesc;
		eschave = 1;
		return 1;
	}
	if(c == ';'){
		escsaveparam();
		return 1;
	}
	if(c == '?' || c == '>' || c == '<' || c == '=' || c == '!'){
		escprivate = 1;
		return 1;
	}
	if(c >= 0x20 && c <= 0x2f){	/* intermediate: unknown */
		escprivate = 1;
		return 1;
	}
	if(c >= 0x40 && c <= 0x7e){	/* final byte */
		escsaveparam();
		if(!escprivate)
			csidispatch(c);
		escstate = Escnone;
		return 1;
	}
	escstate = Escnone;
	return 0;
}

static void
fbcontrol(int c)
{
	ulong n;

	switch(c){
	case '\r':
		curcol = 0;
		break;
	case '\n':
		fbnewline();
		break;
	case '\b':		/* one cell left, erasing nothing */
		if(curcol > 0)
			curcol--;
		break;
	case '\t':
		if(curcol < textcols){
			n = Tabwidth - curcol%Tabwidth;
			if(n > textcols - curcol)
				n = textcols - curcol;
			fberase(currow, curcol, n);
			curcol += n;
		}
		break;
	default:
		if((c >= 0x20 && c <= 0x7e) || (c >= 0x80 && c <= 0x8c))
			fbputglyph(c);
		break;
	}
}

static void
fbconsolechar(int c)
{
	if(escstate == Escstart){
		if(c == '['){
			escstate = EscCsi;
			escnparam = 0;
			escvalue = 0;
			eschave = 0;
			escprivate = 0;
			return;
		}
		escstate = Escnone;
		/* fall through and process the byte as ordinary text */
	}
	if(escstate == EscCsi){
		if(escfeed(c))
			return;
		/* escfeed reset the state: process c as ordinary text */
	}
	if(c == 0x1b){
		escstate = Escstart;
		return;
	}
	fbcontrol(c);
}

static void
fbfallback(void)
{
	escstate = Escnone;
	fbconsolechar('?');
}

static void
fbconsolebytes(char *s, int n)
{
	char *e;
	int c;

	if(s == nil || n <= 0)
		return;
	for(e = s + n; s < e; s++){
		c = *(uchar*)s;
		if(c < Runeself){
			if(nutf != 0){
				nutf = 0;
				fbfallback();
			}
			fbconsolechar(c);
			continue;
		}
		if(nutf >= UTFmax){
			nutf = 0;
			fbfallback();
		}
		utf[nutf++] = c;
		if(fullrune(utf, nutf)){
			Rune r;
			nutf = chartorune(&r, utf);
			nutf = 0;
			switch(r){
			case 0x00e4: fbconsolechar(0x80); break;
			case 0x00c4: fbconsolechar(0x81); break;
			case 0x00f6: fbconsolechar(0x82); break;
			case 0x00d6: fbconsolechar(0x83); break;
			case 0x00e5: fbconsolechar(0x84); break;
			case 0x00c5: fbconsolechar(0x85); break;
			case 0x00a3: fbconsolechar(0x86); break;
			case 0x20ac: fbconsolechar(0x87); break;
			case 0x00a8: fbconsolechar(0x88); break;
			case 0x00b4: fbconsolechar(0x89); break;
			case 0x00a7: fbconsolechar(0x8a); break;
			case 0x00bd: fbconsolechar(0x8b); break;
			case 0x00a4: fbconsolechar(0x8c); break;
			default: fbfallback(); break;
			}
		}
	}

	if(redrawpending && !hidden)
		fbredraw();
}

/* Screen writes never modify a hidden view.  Kernel diagnostics stay
 * separately in kmesg, and are replayed when SystemConsole is selected. */
static void
fbconsoleputs(char *s, int n)
{
	if(!islo()){
		if(!canlock(&fbconsolelock))
			return;
	}else{
		while(!canlock(&fbconsolelock))
			;
	}
	if(activeview == 0 || panicking){
		if(panicking && activeview != 0){
			activeview = 0;
            view = &views[0];
            hidden = 0;
			fbclear();
			currow = curcol = 0;
			escstate = Escnone;
			nutf = 0;
		}
		fbtick(0);
		fbconsolebytes(s, n);
		fbtick(1);
	}
	unlock(&fbconsolelock);
}

/* Both switching and active-session output reuse the existing text renderer,
 * with one kernel-owned framebuffer mapping and separate per-view parser state. */
void
fbconsoleselect(int slot, char *history, int len)
{
	if(!inited || slot < 0 || slot >= 9)
		return;
	lock(&fbconsolelock);
    fbtick(0);
    activeview = slot;
    view = &views[slot];
    hidden = 0;
    if(slot == 0){
        fbclear();
        currow = curcol = 0;
        escstate = Escnone;
        escnparam = escvalue = eschave = escprivate = nutf = 0;
        if(history != nil && len > 0) fbconsolebytes(history, len);
    }
    fbredraw();
    fbtick(1);
	unlock(&fbconsolelock);
}

void
fbconsoleappend(int slot, char *bytes, int len)
{
	if(!inited || slot < 0 || slot >= 9 || bytes == nil || len <= 0)
		return;
	lock(&fbconsolelock);
	if(activeview == slot) fbtick(0);
    view = &views[slot];
    hidden = activeview != slot;
    fbconsolebytes(bytes, len);
    hidden = 0;
    view = &views[activeview];
    if(activeview == slot) fbtick(1);
	unlock(&fbconsolelock);
}

void
fbconsoleinit(void)
{
	u32int *base;
	ulong w, st, h, top, rows, cols;
	char *log;
	int logn, mh;

	if(inited)
		return;
	if(!bootfbmem(&base, &w, &st, &h))
		return;
	if(base == nil || w == 0 || h == 0 || st < w || w < Fbwidth)
		return;

	mh = bootmarkheight();
	if(mh < 0)
		mh = 0;
	top = ((ulong)mh + Fbheight - 1) / Fbheight * Fbheight;
	if(top >= h)
		return;
	rows = (h - top) / Fbheight;
	cols = w / Fbwidth;
	if(rows > Maxrows)
		rows = Maxrows;
	if(cols > Maxcols)
		cols = Maxcols;
	if(rows == 0 || cols == 0)
		return;

	fbmem = base;
	fbwidth = w;
	fbstride = st;
	fbheight = h;
	texttop = top;
	textrows = rows;
	textcols = cols;
	currow = 0;
	curcol = 0;
	escstate = Escnone;
	escnparam = 0;
	escvalue = 0;
	eschave = 0;
	escprivate = 0;
	nutf = 0;

	memset(views, 0, sizeof views);
	{ int i; for(i=0; i<9; i++) memset(views[i].cells, ' ', sizeof views[i].cells); }
	fbclear();

	/* loader log first, then the kernel's, each exactly once */
	if((log = bootlogtext(&logn)) != nil && logn > 0)
		fbconsoleputs(log, logn);
	if(kmesg.n > 0)
		fbconsoleputs(kmesg.buf, (int)kmesg.n);

	screenputs = fbconsoleputs;
	inited = 1;
}
