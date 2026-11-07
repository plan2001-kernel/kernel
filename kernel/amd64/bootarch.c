#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * The AMD64 side of port/bootinfo.c: the BootInfo blob can be anywhere in
 * physical memory, and the page tables _efi64 built map only the kernel
 * itself.  bootearlymap() maps it at BOOTMAPVA (mem.h), a virtual window of
 * our own, so that no physical address is out of reach, from two
 * page-table pages of our own (a PD and a PT under KZERO's PDP):
 * rampage() cannot be used yet, it needs the memory map, which is in the
 * blob.  BOOTMAPSIZE, what one PT maps, is this kernel's own limit on the
 * blob, not part of the ABI.
 */
enum {
	BootMapPages	= 2,
};
static uchar bootmapmem[(BootMapPages+1)*BY2PG];
static int bootmapused;

static uintptr*
bootmapnext(uintptr *pte)
{
	uintptr *t;

	if((*pte & PTEVALID) == 0){
		if(bootmapused >= BootMapPages)
			return nil;
		t = (uintptr*)(((uintptr)bootmapmem + BY2PG-1) & ~(BY2PG-1)) + bootmapused++*(BY2PG/sizeof(uintptr));
		memset(t, 0, BY2PG);
		*pte = PADDR(t) | PTEWRITE | PTEVALID;
	}
	return KADDR(*pte & 0x000FFFFFFFFFF000ull);
}

/* map [pa, pa+size) at BOOTMAPVA and return that, or nil; pa is page aligned */
void*
bootearlymap(uvlong pa, uvlong size)
{
	uvlong o;
	uintptr va, *t;
	int l;

	if(size > BOOTMAPSIZE)
		return nil;
	for(o = 0; o < size; o += BY2PG){
		va = BOOTMAPVA + o;
		t = (uintptr*)CPU0PML4;
		for(l = 3; l > 0; l--)
			if((t = bootmapnext(&t[PTLX(va, l)])) == nil)
				return nil;
		t[PTLX(va, 0)] = (pa + o) | PTEWRITE | PTEVALID;
	}
	putcr3(getcr3());
	return (void*)BOOTMAPVA;
}

/*
 * The boot framebuffer (port/bootfb.c), mapped write-combining (PAT);
 * a display is there, so a panic hangs with its marks visible instead
 * of rebooting.
 */
void*
fbmap(uvlong pa, uvlong size)
{
	void *v;

	if((v = vmap(pa, size)) == nil)
		return nil;
	patwc(v, size);
	conf.monitor = 1;
	return v;
}
