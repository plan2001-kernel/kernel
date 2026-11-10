#ifndef	PLAN2001_ABI_SYS_H
#define	PLAN2001_ABI_SYS_H

#define	SYSR1		0 /* reserved */
#define	_ERRSTR		1 /* reserved */
#define	BIND		2 /* reserved */
#define	CHDIR		3 /* reserved */
#define	CLOSE		4 /* reserved */
#define	DUP		5 /* reserved */
#define	ALARM		6
#define	EXEC		7 /* reserved */
#define	EXITS		8
#define	_FSESSION	9 /* reserved */
#define	FAUTH		10 /* reserved */
#define	_FSTAT		11 /* reserved */
#define	SEGBRK		12
#define	_MOUNT		13 /* reserved */
#define	OPEN		14 /* reserved */
#define	_READ		15 /* reserved */
#define	OSEEK		16 /* reserved */
#define	SLEEP		17
#define	_STAT		18 /* reserved */
/* RFORK(19) is RESERVED.  RFORKEX(143) is the one canonical extended fork
 * boundary; an ordinary fork is the libc rfork() wrapper with an empty
 * Handle plan. */
#define	RFORK		19 /* reserved */
#define	_WRITE		20 /* reserved */
#define	PIPE		21 /* reserved */
#define	CREATE		22 /* reserved */
#define	FD2PATH		23 /* reserved */
#define	BRK_		24
#define	REMOVE		25 /* reserved */
#define	_WSTAT		26 /* reserved */
#define	_FWSTAT		27 /* reserved */
/* NOTIFY(28) and NOTED(29) are the direct historical note calls.  Both use
 * the shared kernel setnotify()/notecont() core; NOTED returns through the
 * special note-return path.  The ObjOpNotifyRegister/ObjOpNoted variants are
 * retired. */
#define	NOTIFY		28
#define	NOTED		29
#define	SEGATTACH	30
#define	SEGDETACH	31
#define	SEGFREE		32
#define	SEGFLUSH	33
#define	UNMOUNT		35 /* reserved */
/* _WAIT(36) is RESERVED: if a name is ever wanted it may not reuse this
 * number; child wait is the direct typed PROCWAIT syscall (142). */
#define	_WAIT		36 /* reserved */
#define	SEEK		39 /* reserved */
#define	FVERSION	40 /* reserved */
#define	ERRSTR		41
#define	STAT		42 /* reserved */
#define	FSTAT		43 /* reserved */
#define	WSTAT		44 /* reserved */
#define	FWSTAT		45 /* reserved */
#define	MOUNT		46 /* reserved */
/* AWAIT(47) is RESERVED: libc await()/wait() use the direct PROCWAIT call. */
#define	AWAIT		47 /* reserved */
#define	PREAD		50 /* reserved */
#define	PWRITE		51 /* reserved */
#define _NSEC		53
/* OVOLOPEN(54) is RESERVED: privileged bootstrap/administrative volume
 * activation is SYSCTL(144), operation SysctlVolumeOpen. */
#define	OVOLOPEN	54 /* reserved */
#define	OSPACEOPEN	55 /* reserved */
#define	OSPACEGET	56 /* reserved */
#define	OSPACEPUT	57 /* reserved */
#define	OSPACECOMMIT	58 /* reserved */
#define	OSPACECLOSE	59 /* reserved */
#define	OEXECUTE	60
#define	OSPACECREATE	61 /* reserved */
#define	IOWRITE		62 /* reserved */
#define	USBOPEN		63 /* reserved */
#define	USBLIST		64 /* reserved */
#define	USBEPOPEN	65 /* reserved */
#define	USBREAD		66 /* reserved */
#define	USBWRITE	67 /* reserved */
#define	USBIOCTL	68 /* reserved */
#define	USBCLOSE	69 /* reserved */
#define	USBEPNAME	70 /* reserved */
/* 71-85 RESERVED: former Pipe/KBD/UART compatibility family.  The numbers
 * stay allocated forever but have no dispatch entry; use the OBJ* five-call
 * ABI (Pipe via CREATE/INVOKE/CLOSE, UART/KBD via OPEN/INVOKE/CLOSE). */
#define	PIPENEW		71 /* reserved */
#define	PIPEREAD	72 /* reserved */
#define	PIPEWRITE	73 /* reserved */
#define	PIPECLOSE	74 /* reserved */
#define	KBDOPEN		75 /* reserved */
#define	KBDREAD		76 /* reserved */
#define	KBDCLOSE	77 /* reserved */
#define	KBDLEDS		78 /* reserved */
#define	UARTCOUNT	79 /* reserved */
#define	UARTOPEN	80 /* reserved */
#define	UARTREAD	81 /* reserved */
#define	UARTWRITE	82 /* reserved */
#define	UARTIOCTL	83 /* reserved */
#define	UARTSTATUS	84 /* reserved */
#define	UARTCLOSE	85 /* reserved */
#define	RTCGET		86 /* reserved */
#define	RTCSET		87 /* reserved */
/* 88-91 RESERVED: process environment operations are SYSCTL(144)
 * operations SysctlEnvGet/Set/Delete/List. */
#define	ENVGET		88 /* reserved */
#define	ENVSET		89 /* reserved */
#define	ENVDELETE	90 /* reserved */
#define	ENVLIST		91 /* reserved */
/* N13 experimental object views.  PIPEINHERIT=100 is RESERVED: the number
 * stays allocated forever but has no dispatch entry.  Handle inheritance is
 * declared atomically by RFORKEX(143) or SPAWN options, never armed ahead
 * of process creation. */
#define OBIND           96 /* reserved */
/* 97-99 reserved: retired object binding calls. */
#define PIPEINHERIT     100 /* reserved */
/* 104 reserved: retired extended object binding call. */
#define OQUERY          106 /* reserved */
/* SYSSNAPSHOT(107) is RESERVED: system status is SYSCTL(144), operation
 * SysctlSnapshot. */
#define SYSSNAPSHOT     107 /* reserved */
/* 108-113 RESERVED: former TCP6* compatibility family.  The numbers stay
 * allocated forever but have no dispatch entry; use the OBJ* five-call ABI. */
#define TCP6LISTEN      108 /* reserved */
#define TCP6CONNECT     109 /* reserved */
#define TCP6ACCEPT      110 /* reserved */
#define TCP6READ        111 /* reserved */
#define TCP6WRITE       112 /* reserved */
#define TCP6CLOSE       113 /* reserved */
/* OSPAWN(92) is RESERVED: spawning is the direct SPAWN(146) call taking an
 * authorized Space Handle and OID, consistent with OEXECUTE(60).
 * OTERMINATE(93) is the direct historical call again and takes one positive
 * direct-child PID. */
#define	OSPAWN		92 /* reserved */
#define	OTERMINATE	93
/*
 * SPAWN's fifth argument (also the historical OSPAWN options layout).  cpu
 * is an exact online logical kernel CPU (0..conf.nmach-1), or ~0ULL for
 * unpinned.  membudget is bytes and 0 inherits the parent's budget; flags=0
 * keeps the original 24-byte options layout; flags=1 appends Oinherit.
 */
struct Ospawn {
	uvlong	cpu;
	uvlong	membudget;
	uvlong	flags;
};

/* 116-120 RESERVED: former NET6* UDP6 compatibility family.  The numbers
 * stay allocated forever but have no dispatch entry.  There is no kernel
 * UDP6/TCP6 object interface; network protocols are userspace over raw
 * device frames/ObjectFIFO. */
#define	NET6ADDR	116 /* reserved */
#define	NET6OPEN	117 /* reserved */
#define	NET6RECVFROM	118 /* reserved */
#define	NET6SENDTO	119 /* reserved */
#define	NET6CLOSE	120 /* reserved */
/* 121-135 RESERVED: former VOBJ* compatibility family.  The numbers stay
 * allocated forever but have no dispatch entry; the OBJ* five-call ABI below
 * is the only object kernel boundary.  Python keeps its historical method
 * names as userland adapters over OBJ* (they add no entry point). */
#define	VOBJCREATE	121 /* reserved */
#define	VOBJREAD	122 /* reserved */
#define	VOBJINFO	123 /* reserved */
#define	VOBJCLOSE	124 /* reserved */
#define	VOBJFIFOCREATE	125 /* reserved */
#define	VOBJFIFOSUB	126 /* reserved */
#define	VOBJFIFOSEND	127 /* reserved */
#define	VOBJFIFORECV	128 /* reserved */
#define	VOBJFIFOSTAT	129 /* reserved */
#define	VOBJGRANT	130 /* reserved */
#define	VOBJREVOKE	131 /* reserved */
#define	VOBJIO		132 /* reserved */
#define	VOBJLIST	133 /* reserved */
#define	VOBJOPEN	134 /* reserved */
#define	VOBJACCEPT	135 /* reserved */
/* Universal five-operation object ABI (runtime/abi/obj.h).
 * CREATE, OPEN, CLOSE, BIND and INVOKE are the only object kernel boundary;
 * read, write, publish, subscribe, receive and accept are userland adapters
 * over OBJINVOKE.  The retired VOBJ and TCP6 numbers above are reserved and
 * dispatch to nothing. */
#define	OBJCREATE	136
#define	OBJOPEN		137
#define	OBJCLOSE	138
#define	OBJBIND		139
#define	OBJINVOKE	140
/* OBJLIST(141) is RESERVED: own-process Handle-table introspection is
 * SYSCTL(144), operation SysctlHandleList. */
#define	OBJLIST		141 /* reserved */
/* Direct typed child wait.  The single argument points to one ProcWait
 * record; the kernel fills pid, the three times and the bounded status text
 * exactly as the historical _WAIT did.  No ProcessSelf Handle is created. */
#define	PROCWAIT	142
/* SYSCTL(144) and IOCTL(145) separate configuration/introspection and
 * device control from object data operations.  See runtime/abi/sysctl.h and
 * runtime/abi/ioctl.h for their typed request records. */
#define	SYSCTL		144
#define	IOCTL		145
/* Direct spawn: (int Space handle, uvlong OID, char **argv, int argc,
 * struct Ospawn *options).  The Handle must carry Vexecute and the same
 * Oexecute/role checks as OEXECUTE apply.  Returns the child PID or -1. */
#define	SPAWN		146
/* Region-scoped publication wait/wake.
 *
 * The publication word is an aligned 64-bit little-endian value.  A writer
 * constructs every byte of the payload first and publishes the word with
 * release ordering; readers acquire the word before reading the payload.  The
 * syscall compares and waits only on equality, so no ordering or wrap-around
 * assumption beyond exact equality is required.  A payload is named by a
 * region-relative offset or delta, never by a cross-process virtual address.
 * The writer must not overwrite bytes belonging to an immutable snapshot that
 * is still published; use separate buffers/versions or publish a later word
 * before reusing storage.  Readers must not read payload bytes unless the
 * acquired word selects that complete snapshot.
 *
 * The first argument is the process-local base address returned by
 * ObjOpRegionMap (not a raw credential and not any interior address).  The
 * second argument is the region-relative byte offset of the word and must be
 * 8-byte aligned with at least 8 bytes inside the region.  Authority is the
 * caller's installed mapping, so a read-only observer may WAIT and the
 * mapping remains usable after its Handle is closed or the region is revoked.
 *
 * REGIONWAIT(base, offset, expected, timeout) applies this order:
 *   1. Validate the installed mapping and aligned/in-bounds word.
 *   2. Compare the word: if it differs from expected, return 0 immediately,
 *      whether or not a writer is present.
 *   3. If it still matches and no RW mapping is active, return 3 immediately,
 *      even when timeout < 0.  This covers a region that never had a writer, a
 *      writer that unmapped, and a writer process that exited or was killed
 *      and whose mappings were released.  A process retained in the kernel's
 *      broken/debug state still owns its mapping and remains the active
 *      writer until that mapping is released.
 *   4. If a writer is active and the word still matches, timeout == 0 returns
 *      2; otherwise atomically enroll and sleep with the timeout/interrupt
 *      rules below.
 *   5. If the active writer mapping is released while enrolled, the waiter is
 *      notified and returns 3.  A replacement writer mapping later cannot
 *      erase that notification.  If a normal WAKE wins the race, returning 1
 *      is permitted.
 * Status 3 is "no active writer at check time / the previous writer mapping
 * was released", not permanent region destruction: a new writer may map later
 * unless Phase 2 revocation denies it.  The caller decides whether to retry,
 * request a new grant or treat the stream as ended.  timeout < 0 blocks until
 * notified, timeout == 0 only checks, and timeout > 0 is a bounded millisecond
 * wait.  Other returns are:
 *   0  value already differed from expected; nothing was slept on
 *   1  woken (possibly spuriously); the caller must recheck the word
 *   2  timeout or a non-blocking check with the value still equal
 * An invalid or unmapped mapping, a bad offset or an interruption raises the
 * usual error (an installed mapping remains usable after revocation, as noted
 * above).  Returned notification never proves a particular value.  Check of
 * writer presence, word comparison and enrollment are one linearizable step
 * with writer-slot acquisition/release; the kernel does not infer process
 * ancestry or a permanent owner.
 *
 * REGIONWAKE(base, offset, count) requires the current writable mapping (the
 * single writer) and wakes at most count waiters registered for the same
 * region and region-relative offset.  count must be greater than zero; a
 * value greater than the number of waiters wakes those present.  It returns
 * the number actually woken.  Wait keys use the kernel region identity plus
 * offset, never a virtual address.  A wake carries no payload. */
#define	REGIONWAIT	147
#define	REGIONWAKE	148
/* flags=1 opts into this bounded extension immediately after Ospawn.
 * view: 0 EMPTY, 1 COPY listed roles, 2 SHARE (list must name whole view).
 * Pipe rights: 1 read, 2 write, 3 both. No implicit endpoint selection.
 */
struct Oinherit {
 uvlong view, nroles;
 char roles[32][32];
 uvlong npipes;
 int pipes[16][2];
 uvlong stdoutpipe; /* 0 or one of the selected writable Pipe handles */
 uvlong nenv;
 struct { char name[32]; char value[128]; } env[8];
};

/* flags=2: explicit attenuation of process-local object handles into a child.
 * Each entry duplicates the named parent handle into the child with the
 * requested subset of the parent's rights (0 means the parent's rights).
 * This is explicit delegation, never implicit inheritance.  Child Handle
 * numbers are allocated in list order to the lowest free child slots; they
 * are not the parent's numbers. */
struct Oinheritv {
	uvlong nhandles;
	struct { uvlong handle; uvlong rights; } h[8];
};

/* RFORKEX(143): the one canonical direct fork boundary, with explicit object
 * Handle delegation.  It carries ordinary RFORK flags plus a bounded,
 * versioned options record naming the parent Handles that cross into the
 * child and their exact rights attenuation.  An ordinary fork is the libc
 * rfork() wrapper with an empty plan; there is no pending one-shot state and
 * OBJINVOKE cannot arm a later fork.
 *
 * All entries are validated before the child is published: the source Handle
 * must be live, carry Vdelegate, not be a subscription, and the requested
 * rights must be a subset of the source rights (0 copies them).  The child
 * starts with an empty Handle table; each entry is installed at dstslot
 * (1..rforkex_maxslot()) or, when dstslot is 0, at the lowest free slot in
 * list order.  dstslot conflicts are rejected.  The assigned child Handle for
 * entry i is written to childhandles[i] when childhandles is non-NULL.  On any
 * failure the parent's Handle state and reference counts are unchanged and no
 * child is left published.  Kernospawn's Oinherit/Oinheritv records are
 * translated into this same plan.
 *
 * flags are ordinary RFORK flags.  RFPROC is required when count is nonzero.
 * The options pointer may be NULL, meaning an empty plan (count 0).  This
 * call has no OContext, environment, Pipe-list or stdout options; those
 * remain explicit SPAWN-only fields and are not implicit inheritance. */
#define RFORKEX			143
enum {
	RFORKEX_VERSION		= 1,
	RFORKEX_MAXHANDLES	= 16,
};

struct Rforkex {
	unsigned int	version;	/* RFORKEX_VERSION */
	unsigned int	count;		/* <= RFORKEX_MAXHANDLES */
	struct {
		unsigned long long	handle;	/* parent process-local Handle */
		unsigned long long	rights;	/* 0 = source rights */
		unsigned long long	dstslot;/* 0 = lowest free; else 1..maxslot */
	} h[RFORKEX_MAXHANDLES];
};

/* Generated user stub for the direct call; returns the child PID or -1. */
long rforkex(unsigned long flags, struct Rforkex *opts,
	unsigned long long *childhandles);

#endif /* PLAN2001_ABI_SYS_H */
