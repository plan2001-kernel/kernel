/*
 * Plan2001-O architecture-shared early boot.
 *
 * The BootInfo blob (include/bootinfo.h, Plan2001 Boot ABI v1) is the same
 * object on every ISA: how its address arrives differs, but its header,
 * sections and memory map do not.  This module is the ISA-independent part
 * of taking it: checking a blob is complete and self-consistent, turning
 * its UEFI memory types into the kernel's RAM/ACPI/reserved classes, and
 * announcing the handoff on a caller-supplied character sink.
 *
 * It deliberately uses no kernel services: an ISA's entry code can call it
 * before its own console, heap or scheduler exists.  The caller has already
 * mapped the blob at "blob" and has copied at least the header fields it
 * can see into b.  This file is compiled for the AMD64 kernel and for the
 * experimental ARM64 boot kernel; it is not a new filesystem or device
 * interface.
 */
#ifndef PLAN2001_OPLANBOOT_H
#define PLAN2001_OPLANBOOT_H

#include <bootinfo.h>

/* the smallest header every v1 loader writes: through arch, as bootinfo.h says */
#define OplanBootMinHeader	((u64int)&((BootInfo*)0)->fdtoff)

enum {
	OplanBootPageSize = 4096,
	OplanBootErrLen = 128,

	/* the same three classes kernel/amd64/fns.h has used for BootInfo */
	OplanBootRAM = 0,
	OplanBootACPI = 1,
	OplanBootReserved = 2,
};

/*
 * 1 if b is a complete, self-consistent v1 blob for ISA arch.  blob is the
 * kernel's mapping of the whole blob; blob is page-aligned.  On 0, err (if
 * non-nil) names the first failed check.  b may be a copy of the loader's
 * header, but it must carry at least OplanBootMinHeader bytes of it.
 */
int	oplanbootvalidate(BootInfo *b, u64int blob, u32int arch,
	    char *err, int errlen);

/* UEFI memory type -> OplanBootRAM/ACPI/Reserved, or -1 if unknown */
int	oplanbootmemclass(u32int type);

/* bytes of memory the map calls RAM (including loader/boot memory) */
u64int	oplanbootram(BootInfo *b, uchar *blob);

/*
 * Print the ISA-independent part of the handoff through out(arg, text).
 * Text strings are NUL-terminated and end in '\n'; out owns presentation
 * (UEFI console, PL011, kernel print).  b may be a copied header while blob
 * is the mapping of the whole blob (AMD64 keeps them separately).
 * Returns bytes of RAM for the caller's own arch-specific line.
 */
u64int	oplanbootannounce(void (*out)(void*, const char*), void *arg,
	    BootInfo *b, uchar *blob);

#endif
