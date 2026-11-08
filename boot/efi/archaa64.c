#include <u.h>
#include "fns.h"
#include "efi.h"
#include "mem.h"

/*
 * The ARM64 side of the loader, for bootaa64.efi (see archx64.c for the
 * AMD64 one and fns.h for the calls).  The experimental Plan2001-O ARM64
 * boot kernel is a freestanding ELF linked at KZERO 0xffffffff80000000
 * plus its physical address, loaded at 0x40100000 in QEMU virt's RAM.
 * Its entry builds a 2 GiB identity/high map, so the BootInfo blob must
 * also stay inside that window.  Plan2001 Boot ABI v1, ARM64 entry:
 * X0 = BootInfo physical address, X1 = 0.
 */

void jump(void *pc, void *bootinfo);

enum {
	RamBase	= 0x40000000,		/* QEMU virt */
	KernelLow = RamBase + 16*1024*1024,	/* blob stays above image + tables */
	KernelMap = 0x80000000ULL,	/* the kernel's boot map covers 0..2 GiB */
};

/* the ISA into the BootInfo; there is no TSC (its tscfreq stays 0) */
void
archconf(BootInfo *bi)
{
	bi->arch = BootArchArm64;
}

/* the kernel's physical entry from its ELF entry: linked at KZERO + pa */
uvlong
archentry(uvlong entry)
{
	return entry - 0xffffffff80000000ULL;
}

/*
 * where 7l starts the kernel's data after its text: the next 64 KB (its
 * default -R; the kernel's mkfile links without one), not the next page.
 * The ELF path does not use this.
 */
ulong
archdataround(void)
{
	return 0x10000;
}

/*
 * May the BootInfo blob lie at [pa, pa+len)?  Not in the first 16 MB of
 * RAM (the kernel's image and boot page tables), and within the physical
 * range the kernel's boot map covers.
 */
int
archblobok(uvlong pa, uvlong len)
{
	return pa >= KernelLow && pa + len <= KernelMap;
}

/* the kernel's entry must be in RAM the boot map covers */
int
archentryok(uvlong entry)
{
	uvlong pa;

	if(entry < 0xffffffff80000000ULL)
		return 0;
	pa = entry - 0xffffffff80000000ULL;
	return pa >= RamBase && pa < KernelMap;
}

/*
 * The loader wrote the image through the firmware's caches.  Clean each
 * loaded range to the point of coherency before the kernel entry turns the
 * MMU and caches off; archcachesync() then makes the instructions visible.
 * aa64gcc.S implements both.
 */
void
archcacheclean(uvlong pa, uvlong len)
{
	extern void cacheclean(uvlong, uvlong);

	cacheclean(pa, len);
}

void
archcachesync(void)
{
	extern void cachesync(void);

	cachesync();
}

/* nil if the kernel can be entered from here: it drops to EL1 itself */
char*
archcheck(void)
{
	return nil;
}

/*
 * Enter the kernel: X0 = BootInfo physical address (aa64gcc.S).  jump()
 * turns the MMU and caches off and drops to EL1; the kernel entry then
 * invalidates its .bss before enabling the data cache.
 */
void
archjump(void *entry, void *bootinfo)
{
	jump(entry, bootinfo);
}
