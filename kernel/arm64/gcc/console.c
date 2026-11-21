/*
 * Plan2001-O experimental ARM64 PL011 console.  See console.h.
 */
#include "u.h"
#include "bootmem.h"
#include "console.h"

#define UART0		(KZERO + 0x09000000ULL)

#define UART_DR		0x000
#define UART_FR		0x018
#define UART_IBRD	0x024
#define UART_FBRD	0x028
#define UART_LCRH	0x02c
#define UART_CR		0x030
#define UART_ICR	0x044

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

void
uartputc(int c)
{
	if(c == '\n')
		uartraw('\r');
	uartraw(c);
}

void
uartputs(const char *s)
{
	while(*s != 0)
		uartputc(*s++);
}

void
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

void
uartdec(u64int v)
{
	char buf[24], *p;

	p = putu64(buf, v);
	*p = 0;
	uartputs(buf);
}

void
uartinit(void)
{
	write32(UART0 + UART_ICR, 0x7ff);
	/* QEMU virt PL011 reference clock is 24 MHz; 115200 8N1 */
	write32(UART0 + UART_IBRD, 13);
	write32(UART0 + UART_FBRD, 1);
	write32(UART0 + UART_LCRH, (3 << 5) | (1 << 4));
	write32(UART0 + UART_CR, (1 << 0) | (1 << 8) | (1 << 9));
}
