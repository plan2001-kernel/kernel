#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"rtcapi.h"

/*
 *  real time clock and non-volatile ram
 */

enum {
	Paddr=		0x70,	/* address port */
	Pdata=		0x71,	/* data port */

	Seconds=	0x00,
	Minutes=	0x02,
	Hours=		0x04, 
	Mday=		0x07,
	Month=		0x08,
	Year=		0x09,
	Status=		0x0A,

	Nbcd=		6,
};

typedef struct Rtc	Rtc;
struct Rtc
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
};


static ulong rtc2sec(Rtc*);
static void sec2rtc(ulong, Rtc*);

#define GETBCD(o) ((bcdclock[o]&0xf) + 10*(bcdclock[o]>>4))

static int
bcdvalid(uchar v)
{
	return (v & 0xf) <= 9 && ((v >> 4) & 0xf) <= 9;
}

/*
 *  Read the six CMOS clock registers using the inherited readiness checks.
 *  Return 0 only if the read stabilized and every register held valid BCD.
 *  rtc is filled even on failure so callers can inspect the best-effort
 *  values that were read.
 */
static int
rtcget(Rtc *rtc)
{
	uchar bcdclock[Nbcd];
	int i, ok;

	for(i = 0; i < Nbcd; i++)
		bcdclock[i] = 0;

	ok = 0;
	/* don't do the read until the clock is no longer busy */
	for(i = 0; i < 10000; i++){
		outb(Paddr, Status);
		if(inb(Pdata) & 0x80)
			continue;

		/* read clock values */
		outb(Paddr, Seconds);	bcdclock[0] = inb(Pdata);
		outb(Paddr, Minutes);	bcdclock[1] = inb(Pdata);
		outb(Paddr, Hours);	bcdclock[2] = inb(Pdata);
		outb(Paddr, Mday);	bcdclock[3] = inb(Pdata);
		outb(Paddr, Month);	bcdclock[4] = inb(Pdata);
		outb(Paddr, Year);	bcdclock[5] = inb(Pdata);

		outb(Paddr, Status);
		if((inb(Pdata) & 0x80) == 0){
			ok = 1;
			break;
		}
	}
	for(i = 0; i < Nbcd; i++)
		if(!bcdvalid(bcdclock[i]))
			ok = 0;

	/*
	 *  convert from BCD
	 */
	rtc->sec = GETBCD(0);
	rtc->min = GETBCD(1);
	rtc->hour = GETBCD(2);
	rtc->mday = GETBCD(3);
	rtc->mon = GETBCD(4);
	rtc->year = GETBCD(5);

	/*
	 *  the world starts jan 1 1970
	 */
	if(rtc->year < 70)
		rtc->year += 2000;
	else
		rtc->year += 1900;
	return ok ? 0 : -1;
}

static Lock nvrtlock;

#define PUTBCD(n,o) bcdclock[o] = (n % 10) | (((n / 10) % 10)<<4)

#define SEC2MIN 60L
#define SEC2HOUR (60L*SEC2MIN)
#define SEC2DAY (24L*SEC2HOUR)

/*
 *  days per month plus days/year
 */
static	int	dmsize[] =
{
	365, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static	int	ldmsize[] =
{
	366, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 *  return the days/month for the given year
 */
static int*
yrsize(int y)
{
	if((y%4) == 0 && ((y%100) != 0 || (y%400) == 0))
		return ldmsize;
	else
		return dmsize;
}

/*
 *  compute seconds since Jan 1 1970
 */
static ulong
rtc2sec(Rtc *rtc)
{
	ulong secs;
	int i;
	int *d2m;

	secs = 0;

	/*
	 *  seconds per year
	 */
	for(i = 1970; i < rtc->year; i++){
		d2m = yrsize(i);
		secs += d2m[0] * SEC2DAY;
	}

	/*
	 *  seconds per month
	 */
	d2m = yrsize(rtc->year);
	for(i = 1; i < rtc->mon; i++)
		secs += d2m[i] * SEC2DAY;

	secs += (rtc->mday-1) * SEC2DAY;
	secs += rtc->hour * SEC2HOUR;
	secs += rtc->min * SEC2MIN;
	secs += rtc->sec;

	return secs;
}

/*
 *  compute rtc from seconds since Jan 1 1970
 */
static void
sec2rtc(ulong secs, Rtc *rtc)
{
	int d;
	long hms, day;
	int *d2m;

	/*
	 * break initial number into days
	 */
	hms = secs % SEC2DAY;
	day = secs / SEC2DAY;
	if(hms < 0) {
		hms += SEC2DAY;
		day -= 1;
	}

	/*
	 * generate hours:minutes:seconds
	 */
	rtc->sec = hms % 60;
	d = hms / 60;
	rtc->min = d % 60;
	d /= 60;
	rtc->hour = d;

	/*
	 * year number
	 */
	if(day >= 0)
		for(d = 1970; day >= *yrsize(d); d++)
			day -= *yrsize(d);
	else
		for (d = 1970; day < 0; d--)
			day += *yrsize(d-1);
	rtc->year = d;

	/*
	 * generate month
	 */
	d2m = yrsize(rtc->year);
	for(d = 1; day >= d2m[d]; d++)
		day -= d2m[d];
	rtc->mday = day + 1;
	rtc->mon = d;

	return;
}

/*
 *  The CMOS year is a two-digit BCD field.  This repo's existing window
 *  maps 00..69 to 2000..2069 and 70..99 to 1970..1999, so the representable
 *  range is 1970..2069 (not a universal 64-bit epoch or century field).
 *  rtc2sec/sec2rtc remain the epoch conversion.
 */
static int
rtcvalid(Rtc *rtc)
{
	int *d2m;

	if(rtc->year < 1970 || rtc->year > 2069)
		return 0;
	if(rtc->mon < 1 || rtc->mon > 12)
		return 0;
	d2m = yrsize(rtc->year);
	if(rtc->mday < 1 || rtc->mday > d2m[rtc->mon])
		return 0;
	if(rtc->hour < 0 || rtc->hour > 23)
		return 0;
	if(rtc->min < 0 || rtc->min > 59)
		return 0;
	if(rtc->sec < 0 || rtc->sec > 59)
		return 0;
	return 1;
}

static int
rtcsame(Rtc *a, Rtc *b)
{
	return a->sec == b->sec && a->min == b->min && a->hour == b->hour
		&& a->mday == b->mday && a->mon == b->mon && a->year == b->year;
}

/*
 *  Read until two consecutive stable reads agree.  Caller holds nvrtlock.
 */
static int
rtcgetstable(Rtc *rtc)
{
	Rtc cur, prev;
	int i, have;

	have = 0;
	for(i = 0; i < 100; i++){
		if(rtcget(&cur) < 0)
			continue;
		if(have && rtcsame(&prev, &cur)){
			*rtc = cur;
			return 0;
		}
		prev = cur;
		have = 1;
	}
	return -1;
}

static void
rtcseterr(char *msg)
{
	if(up != nil)
		kstrcpy(up->errstr, msg, ERRMAX);
}

int
rtcnativeget(Rtcdate *d)
{
	Rtc rtc;
	int r;

	if(d == nil){
		rtcseterr(Ebadarg);
		return -1;
	}

	ilock(&nvrtlock);
	r = rtcgetstable(&rtc);
	iunlock(&nvrtlock);
	if(r < 0){
		rtcseterr("rtc: clock read did not stabilize");
		return -1;
	}
	if(!rtcvalid(&rtc)){
		rtcseterr("rtc: invalid clock value");
		return -1;
	}

	d->year = rtc.year;
	d->month = rtc.mon;
	d->day = rtc.mday;
	d->hour = rtc.hour;
	d->minute = rtc.min;
	d->second = rtc.sec;
	return 0;
}

int
rtcnativeset(Rtcdate *d)
{
	Rtc rtc;
	uchar bcdclock[Nbcd];
	ulong secs;

	if(up == nil || !iseve()){
		rtcseterr(Eperm);
		return -1;
	}
	if(d == nil){
		rtcseterr(Ebadarg);
		return -1;
	}

	rtc.year = d->year;
	rtc.mon = d->month;
	rtc.mday = d->day;
	rtc.hour = d->hour;
	rtc.min = d->minute;
	rtc.sec = d->second;
	if(!rtcvalid(&rtc)){
		rtcseterr(Ebadarg);
		return -1;
	}

	/*
	 *  use the existing epoch conversion and BCD helpers so the round trip
	 *  normalizes through the same two-digit year window as reads
	 */
	secs = rtc2sec(&rtc);
	sec2rtc(secs, &rtc);
	PUTBCD(rtc.sec, 0);
	PUTBCD(rtc.min, 1);
	PUTBCD(rtc.hour, 2);
	PUTBCD(rtc.mday, 3);
	PUTBCD(rtc.mon, 4);
	PUTBCD(rtc.year, 5);

	ilock(&nvrtlock);
	outb(Paddr, Seconds);	outb(Pdata, bcdclock[0]);
	outb(Paddr, Minutes);	outb(Pdata, bcdclock[1]);
	outb(Paddr, Hours);	outb(Pdata, bcdclock[2]);
	outb(Paddr, Mday);	outb(Pdata, bcdclock[3]);
	outb(Paddr, Month);	outb(Pdata, bcdclock[4]);
	outb(Paddr, Year);	outb(Pdata, bcdclock[5]);
	iunlock(&nvrtlock);
	return 0;
}

uchar
nvramread(int addr)
{
	uchar data;

	ilock(&nvrtlock);
	addr &= 0xFF;
	outb(Paddr, addr);
	data = inb(Pdata);
	if(addr & 0x80){
		/* clear NMI disable bit */
		outb(Paddr, 0);
		inb(Pdata);
	}
	iunlock(&nvrtlock);

	return data;
}

void
nvramwrite(int addr, uchar data)
{
	ilock(&nvrtlock);
	addr &= 0xFF;
	outb(Paddr, addr);
	outb(Pdata, data);
	if(addr & 0x80){
		/* clear NMI disable bit */
		outb(Paddr, 0);
		inb(Pdata);
	}
	iunlock(&nvrtlock);
}
