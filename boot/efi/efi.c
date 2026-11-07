#include <u.h>
#include "fns.h"
#include "efi.h"
#include "mem.h"

UINTN MK;
EFI_HANDLE IH;
EFI_SYSTEM_TABLE *ST;

void* (*open)(char *name);
int (*read)(void *f, void *data, int len);
void (*close)(void *f);
void (*stop)(void);

/*
 * on ia32 and amd64, we use IMAGE_FILE_RELOCS_STRIPPED which
 * disables relocations, so rebase() is a no-op.
 *
 * on arm64, the EFI loader can move our code, so we need to update
 * some of our stored addresses which assume we are loaded at our
 * requested base address: a function's (the callbacks, see efimain())
 * and an uninitialised (bss) global's taken as a value.  Found on QEMU
 * virt with AAVMF: &mapbuf came out as its link-time address while the
 * address of initialised data (a GUID, a string) was already right.
 * So nothing here hands the firmware the address of a bss global:
 * bootmapinit() passes a local instead.
 */
extern void *rebase(void *addr);

void
putc(int c)
{
	CHAR16 w[2];

	w[0] = c;
	w[1] = 0;
	eficall(ST->ConOut->OutputString, ST->ConOut, w);
}

int
getc(void)
{
	EFI_INPUT_KEY k;

	if(eficall(ST->ConIn->ReadKeyStroke, ST->ConIn, &k))
		return 0;
	return k.UnicodeChar;
}

void
usleep(int us)
{
	eficall(ST->BootServices->Stall, (UINTN)us);
}

/*
 * Claim [pa, pa+len) at the given EFI memory type via AllocateAddress (=2).
 * pa must be page-aligned: AllocateAddress rejects anything else (and
 * silently - the request is simply invalid, not "denied"). Returns non-zero
 * if it fails.
 */
static int
efiallocx(uvlong pa, uvlong len, int memtype)
{
	uvlong a;

	a = pa;
	return eficall(ST->BootServices->AllocatePages, (UINTN)2, (UINTN)memtype,
		(UINTN)((len + 4095) / 4096), &a) != 0;
}

/*
 * Claim [pa, pa+len) as loader code.  Firmware marks free memory no-execute,
 * which matters as the kernel is entered with the firmware's page tables
 * still active.  Used for the kernel's own memory range, which is executed.
 */
int
efialloc(uvlong pa, uvlong len)
{
	return efiallocx(pa, len, EfiLoaderCode);
}

/*
 * Release pages efialloc() claimed, eg after a later failure: without this
 * a retry (efimain's loop, on a bad read or a rejected kernel) finds the
 * same range already allocated and fails immediately instead of trying
 * again.
 */
void
efifree(uvlong pa, uvlong len)
{
	eficall(ST->BootServices->FreePages, (UINTN)pa, (UINTN)((len + 4095) / 4096));
}

/*
 * The BootInfo blob the kernel is entered with (Plan2001 Boot ABI v1, see
 * sys/include/bootinfo.h): one allocation, wherever the firmware's
 * allocator puts it (efimain()), holding the header (bi), the plan9.ini
 * text (confaddr, see sub.c), print()'s captured text (logbuf, sub.c) and
 * the final memory map (bootexit()).  Nothing is copied anywhere: the
 * kernel is handed its address as its ISA's entry ABI says (archjump()).
 */
enum {
	ConfCap = 8*1024,	/* plan9.ini text, including the NUL */
	LogCap = 4*1024,	/* print()'s captured text */
};
static BootInfo *bi;
static void bootinfohdr(void);
static uchar *blob;
static ulong blobsize, mmapcap;
static uchar *fdtsrc;		/* the firmware's device tree, if any (fdtfind()) */
static ulong fdtsize;
char *confaddr;

/*
 * Allocate len bytes wherever the firmware's allocator wants to put them
 * (AllocateAnyPages = 0), of the given EFI memory type. Returns the
 * physical address, or 0 on failure - firmware never hands out page 0.
 */
static uvlong
efiallocany(uvlong len, int memtype)
{
	uvlong a;

	a = 0;
	if(eficall(ST->BootServices->AllocatePages, (UINTN)0, (UINTN)memtype,
		(UINTN)((len + 4095) / 4096), &a) != 0)
		return 0;
	return a;
}

/*
 * Do not put the final UEFI memory map on the firmware-provided stack.
 * 96 KiB is a large automatic object and old firmware is not required to
 * provide enough spare stack for it. Allocate it before the final map is
 * requested, so its allocation is itself present in that map.
 */
enum {
	MapBufSize = 96*1024,
	/*
	 * Same grid as sys/src/9/port/bootfb.c's (Size/Gap/Margin), continued
	 * by it at slot LoaderMarks (its own constant, kept in sync by
	 * comment, same as this one): one shared top-left marker row
	 * spanning the whole boot, not two separate marker areas.
	 */
	MarkSize = 8,
	MarkGap = 2,
	MarkMargin = 2,
};
static uchar *mapbuf;

/*
 * One colour per stage: 1=EBS start, 2=EBS done, 3=jump.  The kernel's
 * own squares follow in the same row (sys/src/9/port/bootfb.c).
 */
static ulong markcolor[3] = {
	0x0000FF,	/* EBS start: blue */
	0x00FFFF,	/* EBS done: cyan */
	0xFFFF00,	/* jump: yellow */
};

int
bootmapinit(void)
{
	void *p;	/* a local: see rebase() above */

	p = nil;
	if(eficall(ST->BootServices->AllocatePool, (UINTN)EfiLoaderData,
		(UINTN)MapBufSize, &p) != 0 || p == nil)
		return -1;
	mapbuf = p;
	return 0;
}

/*
 * UEFI text output is gone after ExitBootServices. Leave visible progress
 * markers at the top-left of a 32-bit GOP framebuffer instead: one before
 * ExitBootServices, two after it returns, three before archjump - slots 0..2
 * of the shared marker row bootfb.c continues from slot 3 (its
 * LoaderMarks; see MarkSize's comment above).
 */
static void*
fbmarkaddr(int stage)
{
	UINT32 x;

	if(bi->fbbase == 0 || bi->fbdepth != 32 || bi->fbstride == 0
	|| bi->fbwidth == 0 || bi->fbheight < MarkMargin+MarkSize
	|| stage < 1)
		return nil;
	x = MarkMargin + (stage-1)*(MarkSize+MarkGap);
	if(x+MarkSize > bi->fbwidth)
		return nil;
	return (UINT32*)(uintptr)bi->fbbase + (uvlong)MarkMargin*bi->fbstride + x;
}

void
fbmark(int stage)
{
	volatile UINT32 *p;
	volatile UINT32 *row;
	ulong c;
	int x, y;

	p = fbmarkaddr(stage);
	if(p == nil)
		return;
	c = stage>=1 && stage<=3? markcolor[stage-1]: 0x00ff00ff;
	if(memcmp(bi->fbchan, "x8b8g8r8", 8) == 0)
		c = (c & 0x00FF00) | (c>>16 & 0xFF) | (c & 0xFF)<<16;
	for(y = 0; y < MarkSize; y++){
		row = p + (uvlong)y*bi->fbstride;
		for(x = 0; x < MarkSize; x++)
			row[x] = c;
	}
}

/*
 * Fetch the final memory map and leave boot services, retrying with a fresh
 * map if the firmware reports that it changed, as the specification requires.
 * No printing between GetMemoryMap and ExitBootServices: it changes the map.
 * Returns non-zero if boot services could not be left; they are still usable
 * then.  On success the map is recorded in the BootInfo, adjacent entries of
 * the same type and attributes merged, and no boot service may be used again.
 */
int
bootexit(void)
{
	UINTN mapsize, entsize;
	EFI_MEMORY_DESCRIPTOR *t;
	uchar *p;
	UINT32 entvers;
	BootMem *m, *mem;
	ulong n;
	int try;

	if(mapbuf == nil)
		return -3;
	for(try = 0; try < 4; try++){
		print("[P2 L25] GetMemoryMap + ExitBootServices attempt\n");
		mapsize = MapBufSize;
		entsize = sizeof(EFI_MEMORY_DESCRIPTOR);
		entvers = 1;
		if(eficall(ST->BootServices->GetMemoryMap, &mapsize, mapbuf, &MK, &entsize, &entvers))
			return -1;
		if(entsize < sizeof(EFI_MEMORY_DESCRIPTOR))
			return -1;
		if(eficall(ST->BootServices->ExitBootServices, IH, MK) == 0)
			goto Left;
		print("[P2 L25] ExitBootServices rejected stale map; retrying\n");
	}
	return -2;

Left:
	/*
	 * The map goes into the blob's own section, sized in efimain() for
	 * as many entries as mapbuf can hold at the smallest descriptor
	 * size, so it always fits: no fixed limit, no truncation.
	 */
	mem = (BootMem*)(blob + bi->mmapoff);
	n = 0;
	for(p = mapbuf; mapsize >= entsize; p += entsize, mapsize -= entsize){
		t = (EFI_MEMORY_DESCRIPTOR*)p;
		if(t->NumberOfPages == 0)
			continue;
		m = nil;
		if(n > 0){
			m = &mem[n-1];
			if(m->type != t->Type || m->attr != (UINT32)t->Attribute
			|| m->base + m->len != t->PhysicalStart)
				m = nil;
		}
		if(m != nil){
			m->len += t->NumberOfPages * 4096ULL;
			continue;
		}
		if(n >= mmapcap)
			break;	/* cannot happen, see above */
		m = &mem[n++];
		m->base = t->PhysicalStart;
		m->len = t->NumberOfPages * 4096ULL;
		m->type = t->Type;
		m->attr = (UINT32)t->Attribute;
	}
	bi->mmapcount = n;
	return 0;
}

/*
 * Wall clock time from the runtime service, callable before boot services
 * end.  civilsecs converts a Gregorian date to seconds since 1970-01-01,
 * using days_from_civil (Howard Hinnant, public domain) for the date part;
 * GetTime() returns local time, and per the UEFI spec Localtime = UTC -
 * TimeZone (minutes), so TimeZone is added to local to recover UTC.
 * TimeZone may be unspecified (already UTC, the common case for firmware
 * that keeps the RTC in UTC).
 */
static uvlong
civilsecs(EFI_TIME *t)
{
	int y, era, yoe, doy, doe;
	long days;

	y = t->Year - (t->Month <= 2);
	era = (y >= 0? y: y-399) / 400;
	yoe = y - era*400;
	doy = (153*(t->Month + (t->Month > 2? -3: 9)) + 2)/5 + t->Day-1;
	doe = yoe*365 + yoe/4 - yoe/100 + doy;
	days = era*146097L + doe - 719468;

	return (uvlong)days*24*3600 + t->Hour*3600 + t->Minute*60 + t->Second;
}

static void
timeconf(void)
{
	EFI_TIME t;

	if(eficall(ST->RuntimeServices->GetTime, &t, nil))
		return;
	bi->epoch = civilsecs(&t);
	if(t.TimeZone != EfiUnspecifiedTimeZone)
		bi->epoch += t.TimeZone*60;
}

/*
 * Entropy from the firmware's RNG, if it has one; otherwise rngseedlen stays
 * 0 and the kernel's own entropy collection is unaffected.
 */
static void
rngconf(void)
{
	static EFI_GUID EFI_RNG_PROTOCOL_GUID = {
		0x3152bca5, 0xeade, 0x433d,
		0x86, 0x2e, 0xc0, 0x1c,
		0xdc, 0x29, 0x1f, 0x44,
	};
	EFI_RNG_PROTOCOL *rng;
	UINTN len;

	rng = nil;
	if(eficall(ST->BootServices->LocateProtocol, &EFI_RNG_PROTOCOL_GUID, nil, &rng) || rng == nil)
		return;
	len = sizeof(bi->rngseed);
	if(eficall(rng->GetRNG, rng, nil, len, bi->rngseed) == 0)
		bi->rngseedlen = len;
}

static void
acpiconf(void)
{
	static EFI_GUID ACPI_20_TABLE_GUID = {
		0x8868e871, 0xe4f1, 0x11d3,
		0xbc, 0x22, 0x00, 0x80,
		0xc7, 0x3c, 0x88, 0x81,
	};
	static EFI_GUID ACPI_10_TABLE_GUID = {
		0xeb9d2d30, 0x2d88, 0x11d3,
		0x9a, 0x16, 0x00, 0x90,
		0x27, 0x3f, 0xc1, 0x4d,
	};
	EFI_CONFIGURATION_TABLE *t;
	uintptr pa;
	char buf[32], *s;
	int n;

	pa = 0;
	t = ST->ConfigurationTable;
	n = ST->NumberOfTableEntries;
	while(--n >= 0){
		if(memcmp(&t->VendorGuid, &ACPI_10_TABLE_GUID, sizeof(EFI_GUID)) == 0){
			if(pa == 0)
				pa = (uintptr)t->VendorTable;
		} else if(memcmp(&t->VendorGuid, &ACPI_20_TABLE_GUID, sizeof(EFI_GUID)) == 0)
			pa = (uintptr)t->VendorTable;
		t++;
	}

	bi->acpi = pa;
	if(pa){
		s = buf;
		memmove(s, "acpi=0x", 7), s += 7;
		s = hexfmt(s, 0, pa), *s++ = '\n';
		*s = '\0';
		print(buf);
	}
}

static int
topbit(ulong mask)
{
	int bit = 0;

	while(mask != 0){
		mask >>= 1;
		bit++;
	}
	return bit;
}

static int
lowbit(ulong mask)
{
	int bit = 0;

	while((mask & 1) == 0){
		mask >>= 1;
		bit++;
	}
	return bit;
}

/*
 * Decode a mode's pixel format into per-channel bitmasks.  Returns the
 * total bit depth (mr|mg|mb|mx's top bit), or 0 if the format is unusable
 * (eg PixelBltOnly, which has no linear framebuffer at all, or a
 * PixelBitMask mode whose masks are somehow all zero).
 */
static int
pixmasks(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info, ulong *mr, ulong *mg, ulong *mb, ulong *mx)
{
	switch(info->PixelFormat){
	default:
		return 0;	/* unsupported, eg PixelBltOnly */

	case PixelRedGreenBlueReserved8BitPerColor:
		*mr = 0x000000ff;
		*mg = 0x0000ff00;
		*mb = 0x00ff0000;
		*mx = 0xff000000;
		break;

	case PixelBlueGreenRedReserved8BitPerColor:
		*mb = 0x000000ff;
		*mg = 0x0000ff00;
		*mr = 0x00ff0000;
		*mx = 0xff000000;
		break;

	case PixelBitMask:
		*mr = info->PixelInformation.RedMask;
		*mg = info->PixelInformation.GreenMask;
		*mb = info->PixelInformation.BlueMask;
		*mx = info->PixelInformation.ReservedMask;
		break;
	}

	return topbit(*mr | *mg | *mb | *mx);
}

static void
screenconf(void)
{
	static EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
		0x9042a9de, 0x23dc, 0x4a38,
		0x96, 0xfb, 0x7a, 0xde,
		0xd0, 0x80, 0x51, 0x6a,
	};
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	EFI_HANDLE *Handles;
	UINTN Count;

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
	ulong mr, mg, mb, mx, mc;
	int i, bits, depth;
	char buf[96], *s;

	Count = 0;
	Handles = nil;
	print("[P2 L06] GOP: locating handles\n");
	if(eficall(ST->BootServices->LocateHandleBuffer,
		ByProtocol, &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, nil, &Count, &Handles)){
		print("[P2 L06] GOP: LocateHandleBuffer failed\n");
		return;
	}

	for(i=0; i<Count; i++){
		gop = nil;
		if(eficall(ST->BootServices->HandleProtocol,
			Handles[i], &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, &gop))
			continue;

		if(gop == nil)
			continue;
		if((info = gop->Mode->Info) == nil)
			continue;
		if((depth = pixmasks(info, &mr, &mg, &mb, &mx)) == 0)
			continue;

		/* make sure we have linear framebuffer */
		if(gop->Mode->FrameBufferBase == 0)
			continue;
		if(gop->Mode->FrameBufferSize == 0)
			continue;

		goto Found;
	}
	print("[P2 L06] GOP: no usable linear framebuffer\n");
	return;

Found:
	/*
	 * Deliberately not switching to the native/largest GOP mode here
	 * (tried in commit a93c397, reverted): on real hardware with a
	 * minimal GOP implementation (confirmed on a Dell Precision T5600,
	 * MaxMode=3 - a BMC-class video device), SetMode() can hang the
	 * firmware forever instead of returning an error. That happens
	 * before ExitBootServices, so there is no way to time out a
	 * synchronous DXE call that never returns - unlike a failed
	 * SetMode (which the UEFI spec guarantees leaves gop->Mode
	 * untouched), a hung one is unrecoverable. So: just use whatever
	 * mode is already active, exactly as before that commit.
	 */
	bi->fbbase = gop->Mode->FrameBufferBase;
	bi->fbsize = gop->Mode->FrameBufferSize;
	bi->fbwidth = info->HorizontalResolution;
	bi->fbheight = info->VerticalResolution;
	bi->fbstride = info->PixelsPerScanLine;
	bi->fbdepth = depth;

	/* channel descriptor, eg x8r8g8b8 */
	s = bi->fbchan;
	while(depth > 0 && s < bi->fbchan + sizeof(bi->fbchan) - 4){
		if(depth == topbit(mr)){
			mc = mr;
			*s++ = 'r';
		} else if(depth == topbit(mg)){
			mc = mg;
			*s++ = 'g';
		} else if(depth == topbit(mb)){
			mc = mb;
			*s++ = 'b';
		} else if(depth == topbit(mx)){
			mc = mx;
			*s++ = 'x';
		} else {
			break;
		}
		bits = depth - lowbit(mc);
		s = decfmt(s, 0, bits);
		depth -= bits;
	}
	*s = '\0';

	s = buf;
	memmove(s, "fb=", 3), s += 3;
	s = decfmt(s, 0, bi->fbwidth), *s++ = 'x';
	s = decfmt(s, 0, bi->fbheight), *s++ = 'x';
	s = decfmt(s, 0, bi->fbdepth), *s++ = ' ';
	memmove(s, "stride ", 7), s += 7;
	s = decfmt(s, 0, bi->fbstride), *s++ = ' ';
	memmove(s, bi->fbchan, strlen(bi->fbchan)), s += strlen(bi->fbchan);
	memmove(s, " 0x", 3), s += 3;
	s = hexfmt(s, 0, bi->fbbase), *s++ = '\n';
	*s = '\0';
	print(buf);
	print("[P2 L06] GOP: active mode recorded; no SetMode call\n");
}

void
eficonfig(void)
{
	print("[P2 L05] BootInfo: initialise\n");
	bootinfohdr();

	/* the memory map is added by bootexit(), right before leaving boot services */
	print("[P2 L05] ACPI: probe\n");
	acpiconf();
	screenconf();
	archconf(bi);
	print("[P2 L08] RTC: read\n");
	timeconf();
	print(bi->epoch != 0? "[P2 L08] RTC: ok\n": "[P2 L08] RTC: unavailable\n");
	print("[P2 L09] RNG: probe\n");
	rngconf();
	print(bi->rngseedlen != 0? "[P2 L09] RNG: ok\n": "[P2 L09] RNG: unavailable\n");
	print("[P2 L10] BootInfo: initial fields complete\n");
}

/*
 * Allocate the blob anywhere the ISA's kernel lets it be (archblobok(),
 * eg archx64.c: not where the kernel itself goes).  Firmware usually
 * allocates top-down, so the first try is taken; pages rejected are held
 * until a good range is found, so that the firmware does not offer them
 * again, and then given back.
 */
enum {
	BlobTries = 8,
};

static uvlong
bloballoc(uvlong len)
{
	uvlong a, bad[BlobTries];
	int i, n;

	a = 0;
	for(n = 0; n < BlobTries; n++){
		if((a = efiallocany(len, EfiLoaderData)) == 0 || archblobok(a, len))
			break;
		bad[n] = a;
		a = 0;
	}
	for(i = 0; i < n; i++)
		efifree(bad[i], len);
	return a;
}

/*
 * The firmware's flattened device tree, if it gives one (ARM64 and RISC-V
 * firmware usually does, PC firmware only ACPI): found in the configuration
 * table, checked for its magic and size, and later copied whole into the
 * blob's own section (efimain()), so that it is owned, relocated and kept
 * the same way as the rest of the blob.  A bad or oversized one is left
 * out, with a message: the hardware may still be described by ACPI.
 */
enum {
	FdtMagic = 0xd00dfeed,
	FdtMax = 2*1024*1024,	/* EDK2 pads QEMU virt's tree to 1 MB + 4 KB */
};

static ulong
be32(uchar *p)
{
	return (ulong)p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3];
}

static void
fdtfind(void)
{
	static EFI_GUID DEVICE_TREE_GUID = {
		0xb1b621d5, 0xf19c, 0x41a5,
		0x83, 0x0b, 0xd9, 0x15,
		0x2c, 0x69, 0xaa, 0xe0,
	};
	EFI_CONFIGURATION_TABLE *t;
	uchar *p;
	int n;

	t = ST->ConfigurationTable;
	n = ST->NumberOfTableEntries;
	for(; --n >= 0; t++){
		if(memcmp(&t->VendorGuid, &DEVICE_TREE_GUID, sizeof(EFI_GUID)) != 0)
			continue;
		p = t->VendorTable;
		if(p == nil || be32(p) != FdtMagic || be32(p+4) < 40 || be32(p+4) > FdtMax){
			print("[P2 L02] FDT: ignored, bad header or too large\n");
			return;
		}
		fdtsrc = p;
		fdtsize = be32(p+4);
		return;
	}
}

/*
 * The blob's fixed part: magic, version and where its sections are.
 * Called at the start (efimain()) and again from eficonfig() whenever
 * the configuration is cleared, as that zeroes the header.
 */
static void
bootinfohdr(void)
{
	memset(bi, 0, sizeof(*bi));
	bi->magic = BootInfoMagic;
	bi->version = BootInfoVersion;
	bi->headersize = sizeof(*bi);
	bi->totalsize = blobsize;
	bi->configoff = (sizeof(*bi) + 7) & ~7;
	bi->logoff = bi->configoff + ConfCap;
	bi->fdtoff = bi->logoff + LogCap;
	bi->fdtlen = fdtsize;
	bi->mmapoff = bi->fdtoff + ((fdtsize + 7) & ~7);
	bi->mmapentsize = sizeof(BootMem);
}

/*
 * The blob's last fields, set once nothing is printed or configured any
 * more: called from bootkern() (sub.c) after bootexit() succeeded, right
 * before archjump() hands the kernel the blob.
 */
void*
bootinfofinish(void)
{
	bi->configlen = conflen();
	bi->loglen = logused;
	return bi;
}


EFI_STATUS
efimain(EFI_HANDLE ih, EFI_SYSTEM_TABLE *st)
{
	char path[MAXPATH], *kern;
	void *f;
	uvlong la;

	IH = ih;
	ST = st;

	/*
	 * The BootInfo blob, before the very first print() so that its
	 * capture buffer (the blob's log section) misses nothing: wherever
	 * the firmware's allocator puts it (AllocateAnyPages), its size fixed
	 * here - the memory map section holds as many entries as mapbuf
	 * (bootmapinit) can at the smallest descriptor size.  T5600: a fixed
	 * low address can still be EfiBootServicesCode the firmware is
	 * running, which is why nothing about the handoff is at a fixed
	 * address any more (Plan2001 Boot ABI v1, sys/include/bootinfo.h).
	 */
	fdtfind();
	mmapcap = MapBufSize / sizeof(EFI_MEMORY_DESCRIPTOR);
	blobsize = PGROUND(((sizeof(BootInfo) + 7) & ~7) + ConfCap + LogCap + ((fdtsize + 7) & ~7) + mmapcap*sizeof(BootMem));
	if((la = bloballoc(blobsize)) == 0){
		print("[P2 L02] FATAL: cannot allocate the BootInfo blob\n");
		for(;;)
			;
	}
	blob = (uchar*)la;
	memset(blob, 0, blobsize);
	bi = (BootInfo*)blob;
	bootinfohdr();
	confaddr = (char*)blob + bi->configoff;
	logbuf = (char*)blob + bi->logoff;
	logcap = LogCap;
	if(fdtsize != 0)
		memmove(blob + bi->fdtoff, fdtsrc, fdtsize);

	print("[P2 L01] Plan2001 loader 2026-09-25 boot-abi-1\n");
	tracehex("[P2 L02] BootInfo blob at 0x", la);
	tracehex("[P2 L02] BootInfo blob bytes=0x", blobsize);
	if(fdtsize != 0)
		tracehex("[P2 L02] FDT copied into the blob, bytes=0x", fdtsize);

	print("[P2 L03] memory-map buffer: AllocatePool 96 KiB\n");
	if(bootmapinit() != 0){
		print("[P2 L03] FATAL: cannot allocate memory-map buffer\n");
		for(;;)
			;
	}
	print("[P2 L03] memory-map buffer: ready\n");

	/*
	 * Plan2001 is UEFI-only: boot exclusively from the Simple File System
	 * volume this image was itself loaded from (fsinit(), which also
	 * falls back to scanning other SFS volumes for plan9.ini if that
	 * specific one doesn't have it - see fs.c). No PXE or ISO9660
	 * fallback: on real firmware, isoinit()'s BlockIO/PVD scan alone
	 * cost tens of seconds to a minute of boot time (confirmed on a Dell
	 * Precision T5600), and neither ever applies to how Plan2001 is
	 * actually deployed (a USB/ESP volume). A missing boot filesystem is
	 * fatal and reported plainly, not silently chained to another probe.
	 */
	f = nil;
	print("[P2 L04] boot filesystem: open\n");
	if(fsinit(&f) != 0){
		print("[P2 L04] FATAL: boot filesystem unavailable\n");
		for(;;)
			;
	}
	print("[P2 L04] boot filesystem: ready\n");

	print("[P2 L11] rebase boot-device callbacks\n");
	open = rebase(open);
	read = rebase(read);
	close = rebase(close);
	if(stop) stop = rebase(stop);

	for(;;){
		print("[P2 L12] configure kernel path\n");
		kern = configure(f, path);
		print("[P2 L13] open kernel\n");
		f = open(kern);
		if(f == nil){
			print("not found\n");
			continue;
		}
		print("[P2 L13] kernel opened\n");
		print(bootkern(f));
		print("\n");
		close(f);
		f = nil;
	}
}
