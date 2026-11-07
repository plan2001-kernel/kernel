/*
 * keyboard input
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
#include	"kbdapi.h"

enum {
	Data=		0x60,		/* data port */

	Status=		0x64,		/* status port */
	 Inready=	0x01,		/*  input character ready */
	 Outbusy=	0x02,		/*  output busy */
	 Sysflag=	0x04,		/*  system flag */
	 Cmddata=	0x08,		/*  cmd==0, data==1 */
	 Inhibit=	0x10,		/*  keyboard/mouse inhibited */
	 Minready=	0x20,		/*  mouse character ready */
	 Rtimeout=	0x40,		/*  general timeout */
	 Parity=	0x80,

	Cmd=		0x64,		/* command port (write only) */
};

enum
{
	/* controller command byte */
	Cscs1=		(1<<6),		/* scan code set 1 */
	Cauxdis=	(1<<5),		/* mouse disable */
	Ckbddis=	(1<<4),		/* kbd disable */
	Csf=		(1<<2),		/* system flag */
	Cauxint=	(1<<1),		/* mouse interrupt enable */
	Ckbdint=	(1<<0),		/* kbd interrupt enable */
};

static Lock i8042lock;
static Timer *polltimer;
static int ccc;
static void (*auxputc)(int, int);
static int nokbd = 1;			/* flag: no PS/2 keyboard */
static int kbdinitialized;

static struct {
	Ref ref;
	Queue *q;
} kbd;

/*
 *  wait for output no longer busy
 */
static int
outready(void)
{
	int tries, status;

	for(tries = 0; ((status = inb(Status)) & Outbusy); tries++){
		if(tries > 500)
			return -1;
		delay(2);
	}
	return status;
}

/*
 *  wait for input
 */
static int
inready(void)
{
	int tries, status;

	for(tries = 0; !((status = inb(Status)) & Inready); tries++){
		if(tries > 500)
			return -1;
		delay(2);
	}
	return status;
}

static int
ackdata(int status)
{
	if(status < 0 || (status & Inready) == 0)
		return -1;

	return inb(Data);
}

static int
indata(void)
{
	return ackdata(inready());
}

/*
 *  wait for a quiescent controller
 */
static int
flushdata(void)
{
	int tries, status;

	for(tries = 0; (status = inb(Status)) & (Outbusy | Inready); tries++){
		ackdata(status);
		if(tries > 1000)
			return -1;
		delay(1);
	}
	return 0;
}

static int
outdata(int data)
{
	if(outready() < 0)
		return -1;
	outb(Data, data);
	return 0;
}

static int
outcmd(int cmd, int data)
{
	if(outready() < 0)
		return -1;
	outb(Cmd, cmd);
	if(data >= 0 && outdata(data) < 0)
		return -1;
	if(outready() < 0)
		return -1;
	return 0;
}

static int
readccc(void)
{
	if(outcmd(0x20, -1) < 0)
		return -1;
	return indata();
}

static int
writeccc(int data)
{
	return outcmd(0x60, data);
}

/*
 *  ask 8042 to reset the machine
 */
void
i8042reset(void)
{
	int i, x;

	if(nokbd)
		return;

	*((ushort*)KADDR(0x472)) = 0x1234;	/* BIOS warm-boot flag */

	/*
	 *  newer reset the machine command
	 */
	ilock(&i8042lock);
	outcmd(0xFE, -1);
	iunlock(&i8042lock);

	/*
	 *  Pulse it by hand (old somewhat reliable)
	 */
	x = 0xDF;
	for(i = 0; i < 5; i++){
		ilock(&i8042lock);
		x ^= 1;
		outcmd(0xD1, x);
		iunlock(&i8042lock);

		delay(100);
	}
}

int
i8042auxcmd(int cmd)
{
	int c, tries;

	c = 0;
	tries = 0;
	cmd &= 0xFF;

	if(nokbd)
		return -1;

	ilock(&i8042lock);
	do{
		if(tries++ > 2)
			break;
		if(outcmd(0xD4, cmd) < 0)
			break;
		if((c = indata()) < 0)
			break;
	} while(c == 0xFE || c == 0);
	iunlock(&i8042lock);

	if(c != 0xFA){
		print("i8042: %2.2x returned to the %2.2x command (pc=%#p)\n",
			c, cmd, getcallerpc(&cmd));
		return -1;
	}
	return 0;
}

/*
 * set keyboard's leds for lock states (scroll, numeric, caps).
 *
 * at least one keyboard (from Qtronics) also sets its numeric-lock
 * behaviour to match the led state, though it has no numeric keypad,
 * and some BIOSes bring the system up with numeric-lock set and no
 * setting to change that.  this combination steals the keys for these
 * characters and makes it impossible to generate them: uiolkjm&*().
 * thus we'd like to be able to force the numeric-lock led (and behaviour) off.
 */
void
kbdsetleds(int leds)
{
	static int old = -1;

	if(nokbd || leds == old)
		return;

	leds &= 7;
	ilock(&i8042lock);
	for(;;){
		if(outdata(0xED) < 0)	/* `reset keyboard lock states' */
			break;
		if(outdata(leds) < 0)
			break;
		old = leds;
		break;
	}
	iunlock(&i8042lock);
}

/*
 *  keyboard interrupt
 */
static void
i8042intr(Ureg*, void*)
{
	int tries, status;
	uchar c;

	for(tries=0; tries<16; tries++){
		ilock(&i8042lock);
		status = inb(Status);
		if(!(status&Inready)){
			iunlock(&i8042lock);
			break;
		}
		c = ackdata(status);
		iunlock(&i8042lock);

		/*
		 *  if it's the aux port...
		 */
		if(status & Minready){
			if(auxputc != nil)
				auxputc(c, 0);
		} else {
			qproduce(kbd.q, &c, 1);
		}
	}
}

void
i8042auxenable(void (*putc)(int, int))
{
	if(nokbd)
		error("no PS2 controller");

	ilock(&i8042lock);

	/* already enabled? */
	if((ccc & (Cauxdis|Cauxint)) == Cauxint){
		auxputc = putc;
		iunlock(&i8042lock);
		return;
	}

	/* enable kbd/aux xfers and interrupts */
	ccc &= ~Cauxdis;
	ccc |= Cauxint;
	if(writeccc(ccc) < 0){
		iunlock(&i8042lock);
		print("i8042: aux init failed: writing ccc\n");
		return;
	}
	if(outcmd(0xA8, -1) < 0){	/* auxiliary device enable */
		iunlock(&i8042lock);
		print("i8042: aux init failed: aux device enable\n");
		return;
	}
	auxputc = putc;
	iunlock(&i8042lock);

	if(polltimer != nil)
		return;

	intrenable(IrqAUX, i8042intr, 0, BUSUNKNOWN, "kbdaux");
}

static void
kbdpoll(void)
{
	if(nokbd || qlen(kbd.q) > 0)
		return;
	i8042intr(0, 0);
}

/*
 * Kernel-internal raw Scancode operations.  These keep the PS/2 Queue and
 * IRQ/polling implementation private while allowing a trusted kernel caller
 * to use keyboard input without going through Chan/Qid dispatch.
 */
int
kbdscancodeacquire(void)
{
	if(nokbd || kbd.q == nil)
		return -2;
	if(incref(&kbd.ref) == 1)
		return 0;
	decref(&kbd.ref);
	return -1;
}

void
kbdscancoderelease(void)
{
	decref(&kbd.ref);
}

long
kbdscancoderead(void *buf, long n)
{
	if(nokbd || kbd.q == nil || kbd.ref.ref <= 0)
		return -1;
	kbdpoll();
	return qread(kbd.q, buf, n);
}

void
kbdshutdown(void)
{
	if(nokbd)
		return;

	ilock(&i8042lock);

	/* disable kbd and aux xfers and interrupts */
	ccc &= ~(Ckbdint|Cauxint);
	ccc |= (Cauxdis|Ckbddis);
	writeccc(ccc);

	iunlock(&i8042lock);
	nokbd = 1;
}

void
kbdinit(void)
{
	if(kbdinitialized)
		return;
	kbdinitialized = 1;

	if(getconf("*nokbd"))
		return;

	ilock(&i8042lock);

	/* wait for a quiescent controller */
	if(flushdata() < 0){
		iunlock(&i8042lock);
		print("i8042: kbd init failed: flushdata timeout\n");
		return;
	}

	ccc = readccc();
	if(ccc < 0){
		print("i8042: could not read ccc\n");
		ccc = 0;
	}

	/* enable kbd xfers and interrupts */
	ccc &= ~Ckbddis;
	ccc |= Csf | Ckbdint | Cscs1;

	/* disable ps2 mouse */
	ccc &= ~Cauxint;
	ccc |= Cauxdis;

	if(writeccc(ccc) < 0){
		iunlock(&i8042lock);
		print("i8042: kbd init failed: writing ccc\n");
		return;
	}
	iunlock(&i8042lock);

	kbd.q = qopen(1024, Qcoalesce, 0, 0);
	if(kbd.q == nil)
		panic("kbdinit");
	qnoblock(kbd.q, 1);

	ilock(&i8042lock);
	nokbd = 0;
	ioalloc(Cmd, 1, 0, "i8042.cs");
	ioalloc(Data, 1, 0, "i8042.data");
	iunlock(&i8042lock);

	if(getconf("*kbdpoll") != nil){
		polltimer = addclock0link(kbdpoll, 50);
		return;
	}
	intrenable(IrqKBD, i8042intr, 0, BUSUNKNOWN, "kbd");
}

