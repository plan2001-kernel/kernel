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
#include	"omemory.i"

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
 * Boot manifest v1: a bounded, explicit set of independent process roots.
 * Syntax:
 *   version=1
 *   [process.name]
 *   source=geo:PARTUUID=<uuid>:OID=<number> | memory:selftest
 *   owner=<name>     (required, immutable identity, not authority by itself)
 *   args=-N          (optional, whitespace-separated argv for the program)
 *   cpu=0            (optional, -1 = scheduler chooses)
 *   required=true    (optional, diagnostic label; never blocks siblings)
 *
 * One configured manifest replaces the implicit obootexec bootstrap.
 * Unsupported sources, including network sources and a second GE/O volume,
 * fail as individual process entries, never silently reroute.
 */
enum {
	ManifestMaxprocs = 8,
	ManifestMaxline = 384,
	SourceGeo = 1,
	SourceMemory = 2,
};

typedef struct Bootproc Bootproc;
struct Bootproc {
	char name[32];
	char ownername[32];
	char volspec[128];
	char argline[256];
	char argv[Maxargv][128];
	uvlong oid;
	int argc;
	int cpu;
	int kind;
	int required;
	int seen;
};

static Bootproc manifestprocs[ManifestMaxprocs];
static int manifestnprocs;

static int
manifestname(char *s)
{
	int i;
	if(*s == 0)
		return 0;
	for(i=0; s[i]; i++)
		if(!(s[i] >= 'a' && s[i] <= 'z') &&
		   !(s[i] >= 'A' && s[i] <= 'Z') &&
		   !(s[i] >= '0' && s[i] <= '9') &&
		   s[i] != '_' && s[i] != '-')
			return 0;
	return 1;
}

static int
manifestarg(Bootproc *b, char *arg)
{
	int n;
	if(b->argc >= Maxargv)
		return -1;
	n = strlen(arg);
	if(n <= 0 || n >= sizeof b->argv[0])
		return -1;
	memmove(b->argv[b->argc], arg, n+1);
	b->argc++;
	return 0;
}

/* Reject any malformed or unsupported entry before starting the first one. */

static void manifestparse(void);
static void manifeststart(void);


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
	Bootproc *bp;
	int cpu;

	bp = arg;
	/* No legacy Dev init on this path: start the Dev-independent services. */
	consinit();
	if(waserror()){
		if(bp != nil){
			print("boot-manifest: FAIL %s: %s\n", bp->name, up->errstr);
			pexit("manifest launch failed");
		}
		panic("o-server bootstrap: %s", up->errstr);
	}
	img = bp != nil && bp->kind == SourceMemory ? omemory : obootoid;
	imglen = bp != nil && bp->kind == SourceMemory ? sizeof(omemory) : sizeof(obootoid);
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
	up->boot = bp == nil || bp->kind == SourceGeo;
	up->owner = bp == nil ? ownerroot() : ownercreate(bp->ownername, strlen(bp->ownername));

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
	if(bp != nil){
		oa.vollen = strlen(bp->volspec);
		memmove(oa.volspec, bp->volspec, oa.vollen);
		oa.oid = bp->oid;
		oa.argc = bp->argc;
		for(int ai=0; ai<bp->argc; ai++)
			strcpy(oa.argv[ai], bp->argv[ai]);
	}else{
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

	cpu = bp != nil ? bp->cpu : obootcpu;
	if(cpu >= 0){
		if(!proccpuonline(cpu)){
			if(bp != nil){
				print("boot-manifest: FAIL %s: CPU %d offline\n", bp->name, cpu);
				pexit("manifest CPU offline");
			}
			panic("o-server: obootcpu %d is not online", cpu);
		}
		procwiredexact(up, cpu);
		procfirstlog(up, cpu);
		while(m->machno != cpu || up->firstuser)
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
extern void osbootrootselect(uvlong);

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

	if(bootmanifest() != nil){
		manifestparse();
		up = nil;
		if(eve == nil)
			kstrdup(&eve, "");
		manifeststart();
		return;
	}

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

	/*
	 * Trusted persistent ObjectSpace root selector: one durable graph
	 * OID on the obootvol GE/O volume.  The graph payload carries no
	 * authority; restoration uses only this boot configuration and the
	 * current boot volume capability.
	 */
	p = getconf("obootgraph");
	if(p != nil){
		uvlong v;

		if(parseoid(p, &v) < 0){
			print("o-server: bad obootgraph \"%s\"\n", p);
			panic("o-server: obootgraph must be a decimal or 0x hex OID");
		}
		if(v == 0){
			print("o-server: obootgraph \"%s\"\n", p);
			panic("o-server: obootgraph must be a non-zero OID");
		}
		if(bootkernelonly){
			print("o-server: obootgraph \"%s\" without obootexec\n", p);
			panic("o-server: obootgraph requires an executable bootstrap");
		}
		osbootrootselect(v);
		print("o-server: BOOTROOT oid=%#llux\n", v);
	}else if(bootkernelonly)
		print("o-server: BOOTROOT none (kernel-only)\n");
	else
		print("o-server: BOOTROOT none\n");

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


static void
manifestparse(void)
{
	Bootproc *b;
	char *p, *e, line[ManifestMaxline], *v, *q, *oid;
	int n, version, i;

	p = bootmanifest();
	if(p == nil)
		return;
	manifestnprocs = 0;
	b = nil;
	version = 0;
	while(*p != 0){
		for(e = p; *e && *e != '\n'; e++)
			;
		n = e-p;
		if(*e != '\n' || n >= sizeof line)
			panic("boot-manifest: line too long or unterminated");
		memmove(line, p, n);
		line[n] = 0;
		p = e+1;
		if(line[0] == 0 || line[0] == '#')
			continue;
		if(strcmp(line, "version=1") == 0){
			if(version != 0 || b != nil)
				panic("boot-manifest: invalid version position");
			version = 1;
			continue;
		}
		if(version != 1)
			panic("boot-manifest: version=1 must be first");
		if(line[0] == '['){
			if(n < 11 || strcmp(line+n-1, "]") != 0 ||
			   strncmp(line, "[process.", 9) != 0)
				panic("boot-manifest: invalid process header");
			line[n-1] = 0;
			if(strlen(line+9) >= sizeof manifestprocs[0].name ||
			   !manifestname(line+9) || manifestnprocs >= ManifestMaxprocs)
				panic("boot-manifest: invalid process name/count");
			for(i=0; i<manifestnprocs; i++)
				if(strcmp(manifestprocs[i].name, line+9) == 0)
					panic("boot-manifest: duplicate process name");
			b = &manifestprocs[manifestnprocs++];
			memset(b, 0, sizeof *b);
			strcpy(b->name, line+9);
			b->cpu = -1;
			continue;
		}
		if(b == nil || (v = strchr(line, '=')) == nil)
			panic("boot-manifest: setting outside process");
		*v++ = 0;
		if(strcmp(line, "source") == 0){
			if(b->seen & 1)
				panic("boot-manifest: duplicate source");
			b->seen |= 1;
			if(strcmp(v, "memory:selftest") == 0){
				b->kind = SourceMemory;
			}else if(strncmp(v, "geo:", 4) == 0){
				q = v+4;
				oid = strstr(q, ":OID=");
				if(oid == nil || oid-q >= sizeof b->volspec || oid-q <= 9)
					panic("boot-manifest: bad GE/O source");
				memmove(b->volspec, q, oid-q);
				b->volspec[oid-q] = 0;
				if(strncmp(b->volspec, "PARTUUID=", 9) != 0 ||
				   parseoid(oid+5, &b->oid) != 0 || b->oid == 0)
					panic("boot-manifest: invalid GE/O PARTUUID or OID");
				b->kind = SourceGeo;
			}else
				panic("boot-manifest: unsupported source");
		}else if(strcmp(line, "owner") == 0){
			if(b->seen & 2 || strlen(v) >= sizeof b->ownername || !manifestname(v))
				panic("boot-manifest: bad owner");
			b->seen |= 2;
			strcpy(b->ownername, v);
		}else if(strcmp(line, "cpu") == 0){
			uvlong idx;
			if(b->seen & 4 || parseoid(v, &idx) != 0 || idx > 0x7fffffffULL)
				panic("boot-manifest: invalid cpu index");
			b->seen |= 4;
			b->cpu = (int)idx;
		}else if(strcmp(line, "args") == 0){
			if(b->seen & 8 || strlen(v) >= sizeof b->argline)
				panic("boot-manifest: args too long");
			b->seen |= 8;
			strcpy(b->argline, v);
		}else if(strcmp(line, "required") == 0){
			if(b->seen & 16)
				panic("boot-manifest: duplicate required");
			b->seen |= 16;
			if(strcmp(v, "true") == 0)
				b->required = 1;
			else if(strcmp(v, "false") != 0)
				panic("boot-manifest: required must be true/false");
		}else
			panic("boot-manifest: unknown setting");
	}
	if(version != 1 || manifestnprocs == 0)
		panic("boot-manifest: missing version or processes");
	for(i=0; i<manifestnprocs; i++){
		b = &manifestprocs[i];
		if((b->seen & 3) != 3 || b->kind == 0)
			panic("boot-manifest: incomplete process entry");
		if(b->cpu >= 0 && !proccpuonline(b->cpu))
			panic("boot-manifest: offline cpu in process entry");
		if(b->kind == SourceMemory){
			if(b->seen & 8)
				panic("boot-manifest: memory selftest has no args");
			continue;
		}
		/* One GE/O volume is currently implemented. Enforce equality.
		 * Never pretend a second disk was loaded from the first one. */
		if(obootvollen != 0 && strcmp(obootvol, b->volspec) != 0)
			panic("boot-manifest: GE/O source does not match obootvol");
		if(obootvollen == 0){
			strcpy(obootvol, b->volspec);
			obootvollen = strlen(obootvol);
		}
		if(b->oid == MpyOid){
			manifestarg(b, "micropython");
			manifestarg(b, "-O");
			manifestarg(b, b->volspec);
			manifestarg(b, b->argline[0] ? b->argline : "-N");
		}else{
			if(manifestarg(b, b->name) < 0)
				panic("boot-manifest: invalid argv");
			q = b->argline;
			while(*q){
				while(*q == ' ' || *q == '\t')
					q++;
				if(*q == 0)
					break;
				v = q;
				while(*q != 0 && *q != ' ' && *q != '\t')
					q++;
				n = *q;
				*q = 0;
				if(manifestarg(b, v) < 0)
					panic("boot-manifest: too many args");
				*q = n;
			}
		}
	}
	print("boot-manifest: validated %d independent process entries\n", manifestnprocs);
}

static void
manifeststart(void)
{
	Bootproc *b;
	int i;
	ownerinit();
	/* Do not register an unrelated implicit default program. */
	ownerprogrambootprepare(0);
	for(i=0; i<manifestnprocs; i++){
		b = &manifestprocs[i];
		print("boot-manifest: START %s source=%s cpu=%d required=%d\n",
			b->name, b->kind == SourceGeo ? "geo" : "memory",
			b->cpu, b->required);
		kproc(b->name, ouserproc, b);
	}
}
