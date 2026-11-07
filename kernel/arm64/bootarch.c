#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * The ARM64 side of port/bootinfo.c and port/bootfb.c.  Plan2001 Boot ABI
 * v1, ARM64 entry: _start (l.s) stores X0 here; this target remains
 * experimental (see tools/targets/arm64).
 */
uintptr bootinfopa;

/* nothing to halt with this early; port/bootinfo.c loops on it */
void
halt(void)
{
}

/*
 * Where the kernel maps RAM at pa (mmu.c's kmapaddr(), which is static
 * there): KZERO+pa below -KZERO, the KMAP window above it.
 */
static uintptr
bootkmapva(uvlong pa)
{
	if(pa < (uintptr)-KZERO)
		return pa + KZERO;
	return pa + KMAP - (VDRAM - KZERO);
}

/*
 * Map the BootInfo blob before the kernel's memory allocator exists, at
 * the very address kmapram() (mem.c meminit()) maps that RAM later, with
 * the same attributes, so that it stays where it is: the page tables are
 * two pages of our own, under the shared kernel L1 that mmu0init() set
 * up, and l1map() takes an identical entry as already there.  RAM the
 * initial map covers (below INITMAP) needs nothing.
 */
enum {
	BootMapPages	= 2,
	BootMapMax	= 2*MiB,		/* this kernel's own limit, not the ABI's */
};
static uchar bootmapmem[(BootMapPages+1)*BY2PG];
static int bootmapused;

void*
bootearlymap(uvlong pa, uvlong size)
{
	uintptr va, attr, *l1, *l0, *e;
	uvlong o;

	if(size > BootMapMax || pa < VDRAM - KZERO
	|| pa + size > (VDRAM - KZERO) + (KMAPEND - KMAP))
		return nil;
	attr = PTEWRITE | PTEPXN | PTEUXN | PTESH(SHARE_INNER) | PTEKERNEL | PTEAF;
	l1 = (uintptr*)L1;
	for(o = 0; o < size; o += BY2PG){
		va = bootkmapva(pa + o);
		e = &l1[PTL1X(va, 1)];
		if((*e & PTEVALID) != 0 && (*e & PTETABLE) != PTETABLE)
			continue;	/* a block of the initial map */
		if((*e & PTEVALID) == 0){
			if(bootmapused >= BootMapPages)
				return nil;
			l0 = (uintptr*)(((uintptr)bootmapmem + BY2PG-1) & -BY2PG) + bootmapused++*(BY2PG/sizeof(uintptr));
			memset(l0, 0, BY2PG);
			*e = PTEVALID | PTETABLE | PADDR(l0);
		}
		l0 = KADDR(*e & -PGLSZ(0));
		l0[PTLX(va, 0)] = PTEVALID | PTEPAGE | (pa + o) | attr;
	}
	flushtlb();
	return (void*)bootkmapva(pa);
}

/* the boot framebuffer (port/bootfb.c): device memory, as vmap() maps it */
void*
fbmap(uvlong pa, uvlong size)
{
	void *v;

	if((v = vmap(pa, size)) == nil)
		return nil;
	conf.monitor = 1;
	return v;
}
