#include <u.h>
#include <a.out.h>
#include "fns.h"
#include "mem.h"

#define ROUNDUP(x, n)	(((x) + (n)-1) & ~((uintptr)(n)-1))

char hex[] = "0123456789abcdef";

void
tracehex(char *label, uvlong value)
{
	char buf[96], *p;
	int n;

	p = buf;
	n = strlen(label);
	memmove(p, label, n);
	p += n;
	p = hexfmt(p, 0, value);
	*p++ = '\n';
	*p = '\0';
	print(buf);
}

/*
 * Capture buffer for print()'s own text: the BootInfo blob's log section,
 * set up by efimain() (efi.c) before anything is printed.  Text past
 * logcap is not captured, only shown.
 */
char *logbuf;
int logcap;
int logused;

void
print(char *s)
{
	while(*s != 0){
		if(logbuf != nil && logused < logcap)
			logbuf[logused++] = *s;
		if(*s == '\n')
			putc('\r');
		putc(*s++);
	}
}

int
readn(void *f, void *data, int len)
{
	uchar *p, *e;

	p = data;
	e = p + len;
	while(p < e){
		if((len = read(f, p, e - p)) <= 0)
			break;
		p += len;
	}

	return p - (uchar*)data;
}

void
memmove(void *dst, void *src, int n)
{
	uchar *d = dst;
	uchar *s = src;

	if(d < s){
		while(n-- > 0)
			*d++ = *s++;
	} else if(d > s){
		s += n;
		d += n;
		while(n-- > 0)
			*--d = *--s;
	}
}

int
memcmp(void *src, void *dst, int n)
{
	uchar *d = dst;
	uchar *s = src;
	int r = 0;

	while(n-- > 0){
		r = *d++ - *s++;
		if(r != 0)
			break;
	}

	return r;
}

int
strlen(char *s)
{
	char *p = s;

	while(*p != '\0')
		p++;

	return p - s;
}

char*
strchr(char *s, int c)
{
	for(; *s != 0; s++)
		if(*s == c)
			return s;

	return nil;
}

void
memset(void *dst, int v, int n)
{
	uchar *d = dst;

	while(n > 0){
		*d++ = v;
		n--;
	}
}

/*
 * Reads one line into buf, at most max bytes including the terminating nul.
 * Returns the line length, 0 on eof/no more input, -1 if the line does not
 * fit (nothing is written past buf+max in that case), and -2 if eof was
 * reached after a nonempty line that did not end in LF.  A plan9.ini file
 * must end in LF; a partial final line is not silently accepted.  max<=0
 * always returns -1 without touching *buf.
 */
static int
readline(void *f, char *buf, int max)
{
	static char white[] = "\t ";
	char *p, *e;

	e = buf + max - 1;	/* leave room for the nul */
	p = buf;
	do{
		if(f == nil)
			putc('>');
		for(;;){
			if(p >= e)
				return -1;
			if(f == nil){
				while((*p = getc()) == 0)
					;
				if(p == buf && (*p == '\b' || strchr(white, *p) != nil))
					continue;
				putc(*p);
				if(*p == '\r')
					putc('\n');
				else if(*p == '\b'){
					p--;
					putc(' ');
					putc('\b');
					continue;
				}
			}else if(read(f, p, 1) <= 0)
				return p == buf? 0: -2;
			if(strchr("\r\n", *p) != nil)
				break;
			if(p == buf && strchr(white, *p) != nil)
				continue;	/* whitespace on start of line */
			p++;
		}
		while(p > buf && strchr(white, p[-1]))
			p--;
	}while(p == buf);
	*p = 0;

	return p - buf;
}

static int
timeout(int ms)
{
	while(ms > 0){
		if(getc() != 0)
			return 1;
		usleep(100000);
		ms -= 100;
	}
	return 0;
}

/* the plan9.ini text: the BootInfo blob's config section (efi.c) */
#define CONF	confaddr
#define	CONFLEN	(8*1024)	/* efi.c's ConfCap */
#define	CONFEND	(CONF+CONFLEN)

static char *confend;

/* bytes of plan9.ini text in CONF, with its terminating NUL */
int
conflen(void)
{
	return confend - CONF + 1;
}

char*
findconf(char *s)
{
	char *p, *e;
	int n = strlen(s);
	for(p = CONF; p < confend; p = e+1){
		for(e = p; e < confend && *e != '\n'; e++){
			if(*e == '\0')
				return nil;
		}
		if(e - p >= n && memcmp(p, s, n) == 0)
			return p;
	}
	return nil;
}

/* buf is max bytes; too long a value is treated as not found, not truncated */
static char*
getconf(char *s, char *buf, int max)
{
	char *p, *e;

	if((p = findconf(s)) == nil)
		return nil;
	p += strlen(s);
	for(e = p; *e != '\n'; e++)
		;
	if(e - p >= max)
		return nil;
	memmove(buf, p, e - p);
	buf[e - p] = '\0';
	return buf;
}

static int
delconf(char *s)
{
	char *p, *e;

	if((p = findconf(s)) == nil)
		return 0;
	for(e = p; *e != '\n'; e++)
		;
	e++;
	memmove(p, e, confend - e);
	confend -= e - p;
	*confend = '\0';
	return 1;
}

char*
configure(void *f, char *path)
{
	char *line, *kern, *s, *p;
	int inblock, nowait, n;
	static int once = 1;

	if(once){
		once = 0;
Clear:
		print("[P2 L12] clear configuration and collect UEFI data\n");
		confend = CONF;
		memset(confend, 0, CONFLEN);
		eficonfig();
	}
	nowait = 1;
	inblock = 0;
Loop:
	for(;;){
		line = confend+1;
		n = readline(f, line, CONFEND - line);
		if(n == 0)
			break;
		if(n == -2){
			/*
			 * The format requires a final LF.  Stop here rather than
			 * silently dropping the last setting and booting with a
			 * different configuration (for example the default CPU count).
			 */
			print("plan9.ini: missing final newline\n");
			for(;;)
				;
		}
		if(n < 0){
			print("plan9.ini: line too long, ignoring the rest\n");
			break;
		}
		if(*line == 0 || strchr("#;=", *line) != nil)
			continue;
		if(*line == '['){
			inblock = memcmp("[common]", line, 8) != 0;
			continue;
		}
		if(memcmp("boot", line, 5) == 0){
			nowait=1;
			break;
		}
		if(memcmp("wait", line, 5) == 0){
			nowait=0;
			continue;
		}
		if(memcmp("show", line, 5) == 0){
			print(CONF);
			continue;
		}
		if(memcmp("clear", line, 5) == 0){
			if(line[5] == '\0'){
				print("ok\n");
				goto Clear;
			} else if(line[5] == ' ' && delconf(line+6))
				print("ok\n");
			continue;
		}
		if(inblock != 0 || (p = strchr(line, '=')) == nil)
			continue;
		*p++ = 0;
		delconf(line);
		s = confend;
		memmove(confend, line, n = strlen(line)); confend += n;
		*confend++ = '=';
		memmove(confend, p, n = strlen(p)); confend += n;
		*confend++ = '\n';
		*confend = 0;
		print(s);
	}
	kern = getconf("bootfile=", path, MAXPATH);

	if(f != nil){
		print("[P2 L12] plan9.ini read complete\n");
		close(f);
		f = nil;

		if(kern != nil && (nowait==0 || timeout(1000)))
			goto Loop;
	}

	if(kern == nil){
		print("no bootfile\n");
		goto Loop;
	}
	while((p = strchr(kern, '!')) != nil)
		kern = p+1;

	return kern;
}

static char*
numfmt(char *s, ulong b, ulong i, ulong a)
{
	char *r;

	if(i == 0){
		ulong v = a;
		while(v != 0){
			v /= b;
			i++;
		}
		if(i == 0)
			i = 1;
	}

	s += i;
	r = s;
	while(i > 0){
		*--s = hex[a % b];
		a /= b;
		i--;
	}
	return r;
}

char*
hexfmt(char *s, int i, uvlong a)
{
	if(i > 8 || i == 0 && (a>>32) != 0){
		s = numfmt(s, 16, i ? i-8 : 0, a>>32);
		i = 8;
	}
	return numfmt(s, 16, i, a);
}

char*
decfmt(char *s, int i, ulong a)
{
	return numfmt(s, 10, i, a);
}

static ulong
beswal(ulong l)
{
	uchar *p = (uchar*)&l;
	return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

static uvlong
beswall(uvlong l)
{
	uchar *p = (uchar*)&l;
	return ((uvlong)p[0]<<56) | ((uvlong)p[1]<<48) | ((uvlong)p[2]<<40) | ((uvlong)p[3]<<32) | ((uvlong)p[4]<<24) | ((uvlong)p[5]<<16) | ((uvlong)p[6]<<8) | (uvlong)p[7];
}


/*
 * Minimal ELF64 AMD64 kernel loader.  Only ET_EXEC, little-endian, x86-64
 * program-header images are accepted; ET_DYN/relocatable images are
 * rejected rather than guessed at.  PT_LOAD data is written to p_paddr and
 * BSS is zeroed by the loader.  The old a.out path below is unchanged.
 */
#ifndef KZERO
#define KZERO 0xffffffff80000000ull	/* AMD64 kernel half; a.out path unchanged */
#endif

enum {
	ElfMagic0 = 0x7f,
	ElfClass64 = 2,
	ElfData2LSB = 1,
	ElfTypeExec = 2,
	ElfMachineX86_64 = 62,
	ElfPTLoad = 1,
	ElfPFX = 1,
	ElfMaxPh = 16,
};

typedef struct Elf64Ehdr Elf64Ehdr;
typedef struct Elf64Phdr Elf64Phdr;
struct Elf64Ehdr {
	uchar	ident[16];
	u16int	type;
	u16int	machine;
	u32int	version;
	u64int	entry;
	u64int	phoff;
	u64int	shoff;
	u32int	flags;
	u16int	ehsize;
	u16int	phentsize;
	u16int	phnum;
	u16int	shentsize;
	u16int	shnum;
	u16int	shstrndx;
};
struct Elf64Phdr {
	u32int	type;
	u32int	flags;
	u64int	offset;
	u64int	vaddr;
	u64int	paddr;
	u64int	filesz;
	u64int	memsz;
	u64int	align;
};

static int
skipn(void *f, uvlong n)
{
	uchar buf[128];
	int k;

	while(n > 0){
		k = n > sizeof buf? sizeof buf: n;
		if(readn(f, buf, k) != k)
			return -1;
		n -= k;
	}
	return 0;
}

static char*
bootelf(void *f, uchar *magic)
{
	Elf64Ehdr eh;
	Elf64Phdr ph[ElfMaxPh];
	uvlong cur, entry, end;
	uvlong lpa[ElfMaxPh], lsz[ElfMaxPh];
	uchar *d;
	int i, j, n, nload;
	char *errmsg, *why;

	errmsg = "bad ELF kernel";
	memmove(&eh, magic, 4);
	if(readn(f, (uchar*)&eh+4, sizeof eh - 4) != sizeof eh - 4)
		return "short ELF header";
	if(eh.ident[0] != ElfMagic0 || eh.ident[1] != 'E' ||
	   eh.ident[2] != 'L' || eh.ident[3] != 'F')
		return "bad ELF magic";
	if(eh.ident[4] != ElfClass64 || eh.ident[5] != ElfData2LSB)
		return "unsupported ELF class/endianness";
	if(eh.type != ElfTypeExec || eh.machine != ElfMachineX86_64)
		return "unsupported ELF type/machine";
	if(eh.version != 1 || eh.ehsize < sizeof eh)
		return "unsupported ELF version/header size";
	if(eh.phentsize != sizeof(Elf64Phdr) || eh.phnum == 0 ||
	   eh.phnum > ElfMaxPh)
		return "unsupported ELF program headers";
	if(eh.phoff < sizeof eh)
		return "bad ELF program-header offset";
	if(skipn(f, eh.phoff - sizeof eh) != 0)
		return "short ELF program headers";
	if(readn(f, ph, eh.phnum * sizeof ph[0]) != eh.phnum * sizeof ph[0])
		return "short ELF program headers";
	cur = eh.phoff + eh.phnum * sizeof ph[0];

	nload = 0;
	for(i = 0; i < eh.phnum; i++){
		if(ph[i].type != ElfPTLoad)
			continue;
		if(ph[i].filesz > ph[i].memsz || ph[i].paddr == 0 ||
		   ph[i].paddr + ph[i].memsz < ph[i].paddr)
			return "bad ELF load segment size/address";
		if(ph[i].vaddr != ph[i].paddr + KZERO)
			return "ELF segment is not a KZERO image";
		if(ph[i].align != 0 && (ph[i].align & (ph[i].align-1)) != 0)
			return "bad ELF segment alignment";
		if(ph[i].paddr < 0x1c000)
			return "ELF segment overlaps boot tables/Mach";
		if(ph[i].offset < cur)
			return "out-of-order ELF segment";
		for(j = 0; j < i; j++)
			if(ph[j].type == ElfPTLoad &&
			   ph[i].paddr < ph[j].paddr + ph[j].memsz &&
			   ph[j].paddr < ph[i].paddr + ph[i].memsz)
				return "overlapping ELF segments";
		if(efialloc(ph[i].paddr, ph[i].memsz) != 0){
			errmsg = "cannot claim ELF segment memory";
			goto bad;
		}
		lpa[nload] = ph[i].paddr;
		lsz[nload] = ph[i].memsz;
		nload++;
		if(skipn(f, ph[i].offset - cur) != 0){
			errmsg = "short ELF file before segment";
			goto bad;
		}
		d = (uchar*)(uintptr)ph[i].paddr;
		if(readn(f, d, ph[i].filesz) != ph[i].filesz){
			errmsg = "short ELF segment data";
			goto bad;
		}
		if(ph[i].memsz > ph[i].filesz)
			memset(d + ph[i].filesz, 0, ph[i].memsz - ph[i].filesz);
		cur = ph[i].offset + ph[i].filesz;
		end = ph[i].paddr + ph[i].memsz;
		if(end < ph[i].paddr){
			errmsg = "bad ELF segment end";
			goto bad;
		}
	}
	if(nload == 0)
		return "ELF kernel has no loadable segments";
	if(eh.entry < KZERO || eh.entry >= KZERO + 0x10000000ull){
		errmsg = "ELF entry outside kernel half";
		goto bad;
	}
	entry = eh.entry - KZERO;
	for(i = 0; i < eh.phnum; i++)
		if(ph[i].type == ElfPTLoad &&
		   (ph[i].flags & ElfPFX) != 0 &&
		   eh.entry >= ph[i].vaddr &&
		   eh.entry < ph[i].vaddr + ph[i].memsz)
			goto found;
	errmsg = "ELF entry not in executable segment";
	goto bad;

found:
	close(f);
	print("[P2 L22] ELF kernel file closed\n");
	print("[P2 L23] stop boot device\n");
	if(stop) (*stop)();
	fbmark(1);
	n = bootexit();
	if(n != 0){
		if(n == -1)
			print("[P2 L25] FATAL: GetMemoryMap failed\n");
		else if(n == -2)
			print("[P2 L25] FATAL: ExitBootServices failed after retries\n");
		else
			print("[P2 L25] FATAL: memory-map buffer missing\n");
		for(;;)
			;
	}
	fbmark(2);
	fbmark(3);
	archjump((void*)(uintptr)entry, bootinfofinish());
	return errmsg;	/* not reached */

bad:
	for(i = 0; i < nload; i++)
		efifree(lpa[i], lsz[i]);
	return errmsg;
}

char*
bootkern(void *f)
{
	uchar *e, *d, *t;
	uvlong entry;
	ulong n, ktext, elen, dround;
	char *errmsg, *why;
	Exec ex;

	/*
	 * elen != 0 from the efialloc() below on marks the kernel's memory
	 * range as claimed; every failure path from there on goes through
	 * Error, which frees it again.  Without that a retry (efimain's
	 * loop, eg after a transient read error) would find the same range
	 * already allocated and fail immediately instead of trying again.
	 */
	e = nil;
	elen = 0;
	errmsg = "i/o error";

	{
		uchar magic[4];

		if(readn(f, magic, sizeof magic) != sizeof magic)
			return "bad header";
		if(magic[0] == ElfMagic0 && magic[1] == 'E' &&
		   magic[2] == 'L' && magic[3] == 'F')
			return bootelf(f, magic);
		memmove(&ex, magic, sizeof magic);
	}
	if(readn(f, (uchar*)&ex + 4, sizeof(ex) - 4) != sizeof(ex) - 4)
		return "bad header";
	print("[P2 L14] kernel header read\n");

	entry = beswal(ex.entry);
	switch(beswal(ex.magic)){
	case S_MAGIC:
	case R_MAGIC:
		if(readn(f, &entry, 8) != 8)
			goto Error;
		entry = beswall(entry);
		break;
	case I_MAGIC:
		break;
	default:
		return "bad magic";
	}
	e = (uchar*)(uintptr)archentry(entry);	/* where the ISA's kernel goes, eg archx64.c */
	tracehex("[P2 L15] kernel entry physical=0x", (uvlong)e);

	/*
	 * Firmware marks free memory no-execute; claim the kernel's range as
	 * loader code so the firmware's page tables let it run.
	 */
	ktext = beswal(ex.text);
	dround = archdataround();	/* where the ISA's linker starts data after text */
	elen = PGROUND(ROUNDUP((uintptr)e + ktext, dround) + beswal(ex.data)) + PGROUND(beswal(ex.bss)) - (uintptr)e;
	tracehex("[P2 L16] kernel text bytes=0x", ktext);
	tracehex("[P2 L16] kernel data bytes=0x", beswal(ex.data));
	tracehex("[P2 L16] kernel bss bytes=0x", beswal(ex.bss));
	tracehex("[P2 L16] kernel allocation bytes=0x", elen);
	if(efialloc((uvlong)e, elen) != 0){
		elen = 0;
		return "cannot claim the kernel's memory range";
	}
	print("[P2 L17] kernel memory claimed executable\n");
	if((why = archcheck()) != nil){
		errmsg = why;
		goto Error;
	}
	print("[P2 L18] entry state checked\n");

	t = e;
	n = beswal(ex.text);
	print("[P2 L19] load kernel text\n");
	if(readn(f, t, n) != n)
		goto Error;
	t += n;
	d = (uchar*)ROUNDUP((uintptr)t, dround);
	memset(t, 0, d - t);
	n = beswal(ex.data);
	print("[P2 L20] load kernel data\n");
	if(readn(f, d, n) != n)
		goto Error;
	d += n;
	t = (uchar*)PGROUND((uintptr)d);
	t += PGROUND(beswal(ex.bss));
	memset(d, 0, t - d);
	print("[P2 L21] kernel loaded and bss cleared\n");

	close(f);
	print("[P2 L22] kernel file closed\n");

	/* stop device */
	print("[P2 L23] stop boot device\n");
	if(stop) (*stop)();
	print("[P2 L23] boot device stopped\n");

	print("[P2 L24] framebuffer markers: top-left 1=EBS start 2=EBS done 3=jump, then the kernel's\n");
	print("[P2 L24] boot\n");
	fbmark(1);

	/* the file is closed already, so there is nothing to return to */
	n = bootexit();
	if(n != 0){
		if(n == -1)
			print("[P2 L25] FATAL: GetMemoryMap failed\n");
		else if(n == -2)
			print("[P2 L25] FATAL: ExitBootServices failed after retries\n");
		else
			print("[P2 L25] FATAL: memory-map buffer missing\n");
		for(;;)
			;
	}
	fbmark(2);

	/*
	 * No print() from here on: ConOut is gone.  The kernel gets the
	 * BootInfo blob where it is, its address in RDI (Plan2001 Boot ABI
	 * v1, sys/include/bootinfo.h) - nothing is copied to any fixed
	 * address.
	 */
	fbmark(3);
	archjump(e, bootinfofinish());

Error:
	if(elen != 0)
		efifree((uvlong)e, elen);
	return errmsg;
}
