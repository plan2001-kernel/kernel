/*
 * Minimal boot console.  Manifest process roots have independent input,
 * output and diagnostic capability rings.  The kernel owns PS/2 and GOP,
 * never a userspace console-manager process.
 *
 * SystemConsole is always slot 0; Ctrl+Alt+F1 returns to it, F2..F9
 * focus the eight manifest sessions.  The kernel console is a log view,
 * not a new kernel command interpreter.  No UART recovery or network here.
 */
#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../core/error.h"
#include "../core/vobjint.h"
#include "kbdapi.h"
#include "../../runtime/abi/obj.h"

enum { Nbootconsole = 8, History = 8192 };

typedef struct Bootconsole Bootconsole;
struct Bootconsole {
	Vobj *in, *out, *diag;
	uvlong inseq, outseq, diagseq;
	ulong pid;
	char name[32];
	char history[History];
	int used, live, length;
};
static Bootconsole consoles[Nbootconsole];
static int focus;		/* 0 = kernel, i+1 = manifest entry i */
static int redraw;
static int started;
static int traceoutput;
static int keyboardheld;
static int keyctrl, keyalt, keyshift, keycaps, keye0;
static char frame[OBJ_RING_DATA];
static char kernelview[History];

extern void fbconsolewrite(char*, int);
extern int fbconsoleavailable(void);
extern void (*screenputs)(char*, int);

/* Kernel messages must stay in kmesg regardless of the visible session.
 * A panic must always be displayed even if a REPL had keyboard focus. */
static void
systemscreeen(char *s, int n)
{
	if(panicking)
		focus = 0;
	if(focus == 0 || panicking)
		fbconsolewrite(s, n);
}

static void
historyappend(Bootconsole *b, char *s, int n)
{
	int d;

	if(n >= History){
		s += n-History;
		n = History;
	}
	if(b->length+n > History){
		d = b->length+n-History;
		memmove(b->history, b->history+d, b->length-d);
		b->length -= d;
	}
	memmove(b->history+b->length, s, n);
	b->length += n;
}

static void
showfocus(void)
{
	long n, off;

	fbconsolewrite("\033[2J\033[H", 7);
	if(focus == 0){
		off = kmesg.n > History ? kmesg.n-History : 0;
		n = kmesgreadat(kernelview, sizeof kernelview, off);
		if(n > 0)
			fbconsolewrite(kernelview, n);
	}else if(focus <= Nbootconsole && consoles[focus-1].used){
		Bootconsole *b = &consoles[focus-1];
		if(b->length > 0)
			fbconsolewrite(b->history, b->length);
	}
	redraw = 0;
}

static void
setfocus(int target)
{
	char label[96];

	if(target < 0 || target > Nbootconsole ||
	   (target != 0 && !consoles[target-1].live))
		return;
	focus = target;
	redraw = 1;
	if(target == 0)
		print("boot-console: FOCUS kernel\n");
	else{
		snprint(label, sizeof label, "boot-console: FOCUS %s\n",
			consoles[target-1].name);
		print("%s", label);
	}
}

/* PS/2 set 1 subset sufficient for the native MicroPython line editor.
 * Unlike the old userspace keyboard path, control-focus keys cannot be
 * intercepted by a stuck REPL.  The kernel owns the raw keyboard claim. */
static int
keyascii(int sc)
{
	static char normal[128] = {
		[0x01]=27, [0x02]='1', [0x03]='2', [0x04]='3',
		[0x05]='4', [0x06]='5', [0x07]='6', [0x08]='7',
		[0x09]='8', [0x0a]='9', [0x0b]='0', [0x0c]='-',
		[0x0d]='=', [0x0e]=8, [0x0f]=9,
		[0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r',
		[0x14]='t', [0x15]='y', [0x16]='u', [0x17]='i',
		[0x18]='o', [0x19]='p', [0x1a]='[', [0x1b]=']',
		[0x1c]='\r', [0x1e]='a', [0x1f]='s', [0x20]='d',
		[0x21]='f', [0x22]='g', [0x23]='h', [0x24]='j',
		[0x25]='k', [0x26]='l', [0x27]=';', [0x28]='\'',
		[0x29]='`', [0x2b]='\\', [0x2c]='z', [0x2d]='x',
		[0x2e]='c', [0x2f]='v', [0x30]='b', [0x31]='n',
		[0x32]='m', [0x33]=',', [0x34]='.', [0x35]='/',
		[0x39]=' ',
	};
	static char shifted[128] = {
		[0x02]='!', [0x03]='@', [0x04]='#', [0x05]='$',
		[0x06]='%', [0x07]='^', [0x08]='&', [0x09]='*',
		[0x0a]='(', [0x0b]=')', [0x0c]='_', [0x0d]='+',
		[0x1a]='{', [0x1b]='}', [0x27]=':', [0x28]='"',
		[0x29]='~', [0x2b]='|', [0x33]='<', [0x34]='>',
		[0x35]='?',
	};
	int c;
	if(sc < 0 || sc >= 128)
		return -1;
	c = normal[sc];
	if(c == 0)
		return -1;
	if(keyctrl && c >= 'a' && c <= 'z')
		return c-'a'+1;
	if(c >= 'a' && c <= 'z')
		return (keyshift ^ keycaps) ? c-'a'+'A' : c;
	if(keyshift && shifted[sc] != 0)
		return shifted[sc];
	return c;
}

static void
keyevent(uchar code)
{
	int sc, down, c;
	char byte;

	if(code == 0xe0){
		keye0 = 1;
		return;
	}
	if(code == 0xe1){
		keye0 = 0;
		return;
	}
	down = (code & 0x80) == 0;
	sc = code & 0x7f;
	if(sc == 0x1d){ keyctrl = down; keye0 = 0; return; }
	if(sc == 0x38){ keyalt = down; keye0 = 0; return; }
	if(!keye0 && (sc == 0x2a || sc == 0x36)){
		keyshift = down; return;
	}
	if(keye0){
		keye0 = 0;
		return;
	}
	if(sc == 0x3a){
		if(down) keycaps ^= 1;
		return;
	}
	if(!down)
		return;
	if(keyctrl && keyalt && sc >= 0x3b && sc <= 0x43){
		setfocus(sc-0x3b);
		return;
	}
	if(focus <= 0 || focus > Nbootconsole || !consoles[focus-1].live)
		return;
	c = keyascii(sc);
	if(c < 0)
		return;
	byte = c;
	vobjregionringpublish(consoles[focus-1].in,
		&consoles[focus-1].inseq, &byte, 1);
}

static void
pump(Bootconsole *b, Vobj *r, uvlong *last)
{
	uvlong newest, seq;
	ulong n;

	if(r == nil)
		return;
	newest = vobjregionringload(r, OBJ_RING_PUBOFF);
	if(newest < *last)
		return;
	if(newest-*last > OBJ_RING_SLOTS)
		*last = newest-OBJ_RING_SLOTS;
	while(*last < newest){
		seq = ++*last;
		if(vobjregionringconsume(r, seq, frame, sizeof frame, &n)){
			historyappend(b, frame, n);
			if(focus == (b-consoles)+1 && !redraw)
				fbconsolewrite(frame, n);
			if(traceoutput)
				uartputs(frame, n);
		}
		vobjregionringack(r, seq);
	}
}

/* The queue read can sleep waiting for a key.  Do not perform it in the
 * output pump: a silent keyboard must never block every process stdout. */
static void
keyboardworker(void *arg)
{
	int j, n;
	uchar keys[32];

	USED(arg);
	keyboardheld = kbdscancodeacquire() == 0;
	print("boot-console: SYSTEM READY framebuffer=%d keyboard=%d\n",
		fbconsoleavailable(), keyboardheld);
	if(!keyboardheld)
		pexit("PS/2 keyboard unavailable");
	for(;;){
		n = kbdscancoderead(keys, sizeof keys);
		if(n > 0)
			for(j=0; j<n; j++)
				keyevent(keys[j]);
		else
			tsleep(&up->sleep, return0, nil, 10);
	}
}

/* The output worker always progresses independently of keyboard input. */
static void
consoleworker(void *arg)
{
	int i;

	USED(arg);
	for(;;){
		for(i=0; i<Nbootconsole; i++){
			Bootconsole *b = &consoles[i];
			if(!b->used)
				continue;
			pump(b, b->out, &b->outseq);
			pump(b, b->diag, &b->diagseq);
		}
		if(redraw)
			showfocus();
		tsleep(&up->sleep, return0, nil, 10);
	}
}

void
bootconsolestart(void)
{
	if(started)
		return;
	started = 1;
	traceoutput = getconf("bootconsoletrace") != nil;
	screenputs = systemscreeen;
	kproc("boot-console", consoleworker, nil);
	kproc("boot-keyboard", keyboardworker, nil);
}

/* Called only by the manifest's trusted kproc before its user transition.
 * Objects are retained in the bounded kernel table for the boot lifetime. */
int
bootconsolecreate(int slot, char *name, ulong pid)
{
	Bootconsole *b;

	if(slot < 0 || slot >= Nbootconsole || name == nil)
		return -1;
	b = &consoles[slot];
	if(b->used)
		return -1;
	b->in = vobjregionalloc(OBJ_RING_BYTES);
	b->out = vobjregionalloc(OBJ_RING_BYTES);
	b->diag = vobjregionalloc(OBJ_RING_BYTES);
	if(b->in == nil || b->out == nil || b->diag == nil)
		return -1;
	if(vobjregionringinit(b->in) < 0 ||
	   vobjregionringinit(b->out) < 0 ||
	   vobjregionringinit(b->diag) < 0 ||
	   vobjregionwriterhold(b->in) < 0)
		return -1;
	b->pid = pid;
	snprint(b->name, sizeof b->name, "%s", name);
	coherence();
	b->live = b->used = 1;
	print("boot-console: SESSION %s pid=%lud slot=%d\n", name, pid, slot+1);
	return 0;
}

/* The only way to acquire a boot session is from its own PID.  This is not
 * a grant by OwnerID, inherited parentage or guessed slot number. */
int
bootconsolegrant(int stream, uvlong requested, VobjId *id)
{
	Bootconsole *b = nil;
	Vobj *o;
	int i, rights, h;

	for(i=0; i<Nbootconsole; i++)
		if(consoles[i].used && consoles[i].live &&
		   consoles[i].pid == up->pid){
			b = &consoles[i];
			break;
		}
	if(b == nil)
		error(Eperm);
	switch(stream){
	case ObjConsoleSessionInput:
		o = b->in;
		rights = Vread|Vmeta;
		break;
	case ObjConsoleSessionOutput:
		o = b->out;
		rights = Vwrite|Vmeta;
		break;
	case ObjConsoleSessionDiag:
		o = b->diag;
		rights = Vwrite|Vmeta;
		break;
	default:
		error(Ebadarg);
		return -1;
	}
	if(requested == 0)
		requested = rights;
	if((requested & ~(uvlong)rights) != 0 || requested == 0)
		error(Eperm);
	h = vobjinstallobj(o, (int)requested);
	if(id != nil){
		id->incarnation = o->incarnation;
		id->space = o->space;
		id->oid = o->oid;
	}
	return h;
}

void
bootconsoleexit(Proc *p)
{
	Bootconsole *b;
	int i;

	if(!started || p == nil)
		return;
	for(i=0; i<Nbootconsole; i++){
		b = &consoles[i];
		if(!b->used || b->pid != p->pid)
			continue;
		b->live = 0;
		coherence();
		vobjregionwriterrelease(b->in);
		if(focus == i+1){
			focus = 0;
			redraw = 1;
		}
		print("boot-console: EXIT %s pid=%lud\n", b->name, p->pid);
		break;
	}
}
