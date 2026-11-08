/*
 * Plan2001-O experimental ARM64 boot memory.
 *
 * The boot kernel maps physical 0..2 GiB at KZERO and takes its usable RAM
 * from the shared BootInfo v1 memory map.  This is the first kernel service
 * on the ARM64 path: a page-granular physical allocator that reserves the
 * kernel image and the BootInfo blob before handing out any page.
 */
#ifndef PLAN2001_ARM64_BOOTMEM_H
#define PLAN2001_ARM64_BOOTMEM_H

/* includers provide u.h first, as every kernel source does */
#include <bootinfo.h>

#define KZERO		0xffffffff80000000ULL
#define RAMBASE		0x40000000ULL
#define RAMEND		0x80000000ULL	/* the entry map covers physical 0..2 GiB */

enum {
	PageSize = 4096,
};

/*
 * Classify b's memory map, drop everything outside [RAMBASE, RAMEND), and
 * reserve the kernel image [kstart, kend) and the blob [blobpa,
 * blobpa+totalsize).  Returns the bytes of free memory, 0 on failure.
 */
u64int	arm64meminit(BootInfo *b, u64int blobpa, u64int kstart, u64int kend);

/* one zeroed page, physical address; 0 if exhausted */
u64int	arm64pagealloc(void);

/* return a page from arm64pagealloc; 1 if it was known to be allocated */
int	arm64pagefree(u64int pa);

/* bytes currently free */
u64int	arm64memfree(void);

/* why arm64meminit returned 0; 0 if it did not fail */
int	arm64memerror(void);

/* allocate n pages, check them, free them, allocate them again; 1 on pass */
int	arm64memtest(int n);

#endif
