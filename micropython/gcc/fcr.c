/*
 * AMD64 MXCSR control access for the freestanding GCC user build.
 * Mirrors sys/src/libc/amd64/getfcr.s (Plan 9 FP exception masks).
 */
#include <u.h>

int
getfcr(void)
{
	uint csr;

	__asm__ volatile("stmxcsr %0" : "=m"(csr));
	return (int)((csr & 0xFFC0u) ^ (0x3Fu << 7));
}

void
setfcr(int fcr)
{
	uint csr, v;

	__asm__ volatile("stmxcsr %0" : "=m"(csr));
	v = ((uint)fcr ^ (0x3Fu << 7)) & 0xFFC0u;
	csr = (csr & 0x3Fu) | v;
	__asm__ volatile("ldmxcsr %0" :: "m"(csr));
}

int
getfsr(void)
{
	uint csr;

	__asm__ volatile("stmxcsr %0" : "=m"(csr));
	return (int)(csr & 0x3Fu);
}

void
setfsr(int fsr)
{
	uint csr;

	__asm__ volatile("stmxcsr %0" : "=m"(csr));
	csr = (csr & ~0x3Fu) | ((uint)fsr & 0x3Fu);
	__asm__ volatile("ldmxcsr %0" :: "m"(csr));
}
