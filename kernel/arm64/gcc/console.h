/*
 * Plan2001-O experimental ARM64 PL011 console.
 *
 * The boot kernel has exactly one output path: the QEMU virt PL011 at
 * physical 0x09000000, reachable through the EL1-only high map.  It is
 * polled, has no buffer and no locking because the single-CPU boot kernel
 * only prints from EL1 with interrupts masked around each call site that
 * matters.  EL0 has no access to this MMIO page.
 */
#ifndef PLAN2001_ARM64_CONSOLE_H
#define PLAN2001_ARM64_CONSOLE_H

/* includers provide u.h first, as every kernel source does */

void	uartinit(void);
void	uartputc(int);
void	uartputs(const char*);
void	uarthex(u64int);	/* "0x" + 16 lowercase hex digits */
void	uartdec(u64int);

#endif
