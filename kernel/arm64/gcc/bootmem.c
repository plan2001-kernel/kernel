/*
 * Plan2001-O experimental ARM64 boot memory.
 *
 * A deliberately small physical page allocator over the BootInfo v1 memory
 * map.  It is not a VM system, a zone allocator or a replacement for the
 * AMD64 memory code; it exists so the ARM64 boot kernel can prove that the
 * shared map classification yields usable RAM before any other service is
 * brought up.
 *
 * Policy: every free range is page-aligned inside [RAMBASE, RAMEND), the
 * kernel image and the BootInfo blob are subtracted, allocation is a bump
 * pointer per range with a LIFO list of freed pages, and every page handed
 * out is zeroed.  Interrupts do not call this allocator, so it needs no lock
 * in this single-CPU boot kernel.
 */
#include "u.h"
#include <oplanboot.h>
#include "bootmem.h"

enum {
	MaxRanges = 64,
	MaxFreed = 32,
	NPages = (RAMEND - RAMBASE) / PageSize,
};

typedef struct Range Range;
struct Range {
	u64int	base;
	u64int	end;
};

static Range ranges[MaxRanges];
static int nranges;
static u64int freed[MaxFreed];
static int nfreed;
static u64int pageused[NPages/64];
static u64int freebytes;
static int memerr;

static int
pageisused(u64int pa)
{
	u64int i;

	i = (pa - RAMBASE) / PageSize;
	return (pageused[i/64] >> (i%64)) & 1;
}

static void
setpageused(u64int pa, int used)
{
	u64int i, *w;

	i = (pa - RAMBASE) / PageSize;
	w = &pageused[i/64];
	if(used)
		*w |= 1ULL << (i%64);
	else
		*w &= ~(1ULL << (i%64));
}

int
arm64memerror(void)
{
	return memerr;
}

static u64int
pageup(u64int v)
{
	return (v + PageSize-1) & ~(u64int)(PageSize-1);
}

static u64int
pagedown(u64int v)
{
	return v & ~(u64int)(PageSize-1);
}

static void
addrange(u64int base, u64int end)
{
	int i;

	if(base >= end)
		return;
	for(i = 0; i < nranges; i++){
		if(end == ranges[i].base){
			ranges[i].base = base;
			return;
		}
		if(base == ranges[i].end){
			ranges[i].end = end;
			return;
		}
	}
	if(nranges >= MaxRanges)
		return;
	ranges[nranges].base = base;
	ranges[nranges].end = end;
	nranges++;
}

/* remove [s, e) from every free range, splitting when it falls inside one */
static void
subtract(u64int s, u64int e)
{
	Range *r;
	u64int b;
	int i;

	if(s >= e)
		return;
	for(i = 0; i < nranges; i++){
		r = &ranges[i];
		if(e <= r->base || s >= r->end)
			continue;
		if(s <= r->base && e >= r->end){
			ranges[i] = ranges[--nranges];
			i--;
			continue;
		}
		if(s <= r->base){
			r->base = e;
			continue;
		}
		if(e >= r->end){
			r->end = s;
			continue;
		}
		b = r->end;
		r->end = s;
		if(nranges < MaxRanges){
			ranges[nranges].base = e;
			ranges[nranges].end = b;
			nranges++;
		}
	}
}

static void
zeropage(u64int pa)
{
	volatile u64int *p;
	int i;

	p = (volatile u64int*)(uintptr)(pa + KZERO);
	for(i = 0; i < PageSize/8; i++)
		p[i] = 0;
}

static int
pageiszero(u64int pa)
{
	volatile u64int *p;
	int i;

	p = (volatile u64int*)(uintptr)(pa + KZERO);
	for(i = 0; i < PageSize/8; i++)
		if(p[i] != 0)
			return 0;
	return 1;
}

static int
overlaps(u64int abase, u64int aend, u64int bbase, u64int bend)
{
	return abase < bend && bbase < aend;
}

u64int
arm64meminit(BootInfo *b, u64int blobpa, u64int kstart, u64int kend)
{
	BootMem *m;
	u64int base, end, total;
	int i;

	nranges = 0;
	nfreed = 0;
	freebytes = 0;
	memerr = 0;
	for(i = 0; i < NPages/64; i++)
		pageused[i] = 0;
	if(b == nil || b->mmapcount == 0){
		memerr = 5;
		return 0;
	}

	for(i = 0; i < b->mmapcount; i++){
		m = (BootMem*)(uintptr)(blobpa + KZERO + b->mmapoff
		    + (u64int)i*b->mmapentsize);
		if(oplanbootmemclass(m->type) != OplanBootRAM)
			continue;
		base = m->base;
		end = m->base + m->len;
		if(end < base)
			continue;
		if(base < RAMBASE)
			base = RAMBASE;
		if(end > RAMEND)
			end = RAMEND;
		base = pageup(base);
		end = pagedown(end);
		addrange(base, end);
	}

	/* the blob sits in loader data, so the map calls the blob's pages RAM */
	subtract(pagedown(kstart), pageup(kend));
	subtract(pagedown(blobpa), pageup(blobpa + b->totalsize));

	total = 0;
	for(i = 0; i < nranges; i++){
		/* every free range is aligned, mapped, and outside what we keep */
		if((ranges[i].base & (PageSize-1)) != 0
		|| (ranges[i].end & (PageSize-1)) != 0){
			memerr = 1;
			return 0;
		}
		if(ranges[i].base < RAMBASE || ranges[i].end > RAMEND){
			memerr = 2;
			return 0;
		}
		if(overlaps(ranges[i].base, ranges[i].end, kstart, kend)){
			memerr = 3;
			return 0;
		}
		if(overlaps(ranges[i].base, ranges[i].end, blobpa,
		    blobpa + b->totalsize)){
			memerr = 4;
			return 0;
		}
		total += ranges[i].end - ranges[i].base;
	}
	freebytes = total;
	return total;
}

u64int
arm64pagealloc(void)
{
	u64int pa;
	int i, found;

	if(nfreed > 0){
		pa = freed[--nfreed];
	}else{
		found = 0;
		pa = 0;
		for(i = 0; i < nranges; i++){
			if(ranges[i].base >= ranges[i].end)
				continue;
			pa = ranges[i].base;
			ranges[i].base += PageSize;
			if(ranges[i].base >= ranges[i].end)
				ranges[i] = ranges[--nranges];
			found = 1;
			break;
		}
		if(!found)
			return 0;
	}
	if(pageisused(pa))
		return 0;
	setpageused(pa, 1);
	zeropage(pa);
	freebytes -= PageSize;
	return pa;
}

int
arm64pagefree(u64int pa)
{
	if((pa & (PageSize-1)) != 0 || pa < RAMBASE || pa + PageSize > RAMEND)
		return 0;
	if(!pageisused(pa) || nfreed >= MaxFreed)
		return 0;	/* not allocated, already freed, or no room */
	setpageused(pa, 0);
	freed[nfreed++] = pa;
	freebytes += PageSize;
	return 1;
}

u64int
arm64memfree(void)
{
	return freebytes;
}

static void
fillpage(u64int pa, u64int v)
{
	volatile u64int *p;
	int i;

	p = (volatile u64int*)(uintptr)(pa + KZERO);
	for(i = 0; i < PageSize/8; i++)
		p[i] = v + (u64int)i;
}

int
arm64memtest(int n)
{
	u64int pages[MaxFreed], again[MaxFreed], pa, f0;
	int i, j;

	if(n < 1 || n > MaxFreed)
		return 0;
	f0 = freebytes;
	if(f0 < (u64int)n*PageSize)
		return 0;

	/* round 1: distinct and zeroed; dirty each page so round 2 tests zeroing */
	for(i = 0; i < n; i++){
		if((pa = arm64pagealloc()) == 0)
			return 0;
		if((pa & (PageSize-1)) != 0 || pa < RAMBASE
		|| pa + PageSize > RAMEND || !pageiszero(pa))
			return 0;
		for(j = 0; j < i; j++)
			if(pages[j] == pa)
				return 0;
		pages[i] = pa;
		fillpage(pa, 0x9e3779b97f4a7c15ULL + (u64int)i);
	}
	if(freebytes != f0 - (u64int)n*PageSize)
		return 0;
	for(i = 0; i < n; i++)
		if(!arm64pagefree(pages[i]))
			return 0;
	if(freebytes != f0)
		return 0;
	/* a page may leave the allocator once; the bitmap refuses a second free */
	if(arm64pagefree(pages[0]) != 0 || freebytes != f0)
		return 0;

	/*
	 * Round 2 takes all n pages before freeing any, so the LIFO free list
	 * cannot rotate one page n times: every page must come back once,
	 * distinct and zeroed despite the round-1 contents.
	 */
	for(i = 0; i < n; i++){
		if((pa = arm64pagealloc()) == 0)
			return 0;
		if(!pageiszero(pa))
			return 0;
		for(j = 0; j < i; j++)
			if(again[j] == pa)
				return 0;
		again[i] = pa;
	}
	for(i = 0; i < n; i++){
		for(j = 0; j < n; j++)
			if(again[i] == pages[j])
				break;
		if(j == n)
			return 0;
	}
	for(i = 0; i < n; i++)
		if(!arm64pagefree(again[i]))
			return 0;
	return freebytes == f0;
}
