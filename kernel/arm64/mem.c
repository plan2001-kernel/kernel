#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define INITMAP	(ROUND((uintptr)end + BY2PG, PGLSZ(1))-KZERO)

/*
 * Create initial identity map in top-level page table
 * (L1BOT) for TTBR0. This page table is only used until
 * mmu1init() loads m->mmutop.
 */
void
mmuidmap(uintptr *l1bot)
{
	uintptr pa, pe, attr;

	/* VDRAM */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTESH(SHARE_INNER);
	pe = -KZERO;
	for(pa = VDRAM - KZERO; pa < pe; pa += PGLSZ(PTLEVELS-1))
		l1bot[PTLX(pa, PTLEVELS-1)] = pa | PTEVALID | PTEBLOCK | attr;
}

/*
 * Create initial shared kernel page table (L1) for TTBR1.
 * This page table coveres the INITMAP and VIRTIO,
 * and later we fill the ram mappings in meminit().
 */
void
mmu0init(uintptr *l1)
{
	uintptr va, pa, pe, attr;

	/* DRAM - INITMAP */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTESH(SHARE_INNER);
	pe = INITMAP;
	for(pa = VDRAM - KZERO, va = VDRAM; pa < pe; pa += PGLSZ(1), va += PGLSZ(1))
		l1[PTL1X(va, 1)] = pa | PTEVALID | PTEBLOCK | attr;

	/* VIRTIO */
	attr = PTEWRITE | PTEAF | PTEKERNEL | PTEUXN | PTEPXN | PTESH(SHARE_OUTER) | PTEDEVICE;
	pe = PHYSIOEND;
	for(pa = PHYSIO, va = VIRTIO; pa < pe; pa += PGLSZ(1), va += PGLSZ(1)){
		if(((pa|va) & PGLSZ(1)-1) != 0){
			l1[PTL1X(va, 1)] = (uintptr)l1 | PTEVALID | PTETABLE;
			for(; pa < pe && ((va|pa) & PGLSZ(1)-1) != 0; pa += PGLSZ(0), va += PGLSZ(0)){
				assert(l1[PTLX(va, 0)] == 0);
				l1[PTLX(va, 0)] = pa | PTEVALID | PTEPAGE | attr;
			}
			break;
		}
		l1[PTL1X(va, 1)] = pa | PTEVALID | PTEBLOCK | attr;
	}

	if(PTLEVELS > 2)
	for(va = KSEG0; va != 0; va += PGLSZ(2))
		l1[PTL1X(va, 2)] = (uintptr)&l1[L1TABLEX(va, 1)] | PTEVALID | PTETABLE;

	if(PTLEVELS > 3)
	for(va = KSEG0; va != 0; va += PGLSZ(3))
		l1[PTL1X(va, 3)] = (uintptr)&l1[L1TABLEX(va, 2)] | PTEVALID | PTETABLE;
}

/*
 * One bank of RAM for the kernel: [base, limit), page aligned, not below
 * lo; the BootInfo blob (port/bootinfo.c) is ours for good and cut out.
 */
static void
membank(uvlong base, uvlong limit, uvlong lo, uvlong hi)
{
	uvlong bb, be;
	Confmem *cm;
	int i;

	if(base < lo)
		base = lo;
	if(limit > hi)
		limit = hi;
	base = PGROUND(base);
	limit &= -BY2PG;
	if(base >= limit)
		return;
	bb = bootinfopa & -BY2PG;
	be = PGROUND(bootinfopa + bootinfo->totalsize);
	if(base < be && bb < limit){
		membank(base, bb, lo, hi);
		membank(be, limit, lo, hi);
		return;
	}
	for(i = 0; i < nelem(conf.mem); i++){
		cm = &conf.mem[i];
		if(cm->limit == 0){
			cm->base = base;
			cm->limit = limit;
			return;
		}
	}
	print("meminit: no bank left for %#llux-%#llux\n", base, limit);
}

/*
 * RAM is what the firmware's memory map (the BootInfo's) says is, less
 * the kernel and everything below it (its page tables and Machs, the
 * firmware's device tree at the start of RAM), what the kernel cannot map
 * (KLIMIT) and the BootInfo blob; adjacent RAM entries of any UEFI type
 * are one bank.  Not *maxmem: under UEFI, RAM has the firmware's runtime
 * regions in it.
 */
void
meminit(void)
{
	uvlong base, limit, lo;
	BootMem *bm;
	Confmem *cm;
	int i;

	lo = PGROUND((uintptr)end - KZERO);
	base = limit = 0;
	for(i = 0; i < bootinfo->mmapcount; i++){
		bm = bootmem(i);
		if(bootmemclass(bm->type) != BootClassRAM)
			continue;
		if(limit != 0 && bm->base == limit){
			limit += bm->len;
			continue;
		}
		if(limit != 0)
			membank(base, limit, lo, KLIMIT);
		base = bm->base;
		limit = bm->base + bm->len;
	}
	if(limit != 0)
		membank(base, limit, lo, KLIMIT);

	/* conf.mem[0] first: rampage() takes kmapram()'s page tables from it */
	for(i = 0; i < nelem(conf.mem); i++){
		cm = &conf.mem[i];
		if(cm->limit == 0)
			break;
		kmapram(cm->base, cm->limit);
		cm->npage = (cm->limit - cm->base)/BY2PG;
	}
}
