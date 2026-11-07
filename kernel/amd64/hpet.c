#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

/*
 * HPET timer
 *
 * The HPET timer is memory mapped which allows
 * faster access compared to the classic i8253.
 * This timer is not used to generate interrupts
 * as we use the LAPIC timer for that.
 * Its purpose is to measure the LAPIC timer
 * and TSC frequencies.
 */

enum {
	Cap	= 0x00/4,
	Period	= 0x04/4,
	Config	= 0x10/4,
	Isr	= 0x20/4,
	Ctrlo	= 0xF0/4,
	Ctrhi	= 0xF4/4,
};

static struct {
	Lock lk;
	u32int	*mmio;
	uvlong	last;
	uvlong	freq;
} hpet;

int
hpetprobe(uvlong pa)
{
	u32int cap, period;
	int mhz;

	if((hpet.mmio = vmap(pa, 1024)) == nil)
		return -1;
	cap = hpet.mmio[Cap];
	period = hpet.mmio[Period];
	if(period == 0 || period > 0x05F4E100)
		return -1;
	hpet.freq = 1000000000000000ULL / period;
	mhz = (hpet.freq + 500000) / 1000000;

	print("HPET: %llux %.8ux %d MHz \n", pa, cap, mhz);

	return 0;
}

/*
 * the TSC against the HPET over 50 ms, each HPET read between two TSC
 * reads and taken at their middle: a read is microseconds in a VM (an
 * exit), and over the 10 ms of delayloop's measure below that made the
 * clock (todinit's fastticks) up to 0.13% fast or slow, a boot's own
 */
static uvlong
hpettscfreq(void)
{
	u32int x, y;
	uvlong a0, a1, b0, b1;

	cycles(&a0);
	x = hpet.mmio[Ctrlo];
	cycles(&a1);
	do {
		cycles(&b0);
		y = hpet.mmio[Ctrlo] - x;
		cycles(&b1);
	} while(y < hpet.freq/20);
	return ((b0+b1)/2 - (a0+a1)/2) * hpet.freq / y;
}

static uvlong
hpetcpufreq(void)
{
	u32int x, y;
	uvlong a, b;
	int loops;

	ilock(&hpet.lk);
	for(loops = 1000;;loops += 1000){
		cycles(&a);
		x = hpet.mmio[Ctrlo];
		delayloop(loops);
		cycles(&b);
		y = hpet.mmio[Ctrlo] - x;
		if(y >= hpet.freq/HZ || loops >= 1000000)
			break;
	}
	if(m->havetsc && b > a)
		m->pmach.cyclefreq = hpettscfreq();
	iunlock(&hpet.lk);

	if(m->havetsc && b > a){
		b -= a;
		m->delaylcycles = (b + loops-1) / loops;
		return m->pmach.cyclefreq;
	}
	return (vlong)loops*m->delaylcycles * hpet.freq / y;
}

void
hpetinit(void)
{
	uvlong cpufreq;

	if(m->machno != 0){
		m->cpuhz = MACHP(0)->cpuhz;
		m->cpumhz = MACHP(0)->cpumhz;
		m->pmach.cyclefreq = MACHP(0)->pmach.cyclefreq;
		m->loopconst = MACHP(0)->loopconst;
		return;
	}

	/* start counting */
	hpet.mmio[Config] |= 1;

	/* measure loopconst for delay() and tsc frequencies */
	cpufreq = hpetcpufreq();

	m->loopconst = (cpufreq/1000)/m->delaylcycles;	/* delayloop()'s for 1 ms */
	m->cpuhz = cpufreq;

	/* round to the nearest megahz */
	m->cpumhz = (cpufreq+500000)/1000000L;
	if(m->cpumhz == 0)
		m->cpumhz = 1;
}

uvlong
hpetread(uvlong *hz)
{
	uvlong ticks;

	if(hz != nil)
		*hz = hpet.freq;

	ilock(&hpet.lk);
	ticks = hpet.last;
	ticks += hpet.mmio[Ctrlo] - (u32int)ticks;
	hpet.last = ticks;
	iunlock(&hpet.lk);

	return ticks;
}
