#include <u.h>
#include "fns.h"
#include "efi.h"
#include "mem.h"

/*
 * The AMD64 side of the loader, for bootx64.efi: what efi.c and sub.c do
 * the same on every ISA is there; what only an AMD64 kernel needs - where
 * it goes, what the machine must look like when it is entered, how - is
 * here, behind the arch*() calls (fns.h).  Plan2001 Boot ABI v1, AMD64
 * entry: docs/boot-abi-amd64.md.
 */

void jump64(void *pc, void *bootinfo);
uvlong getcr3(void);
uvlong getcr4(void);
uvlong rdtsc(void);

/*
 * TSC frequency, measured against the firmware's own timer (Stall).  Not as
 * accurate as the kernel's own HPET-based measurement, but available before
 * any kernel code runs, and a sanity check for it.  0 if RDTSC is not usable
 * this early for some reason; the kernel falls back to its own calibration.
 */
static void
tscconf(BootInfo *bi)
{
	uvlong t0, t1;
	enum { Ms = 50 };

	t0 = rdtsc();
	eficall(ST->BootServices->Stall, (UINTN)(Ms*1000));
	t1 = rdtsc();
	if(t1 > t0)
		bi->tscfreq = (t1 - t0) * 1000 / Ms;
}

/* the ISA, and ISA facts the firmware can tell us, into the BootInfo: the TSC frequency */
void
archconf(BootInfo *bi)
{
	bi->arch = BootArchAmd64;
	print("[P2 L07] TSC: measure\n");
	tscconf(bi);
	print(bi->tscfreq != 0? "[P2 L07] TSC: ok\n": "[P2 L07] TSC: unavailable\n");
}

/*
 * The kernel's physical entry from the one in its a.out header: it is
 * linked at KZERO (0xffffffff80000000) plus its physical address, and
 * loaded at that physical address.
 */
uvlong
archentry(uvlong entry)
{
	return entry & 0x0FFFFFFFULL;
}

/* where 6l starts the kernel's data after its text: the next page */
ulong
archdataround(void)
{
	return 4096;
}

/*
 * May the BootInfo blob lie at [pa, pa+len)?  Not where the kernel goes:
 * its image, at the fixed physical address it is linked for and claimed
 * only later (bootkern()), and its boot page tables and Mach at
 * KBOOTLO..KBOOTHI (mem.h), which _efi64 clears on entry - all below
 * KernelLow.
 */
enum {
	KernelLow = 16*1024*1024,
};

int
archblobok(uvlong pa, uvlong)
{
	return pa >= KernelLow;
}

/*
 * The kernel is entered at its 64-bit entry with the firmware's page tables
 * still active, so it must be safe: paging must be 4-level (a processor in
 * long mode cannot change that) and none of the firmware's page tables may
 * lie in the range the kernel entry clears.
 */
enum {
	PtPresent	= 1<<0,
	PtLarge		= 1<<7,
	Cr4La57		= 1<<12,
};
#define PTADDR(e)	((e) & 0x000FFFFFFFFFF000ull)

static int
ptbad(uvlong pa, uvlong lo, uvlong hi)
{
	return pa < hi && pa+4096 > lo;
}

static int
ptclear(uvlong lo, uvlong hi)
{
	uvlong *l4, *l3, *l2, e;
	int i, j, k;

	l4 = (uvlong*)PTADDR(getcr3());
	if(ptbad((uvlong)l4, lo, hi))
		return 0;
	for(i = 0; i < 512; i++){
		if((l4[i] & PtPresent) == 0)
			continue;
		l3 = (uvlong*)PTADDR(l4[i]);
		if(ptbad((uvlong)l3, lo, hi))
			return 0;
		for(j = 0; j < 512; j++){
			e = l3[j];
			if((e & PtPresent) == 0 || (e & PtLarge) != 0)
				continue;
			l2 = (uvlong*)PTADDR(e);
			if(ptbad((uvlong)l2, lo, hi))
				return 0;
			for(k = 0; k < 512; k++){
				e = l2[k];
				if((e & PtPresent) == 0 || (e & PtLarge) != 0)
					continue;
				if(ptbad(PTADDR(e), lo, hi))
					return 0;
			}
		}
	}
	return 1;
}


/* nil if the kernel can be entered from the state we are in, else why not */
char*
archcheck(void)
{
	if(getcr4() & Cr4La57)
		return "5-level paging is active, the kernel needs 4-level";
	if(!ptclear(KBOOTLO, KBOOTHI))
		return "firmware page tables overlap the kernel's boot area";
	return nil;
}

/* enter the kernel: RDI = BootInfo, RSI = 0 (x64.s); does not return */
void
archjump(void *entry, void *bootinfo)
{
	jump64(entry, bootinfo);
}
