#include <u.h>
#include "fns.h"
#include "efi.h"
#include "mem.h"

/*
 * The ARM64 side of the loader, for bootaa64.efi (see archx64.c for the
 * AMD64 one and fns.h for the calls).  The kernel is Plan2001's arm64
 * port of 9front's QEMU virt kernel: linked at KZERO 0xffffffff80000000
 * plus its physical address, text at 0x40100000, its boot page tables and
 * Mach below that, in the first megabyte of RAM (0x40000000).  Plan2001
 * Boot ABI v1, ARM64 entry: docs/boot-abi-arm64.md.
 */

void jump(void *pc, void *bootinfo);

enum {
	RamBase	= 0x40000000,		/* QEMU virt */
	KernelLow = RamBase + 16*1024*1024,	/* image, page tables, Mach below */
	KernelMap = 0x140000000ULL,	/* the kernel's KZERO window ends here (KLIMIT) */
};

/* the ISA into the BootInfo; there is no TSC (its tscfreq stays 0) */
void
archconf(BootInfo *bi)
{
	bi->arch = BootArchArm64;
}

/* the kernel's physical entry from its a.out entry: linked at KZERO + pa */
uvlong
archentry(uvlong entry)
{
	return entry - 0xffffffff80000000ULL;
}

/*
 * where 7l starts the kernel's data after its text: the next 64 KB (its
 * default -R; the kernel's mkfile links without one), not the next page
 */
ulong
archdataround(void)
{
	return 0x10000;
}

/*
 * May the BootInfo blob lie at [pa, pa+len)?  Not in the first 16 MB of RAM
 * (the kernel's image and boot page tables), and within the physical range
 * the kernel maps at KZERO.
 */
int
archblobok(uvlong pa, uvlong len)
{
	return pa >= KernelLow && pa + len <= KernelMap;
}

/* nil if the kernel can be entered from here: it drops to EL1 itself */
char*
archcheck(void)
{
	return nil;
}

/*
 * Enter the kernel: X0 = BootInfo (aa64.s).  jump() turns the MMU and the
 * caches off first; the kernel writes back and invalidates the caches
 * itself before it builds its own page tables (cachedwbinv in its _start).
 */
void
archjump(void *entry, void *bootinfo)
{
	jump(entry, bootinfo);
}
