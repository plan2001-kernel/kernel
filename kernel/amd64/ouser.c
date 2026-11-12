#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"tos.h"

#include	<a.out.h>
#include	"kbuildid.h"
#include	"../core/owner.h"

/*
 * The single native O-server bootstrap.
 *
 * The kernel-embedded flat image in obootoid.c is copied into the first
 * user text page.  It opens the configured GE/O volume by PARTUUID, opens
 * the "init" Space and OEXECUTEs the MicroPython interpreter stored there.
 */
#include	"obootoid.i"

enum {
	Maxargv		= 8,
	MpyOid		= 0x4f45584543000003ULL,	/* OID_MP_EXEC */
};

typedef struct Oargs Oargs;
typedef struct Oenter Oenter;
struct Oenter
{
	uintptr	sp;
	uintptr	arg;
};

struct Oargs
{
	uvlong	oid;
	long	argc;
	long	vollen;
	char	volspec[128];
	char	argv[Maxargv][128];
};

static char obootvol[128];
static int obootvollen;
static uvlong bootoid = MpyOid;
static int bootismpy = 1;
static int bootkernelonly;
static int bootreqset;
static uvlong bootreqoid;
static int obootcpu = -1;
static const char kernelbuild[] = PLAN2001_KERNEL_BUILD_ID;

/*
 * Strict full-string unsigned parser: decimal digits or 0x-prefixed hex.
 * Empty strings, signs, overflow and trailing characters are rejected.
 */
static int
parseoid(char *s, uvlong *v)
{
	uvlong n;
	int base, d;
	char *p;

	if(s == nil || *s == '\0')
		return -1;
	base = 10;
	p = s;
	if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X')){
		base = 16;
		p += 2;
		if(*p == '\0')
			return -1;
	}
	n = 0;
	for(; *p != '\0'; p++){
		if(*p >= '0' && *p <= '9')
			d = *p - '0';
		else if(base == 16 && *p >= 'a' && *p <= 'f')
			d = *p - 'a' + 10;
		else if(base == 16 && *p >= 'A' && *p <= 'F')
			d = *p - 'A' + 10;
		else
			return -1;
		if(d >= base)
			return -1;
		if(n > (~(uvlong)0 - d)/base)
			return -1;
		n = n*base + d;
	}
	*v = n;
	return 0;
}

/*
 * The image is the plan9 amd64 a.out produced by 6l: a 32-byte Exec
 * header, an eight-byte expanded entry, then text and data.  The
 * kernel copies the whole image to UTZERO; the established touser()
 * entry point is UTZERO+0x28, exactly the linked entry address.
 */
static void
ouserimage(uchar *img, ulong n, char *tag, uintptr *entryp, ulong *textp)
{
	Exec *e;
	uvlong entry;
	ulong text;
	ulong hdr;

	hdr = sizeof(Exec)+sizeof(uvlong);
	if(n < hdr)
		panic("%s: user image too short: %lud", tag, n);
	e = (Exec*)img;
	if(beswal(e->magic) != AOUT_MAGIC)
		panic("%s: user image bad magic %#lux", tag, beswal(e->magic));
	entry = beswav(*(uvlong*)(e+1));
	text = beswal(e->text);
	if(entry != UTZERO+hdr)
		panic("%s: user entry %#llux, want %#llux", tag, entry, UTZERO+hdr);
	if((entry & (BY2WD-1)) != 0)
		panic("%s: user entry misaligned %#llux", tag, entry);
	if(text == 0 || text > BY2PG || n > BY2PG)
		panic("%s: user image text %lud, image %lud", tag, text, n);
	if((uvlong)hdr+text > n)
		panic("%s: user image truncated: header+text %lud, image %lud",
			tag, hdr+text, n);
	if(beswal(e->data) != 0 || beswal(e->bss) != 0)
		panic("%s: user image needs data/bss outside the text page", tag);

	*entryp = entry;
	*textp = text;
}

static void
stackpage(Segment *s, uintptr va, void *data, ulong n)
{
	KMap *k;
	Page *p;

	p = newpage(va, nil);
	k = kmap(p);
	memset(VA(k), 0, BY2PG);
	if(data != nil && n > 0)
		memmove(VA(k), data, n);
	kunmap(k);
	segpage(s, p);
}

static void
ouserproc(void *arg)
{
	KMap *k;
	Page *p;
	Segment *ts;
	uintptr entry, sp, argp;
	uchar *img;
	ulong text, imglen;
	Oargs oa;
	Oenter oe;
	char *repl;

	USED(arg);
	/* No legacy Dev init on this path: start the Dev-independent services. */
	consinit();
	if(waserror())
		panic("o-server bootstrap: %s", up->errstr);
	img = obootoid;
	imglen = sizeof(obootoid);
	ouserimage(img, imglen, "o-server", &entry, &text);

	/*
	 * Only the objects needed to execute user code are initialized.
	 * The initial process gets one empty, private Egrp so native ENV
	 * operations work from userspace without exposing confegrp.
	 * proc0's FD group, namec("#/"), cclone and init0 are
	 * deliberately absent.  kproc has already supplied the shared
	 * Pgrp; it remains initialized but unused here.
	 */
	memset(&up->sleep, 0, sizeof up->sleep);
	up->timer.tt = nil;
	up->trend = nil;
	up->alarm = 0;
	up->fdgrp = nil;
	if(up->egrp != nil){
		closeegrp(up->egrp);
		up->egrp = nil;
	}
	up->egrp = newegrp();
	/* Explicit boot role: privileged lifecycle status is never inferred
	 * from ancestry or from a zero parent PID.  The first userspace
	 * process is an ordinary named owner (root), not a kernel override. */
	ownerinit();
	up->boot = 1;
	up->owner = ownerroot();

	ts = newseg(SG_TEXT|SG_RONLY, UTZERO, 1);
	ts->flushme = 1;
	p = newpage(UTZERO, nil);
	k = kmap(p);
	memmove(VA(k), img, imglen);
	memset((uchar*)VA(k)+imglen, 0, BY2PG-imglen);
	kunmap(k);
	segpage(ts, p);
	up->seg[TSEG] = ts;

	up->seg[SSEG] = newseg(SG_STACK|SG_NOEXEC, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	/* Fault in and zero the top stack page so the Tos slot is valid. */
	stackpage(up->seg[SSEG], USTKTOP-BY2PG, nil, 0);

	memset(&oa, 0, sizeof oa);
	oa.vollen = obootvollen;
	memmove(oa.volspec, obootvol, obootvollen);
	oa.oid = bootoid;
	if(bootismpy){
		oa.argc = 4;
		memmove(oa.argv[0], "micropython", 12);
		memmove(oa.argv[1], "-O", 3);
		memmove(oa.argv[2], obootvol, obootvollen+1);
		repl = getconf("obootrepl");
		if(repl != nil && strcmp(repl, "1") == 0)
			memmove(oa.argv[3], "-I", 3);
		else
			memmove(oa.argv[3], "-N", 3);
	} else {
		oa.argc = 2;
		memmove(oa.argv[0], "oexec", 6);
		memmove(oa.argv[1], obootvol, obootvollen+1);
	}

	argp = USTKTOP-2*BY2PG;
	stackpage(up->seg[SSEG], argp, &oa, sizeof oa);
	/* The block lives above the initial stack and is not grown into. */
	sp = argp-256;

	up->kp = 0;
	up->noswap = 0;
	up->privatemem = 0;
	up->iomask = Iomout|Iomdiag;
	procpriority(up, PriNormal, 0);
	procsetup(up);

	flushmmu();
	poperror();

	/* Bootstrap invariant: a private, empty environment group exists
	 * before the first transition to userspace. */
	if(up->egrp == nil || up->egrp->nent != 0)
		panic("o-server: bootstrap Egrp not initialized empty");
	print("o-server: KERNEL-READY pid=%lud entry=%#p text=%lud image=%lud\n",
		up->pid, entry, text, imglen);
	print("o-server: EGRP pid=%lud ref=%ld nent=%d\n",
		up->pid, up->egrp->ref.ref, up->egrp->nent);
	print("o-server: VOLSPEC len=%d %s\n", obootvollen, obootvol);
	print("o-server: ARGV argc=%d", oa.argc);
	{
		int i;

		for(i = 0; i < oa.argc; i++)
			print(" <%s>", oa.argv[i]);
	}
	print("\no-server: OID=%#llux\n", oa.oid);
	if(bootreqset)
		print("o-server: BOOTEXEC requested=%#llux effective=%#llux mode=%s\n",
			bootreqoid, oa.oid, bootismpy ? "mpy" : "generic");
	else
		print("o-server: BOOTEXEC requested=default effective=%#llux mode=%s\n",
			oa.oid, bootismpy ? "mpy" : "generic");
	print("o-server: KERNEL-TO-USER pid=%lud pc=%#p sp=%#p arg=%#p\n",
		up->pid, entry, sp, argp);
	print("Plan2001-O kernel build: %s\n",
		kernelbuild + sizeof(PLAN2001_KERNEL_BUILD_MAGIC) - 1);

	if(obootcpu >= 0){
		if(!proccpuonline(obootcpu))
			panic("o-server: obootcpu %d is not online", obootcpu);
		procwiredexact(up, obootcpu);
		procfirstlog(up, obootcpu);
		while(m->machno != obootcpu || up->firstuser)
			sched();
	}else
		procfirstlog(up, -1);

	splhi();
	fpukexit(nil);
	oe.sp = sp;
	oe.arg = argp;
	otouser(&oe);

	panic("o-server: touser returned");
}

extern int okernelvol(char*);

static void
kernelreadyproc(void *arg)
{
	USED(arg);
	consinit();
	if(okernelvol(obootvol) != 1)
		panic("o-server: kernel-only O volume open failed");
	/* Let the remaining boot services finish before printing the
	 * deterministic marker; concurrent print() calls can interleave. */
	tsleep(&up->sleep, return0, nil, 2000);
	print("o-server: BOOTEXEC requested=none mode=kernel-only\n");
	print("o-server: kernel-only mode: no obootexec configured\n");
	print("PLAN2001 KERNEL READY: kernel-only, no external executable\n");
	for(;;)
		tsleep(&up->sleep, return0, nil, 1000);
}

void
o_user_bootstrap(void)
{
	char *p;
	int n;

	p = getconf("obootvol");
	if(p == nil || strncmp(p, "PARTUUID=", 9) != 0){
		print("o-server: bad obootvol\n");
		panic("o-server: obootvol must be PARTUUID=...");
	}
	n = strlen(p);
	if(n <= 0 || n >= sizeof obootvol){
		print("o-server: obootvol length %d\n", n);
		panic("o-server: obootvol too long");
	}
	memmove(obootvol, p, n);
	obootvol[n] = 0;
	obootvollen = n;

	p = getconf("obootexec");
	if(p == nil){
		/* Kernel-only mode: no external executable is configured.  A
		 * kernel process opens the volume read-only and holds a stable
		 * scheduler idle state; MicroPython is never selected implicitly. */
		bootkernelonly = 1;
		bootismpy = 0;
		bootreqset = 0;
		bootreqoid = 0;
		bootoid = 0;
	}else{
		if(parseoid(p, &bootreqoid) < 0){
			print("o-server: bad obootexec \"%s\"\n", p);
			panic("o-server: obootexec must be a decimal or 0x hex OID");
		}
		if(bootreqoid == 0){
			print("o-server: obootexec \"%s\"\n", p);
			panic("o-server: obootexec must be a non-zero OID");
		}
		bootreqset = 1;
		if(bootreqoid == MpyOid){
			bootoid = MpyOid;
			bootismpy = 1;
		}else{
			bootoid = bootreqoid;
			bootismpy = 0;
		}
	}

	p = getconf("obootcpu");
	if(p != nil){
		uvlong v;

		if(parseoid(p, &v) < 0 || v > 0x7fffffffULL){
			print("o-server: bad obootcpu \"%s\"\n", p);
			panic("o-server: obootcpu must be a non-negative CPU index");
		}
		obootcpu = (int)v;
	}

	if(bootkernelonly)
		ownerprogrambootprepare(0);
	else
		ownerprogrambootprepare(bootoid);
	up = nil;
	if(eve == nil)
		kstrdup(&eve, "");
#ifdef PLAN2001_ALARM_NOTETEST
	/*
	 * Test-only: start the existing alarm kernel service so a real "alarm"
	 * note can be delivered to the test process.  The normal build does not
	 * define this macro; alarm.c and the ALARM syscall are unchanged, and
	 * the published kernel keeps its previous alarm behavior.
	 */
	kproc("alarm", alarmkproc, nil);
#endif
	if(bootkernelonly){
		kproc("*kernelready*", kernelreadyproc, nil);
		return;
	}
	kproc("*ouser*", ouserproc, nil);
}
