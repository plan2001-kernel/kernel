#include	"u.h"
#include	"tos.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/pci.h"
#include	"ureg.h"
#include	"pool.h"
#include	"rebootcode.i"

Conf conf;
int idle_spin;

extern void sdinitcontrollers(void);
extern void uartnreset(void);
extern void usbcore_reset(void);
extern void usbcore_shutdown(void);
extern void sysgefsshutdown(void);
extern int ipv6native_boot_start(void);

extern void (*i8237alloc)(void);

void
confinit(void)
{
	char *p;
	int i, userpcnt;
	ulong kpages;

	if(p = getconf("service")){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	conf.npage = 0;
	for(i=0; i<nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 4000)
		conf.nproc = 4000;

	if(cpuserver) {
		if(userpcnt < 10)
			userpcnt = 70;
		kpages = conf.npage - (conf.npage*userpcnt)/100;
	} else {
		if(userpcnt < 10) {
			if(conf.npage*BY2PG < 16*MB)
				userpcnt = 50;
			else
				userpcnt = 60;
		}
		kpages = conf.npage - (conf.npage*userpcnt)/100;
	}

	/*
	 * can't go past the end of virtual memory.
	 */
	if(kpages > ((uintptr)-KZERO)/BY2PG)
		kpages = ((uintptr)-KZERO)/BY2PG;

	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for.
	 */
	kpages *= BY2PG;
	kpages -= conf.nproc*sizeof(Proc*);
	mainmem->maxsize = kpages;
}

void
machinit(void)
{
	int machno;
	Segdesc *gdt;
	uintptr *pml4;

	machno = m->machno;
	pml4 = m->pml4;
	gdt = m->gdt;
	memset(m, 0, sizeof(Mach));
	m->machno = machno;
	m->pml4 = pml4;
	m->gdt = gdt;
	m->pmach.perf.period = 1;

	/*
	 * For polled uart output at boot, need
	 * a default delay constant. 100000 should
	 * be enough for a while. Cpuidentify will
	 * calculate the real value later.
	 */
	m->loopconst = 100000;
}

void
mach0init(void)
{
	conf.nmach = 1;

	MACHP(0) = (Mach*)CPU0MACH;

	m->machno = 0;
	m->pml4 = (u64int*)CPU0PML4;
	m->gdt = (Segdesc*)CPU0GDT;

	machinit();

	active.machs[0] = 1;
	active.exiting = 0;
}

void
main(void)
{
	char *p;

	bootmark(BMMain);
	bootinfoinit();
	mach0init();
	bootargsinit();
	p = getconf("oboot");
	if(p != nil && strcmp(p, "server") != 0)
		panic("oboot: unsupported boot mode; use oboot=server");
	trapinit0();
	ioinit();
	i8250console();
	quotefmtinstall();
	print("\nPlan 9\n");
	cpuidentify();
	meminit0();
	archinit();
	if(arch->clockinit)
		arch->clockinit();
	meminit();
	ramdiskinit();
	confinit();
	xinit();
	trapinit();
	mathinit();
	if(i8237alloc != nil)
		i8237alloc();
	bootfbinit();
	bootmark(BMMem);
	pcicfginit();
	fbconsoleinit();
	printinit();
	cpuidprint();
	mmuinit();
	if(arch->intrinit)
		arch->intrinit();
	bootinforandinit();
	timersinit();
	if(arch->clockenable)
		arch->clockenable();
	bootinfoclock();
	procinit0();
	initseg();
	links();
	chandevreset();
	/*
	 * UART lifecycle is direct now that the legacy #t Dev is gone.  This
	 * enumerates the physical ports, selects the console and flushes the
	 * early kernel message buffer.
	 */
	uartnreset();
	/*
	 * USB lifecycle is direct now that the legacy #u Dev is gone.  links()
	 * has registered the EHCI/xHCI types; probe controllers once here.
	 * usbcore_init() (root hubs) stays deferred to the first native USBOPEN.
	 */
	usbcore_reset();
	/*
	 * Initialize the PS/2 keyboard service directly; the legacy #b Dev is
	 * gone.
	 */
	kbdinit();
	/*
	 * Native SD owner: profiles without #S still get their controllers
	 * probed and registered here.  The core guard makes the legacy
	 * sddevtab.reset path a no-op in profiles that have it.
	 */
	sdinitcontrollers();
	/*
	 * Native IPv6 networking starts in kernel process context before the
	 * O-server handoff.
	 */
	/* Initialize boot identity here, outside the native network service. */
	kstrdup(&eve, "");
	ipv6native_boot_start();
	bootmark(BMDevs);
	preallocpages();
	pageinit();
	o_user_bootstrap();
	bootmark(BMUser);
	/*
	 * This CPU has finished every per-CPU prerequisite and is at the last
	 * safe handoff to the common scheduler.  Publish readiness only here,
	 * not at the early mach0init() online mark, so exact CPU placement
	 * cannot select a CPU that is online but still unable to schedule.
	 */
	active.sched[0] = 1;
	coherence();
	schedinit();
}

static void
rebootjump(uintptr entry, uintptr code, ulong size)
{
	void (*f)(uintptr, uintptr, ulong);
	uintptr *pte;

	arch->introff();

	/*
	 * This allows the reboot code to turn off the page mapping
	 */
	*mmuwalk(m->pml4, 0, 3, 0) = *mmuwalk(m->pml4, KZERO, 3, 0);
	*mmuwalk(m->pml4, 0, 2, 0) = *mmuwalk(m->pml4, KZERO, 2, 0);

	if((pte = mmuwalk(m->pml4, REBOOTADDR, 1, 0)) != nil)
		*pte &= ~PTENOEXEC;
	if((pte = mmuwalk(m->pml4, REBOOTADDR, 0, 0)) != nil)
		*pte &= ~PTENOEXEC;

	mmuflushtlb(PADDR(m->pml4));

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));

	/* off we go - never to return */
	coherence();
	(*f)(entry, code, size);

	for(;;);
}

void
exit(int)
{
	cpushutdown();
	splhi();

	if(m->machno)
		rebootjump(0, 0, 0);

	/* clear secrets */
	zeroprivatepages();
	poolreset(secrmem);

	arch->reset();
}

void
reboot(void *entry, void *code, ulong size)
{
	/*
	 * rebootjump() below (and rebootcode.s) hand the new kernel off in
	 * 32-bit protected mode, with paging and EFER.LME off.  This
	 * kernel's only entry, _efi64 (pc64/l.s), needs long mode with the
	 * firmware's page tables still active - a mismatch since the 32-bit
	 * entry path was removed; this legacy kexec route remains unsupported.  Refuse here, before
	 * the shutdown below starts undoing device and CPU state that
	 * cannot be undone, rather than run it all and then jump into a
	 * state that is certain to crash or hang with no way back.  Once
	 * rebootcode.s is rewritten for a 64-bit handoff this can go.
	 */
	panic("reboot: no 64-bit kexec handoff yet, refusing to load a new kernel");

	/*
	 * the boot processor is cpu0.  execute this function on it
	 * so that the new kernel has the same cpu0.  this only matters
	 * because the hardware has a notion of which processor was the
	 * boot processor and we look at it at start up.
	 */
	while(m->machno != 0){
		procwired(up, 0);
		sched();
	}
	cpushutdown();
	delay(1000);
	splhi();

	/* turn off buffered serial console */
	serialoq = nil;

	/* GE/O volumes have no Dev registration; halt them directly. */
	sysgefsshutdown();
	/* USB HCI shutdown is direct; safe when no controller was initialized. */
	usbcore_shutdown();
	/* Also shut down the keyboard service if #b is not registered. */
	kbdshutdown();

	/* clear secrets */
	zeroprivatepages();
	poolreset(secrmem);

	/* disable pci devices */
	pcireset();

	rebootjump((uintptr)entry & (ulong)~0xF0000000UL, PADDR(code), size);
}

void
procsetup(Proc *p)
{
	fpuprocsetup(p);

	/* clear debug registers */
	memset(p->pmmu.dr, 0, sizeof(p->pmmu.dr));
	if(m->dr7 != 0){
		m->dr7 = 0;
		putdr7(0);
	}
}

void
procfork(Proc *p)
{
	fpuprocfork(p);
}

void
procrestore(Proc *p)
{
	if(p->pmmu.dr[7] != 0){
		m->dr7 = p->pmmu.dr[7];
		putdr(p->pmmu.dr);
	}

	fpuprocrestore(p);
}

void
procsave(Proc *p)
{
	if(m->dr7 != 0){
		m->dr7 = 0;
		putdr7(0);
	}
	if(p->state == Moribund)
		p->pmmu.dr[7] = 0;

	fpuprocsave(p);

	/*
	 * While this processor is in the scheduler, the process could run
	 * on another processor and exit, returning the page tables to
	 * the free list where they could be reallocated and overwritten.
	 * When this processor eventually has to get an entry from the
	 * trashed page tables it will crash.
	 *
	 * If there's only one processor, this can't happen.
	 * You might think it would be a win not to do this in that case,
	 * especially on VMware, but it turns out not to matter.
	 */
	mmuflushtlb(PADDR(m->pml4));
}

int
pcibiosinit(int *, int *)
{
	return -1;
}

