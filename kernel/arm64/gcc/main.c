/*
 * Plan2001-O experimental ARM64 boot kernel: the smallest complete path
 * from the UEFI loader's BootInfo handoff to the architecture-independent
 * Plan2001-O early boot module (kernel/core/oplanboot.c).
 *
 * It owns a PL011 console, a 2 GiB boot map (entry.S), a GIC (v2 or v3 as
 * QEMU virt exposes) and the EL1 physical timer.  It does not yet contain
 * the AMD64 kernel's devices, scheduler, GE/O or userspace; the ARM64
 * marker is therefore printed only after the shared module has validated
 * the same BootInfo ABI the AMD64 kernel uses.
 */
#include "u.h"
#include <bootinfo.h>
#include <oplanboot.h>
#include "bootmem.h"

#define UART0		(KZERO + 0x09000000ULL)
#define GICD		(KZERO + 0x08000000ULL)
#define GICC		(KZERO + 0x08010000ULL)
#define GICR		(KZERO + 0x080a0000ULL)

#define UART_DR		0x000
#define UART_FR		0x018
#define UART_IBRD	0x024
#define UART_FBRD	0x028
#define UART_LCRH	0x02c
#define UART_CR		0x030
#define UART_ICR	0x044

#define GICC_CTLR	0x000
#define GICC_PMR	0x004
#define GICC_IAR	0x00c
#define GICC_EOIR	0x010

#define GICD_CTLR	0x000
#define GICD_ISENABLER	0x100
#define GICD_IPRIORITYR	0x400
#define GICD_PIDR2	0xfe8

#define GICR_WAKER	0x014
#define GICR_SGI	0x10000
#define GICR_IGROUPR0	0x080
#define GICR_ISENABLER0	0x100
#define GICR_IPRIORITYR0 0x400
#define GICR_TYPER	0x008

static int gicver;
static volatile int timerprints;

extern void arm64_halt(void);
extern char _efi64[], end[];

static u32int
read32(u64int a)
{
	return *(volatile u32int*)(uintptr)a;
}

static void
write32(u64int a, u32int v)
{
	*(volatile u32int*)(uintptr)a = v;
}

static void
uartraw(int c)
{
	while(read32(UART0 + UART_FR) & (1 << 5))
		;
	write32(UART0 + UART_DR, (u32int)c);
}

static void
uartputc(int c)
{
	if(c == '\n')
		uartraw('\r');
	uartraw(c);
}

static void
uartputs(const char *s)
{
	while(*s != 0)
		uartputc(*s++);
}

static void
uarthex(u64int v)
{
	static char h[] = "0123456789abcdef";
	int i;

	uartputs("0x");
	for(i = 60; i >= 0; i -= 4)
		uartputc(h[(v >> i) & 0xf]);
}

static char*
putu64(char *p, u64int v)
{
	char t[24];
	int n;

	n = 0;
	do{
		t[n++] = '0' + v%10;
		v /= 10;
	}while(v != 0);
	while(n > 0)
		*p++ = t[--n];
	return p;
}

static void
uartdec(u64int v)
{
	char buf[24], *p;

	p = putu64(buf, v);
	*p = 0;
	uartputs(buf);
}

static void
announceout(void *arg, const char *s)
{
	(void)arg;
	uartputs(s);
}

static void
uartinit(void)
{
	write32(UART0 + UART_ICR, 0x7ff);
	/* QEMU virt PL011 reference clock is 24 MHz; 115200 8N1 */
	write32(UART0 + UART_IBRD, 13);
	write32(UART0 + UART_FBRD, 1);
	write32(UART0 + UART_LCRH, (3 << 5) | (1 << 4));
	write32(UART0 + UART_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

static void
gicv2init(void)
{
	write32(GICC + GICC_CTLR, 0);
	write32(GICD + GICD_CTLR, 0);
	write32(GICD + GICD_ISENABLER, 1u << 30);
	((volatile uchar*)(uintptr)(GICD + GICD_IPRIORITYR))[30] = 0xa0;
	write32(GICC + GICC_PMR, 0xf8);
	write32(GICC + GICC_CTLR, 1);
	write32(GICD + GICD_CTLR, 1);
}

static void
gicv3init(void)
{
	u32int v;

	write32(GICD + GICD_CTLR, 0);
	write32(GICD + GICD_CTLR, (1u << 4) | (1u << 1));	/* ARE_NS, Grp1NS */
	while(read32(GICD + GICD_CTLR) & (1u << 31))
		;

	v = read32(GICR + GICR_WAKER);
	write32(GICR + GICR_WAKER, v & ~(1u << 1));
	while(read32(GICR + GICR_WAKER) & (1u << 2))
		;

	((volatile uchar*)(uintptr)(GICR + GICR_SGI + GICR_IPRIORITYR0))[30] = 0xa0;
	v = read32(GICR + GICR_SGI + GICR_IGROUPR0);
	write32(GICR + GICR_SGI + GICR_IGROUPR0, v | (1u << 30));	/* PPI 30 is Group 1 */
	write32(GICR + GICR_SGI + GICR_ISENABLER0, 1u << 30);

	__asm__ volatile("msr s3_0_c12_c12_5, %0" : : "r"(7ULL) : "memory");
	__asm__ volatile("msr s3_0_c4_c6_0, %0" : : "r"(0xf8ULL) : "memory");
	__asm__ volatile("msr s3_0_c12_c12_4, %0" : : "r"(0ULL) : "memory");
	__asm__ volatile("msr s3_0_c12_c12_7, %0" : : "r"(1ULL) : "memory");
	__asm__ volatile("isb" : : : "memory");
}

static u32int
gicack(void)
{
	u64int v;

	if(gicver == 3)
		__asm__ volatile("mrs %0, s3_0_c12_c12_0" : "=r"(v));
	else
		v = read32(GICC + GICC_IAR);
	return (u32int)v;
}

static void
giceoi(u32int iar)
{
	if(gicver == 3)
		__asm__ volatile("msr s3_0_c12_c12_1, %0" : : "r"((u64int)iar) : "memory");
	else
		write32(GICC + GICC_EOIR, iar);
}

static int
gicversion(void)
{
	u32int pidr2;

	pidr2 = read32(GICD + GICD_PIDR2);
	if(((pidr2 >> 4) & 0xf) >= 3)
		return 3;
	/*
	 * QEMU's GICv3 model leaves the distributor ID registers at zero.
	 * The redistributor's GICR_TYPER still identifies it; the QEMU virt
	 * device window is mapped and an absent redistributor reads as zero.
	 */
	if(pidr2 == 0 && read32(GICR + GICR_TYPER) != 0)
		return 3;
	return 2;
}

static void
timerstart(u64int hz)
{
	if(hz == 0)
		hz = 62500000;
	__asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(0ULL) : "memory");
	__asm__ volatile("msr cntp_tval_el0, %0" : : "r"(hz/10) : "memory");
	__asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(1ULL) : "memory");
	__asm__ volatile("isb" : : : "memory");
}

void
arm64_irq_handler(void)
{
	u32int iar;
	u64int hz;

	iar = gicack();
	if((iar & 0xffffff) == 30){
		timerprints++;
		hz = 62500000;
		__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
		if(timerprints < 3){
			__asm__ volatile("msr cntp_tval_el0, %0" : : "r"(hz/10) : "memory");
		}else if(timerprints == 3){
			__asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(0ULL) : "memory");
			uartputs("PLAN2001-O: ARM64 timer: 3 ticks, interrupts working\n");
		}
	}
	giceoi(iar);
}

void
arm64_fault(u64int esr, u64int elr, u64int far, u64int spsr)
{
	uartputs("\nPLAN2001-O: ARM64 FAULT\n");
	uartputs("  esr="), uarthex(esr), uartputc('\n');
	uartputs("  elr="), uarthex(elr), uartputc('\n');
	uartputs("  far="), uarthex(far), uartputc('\n');
	uartputs("  spsr="), uarthex(spsr), uartputc('\n');
	arm64_halt();
}

void
arm64_main(u64int bootpa)
{
	BootInfo *b;
	u64int hz, ram;
	char err[OplanBootErrLen];

	uartinit();
	uartputs("\nPLAN2001-O: ARM64 kernel: entry\n");
	if(bootpa == 0 || (bootpa & (OplanBootPageSize-1)) != 0
	|| bootpa < RAMBASE || bootpa >= RAMEND){
		uartputs("PLAN2001-O: ARM64 FAULT: bad BootInfo address\n");
		arm64_halt();
	}
	b = (BootInfo*)(uintptr)(bootpa + KZERO);
	if(!oplanbootvalidate(b, (u64int)(uintptr)b, BootArchArm64,
	    err, sizeof err)){
		uartputs("PLAN2001-O: ARM64 FAULT: BootInfo: ");
		uartputs(err);
		uartputc('\n');
		arm64_halt();
	}
	if(b->totalsize > RAMEND - bootpa){
		uartputs("PLAN2001-O: ARM64 FAULT: BootInfo outside boot map\n");
		arm64_halt();
	}

	uartputs("PLAN2001-O: ARM64: BootInfo at ");
	uarthex(bootpa);
	uartputc('\n');
	ram = oplanbootannounce(announceout, nil, b, (uchar*)b);
	(void)ram;

	/*
	 * Turn the shared memory map's RAM into pages, reserving the kernel
	 * image and the BootInfo blob itself, then prove allocation works.
	 */
	if(arm64meminit(b, bootpa, (u64int)(uintptr)_efi64 - KZERO,
	    (u64int)(uintptr)end - KZERO) == 0){
		uartputs("PLAN2001-O: ARM64 FAULT: no free memory in BootInfo map: ");
		uartdec(arm64memerror());
		uartputc('\n');
		arm64_halt();
	}
	uartputs("PLAN2001-O: ARM64 MEM: ");
	uartdec(arm64memfree()/(1024*1024));
	uartputs(" MiB free\n");
	if(!arm64memtest(16)){
		uartputs("PLAN2001-O: ARM64 FAULT: page allocator self-test failed\n");
		arm64_halt();
	}
	uartputs("PLAN2001-O: ARM64 MEM: allocator self-test passed\n");

	gicver = gicversion();
	if(gicver == 3)
		gicv3init();
	else
		gicv2init();
	uartputs("PLAN2001-O: ARM64: GICv");
	uartdec(gicver);
	uartputc('\n');

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
	uartputs("PLAN2001-O: ARM64: CNTFRQ ");
	uartdec(hz);
	uartputs(" Hz\n");
	timerstart(hz);
	__asm__ volatile("msr daifclr, #2" : : : "memory");

	uartputs("PLAN2001-O: ARM64 BOOT: shared kernel initialization reached\n");
	for(;;)
		__asm__ volatile("wfi");
}
