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
/* RFORK(19) is the direct historical process/group call. */
#define	RFORK		19
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
#define	RENDEZVOUS	34
#define	UNMOUNT		35 /* reserved */
/* _WAIT(36) is RESERVED: if a name is ever wanted it may not reuse this
 * number; child wait is the direct typed OBJWAIT syscall (142). */
#define	_WAIT		36 /* reserved */
#define	SEMACQUIRE	37
#define	SEMRELEASE	38
#define	SEEK		39 /* reserved */
#define	FVERSION	40 /* reserved */
#define	ERRSTR		41
#define	STAT		42 /* reserved */
#define	FSTAT		43 /* reserved */
#define	WSTAT		44 /* reserved */
#define	FWSTAT		45 /* reserved */
#define	MOUNT		46 /* reserved */
/* AWAIT(47) is RESERVED: libc await()/wait() use the direct OBJWAIT call. */
#define	AWAIT		47 /* reserved */
#define	PREAD		50 /* reserved */
#define	PWRITE		51 /* reserved */
#define	TSEMACQUIRE	52
#define _NSEC		53
#define	OVOLOPEN	54
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
#define	ENVGET		88
#define	ENVSET		89
#define	ENVDELETE	90
#define	ENVLIST		91
/* N13 experimental object views.  PIPEINHERIT=100 is RESERVED: the number
 * stays allocated forever but has no dispatch entry; use
 * OBJINVOKE(ObjOpPipeInherit). */
#define OBIND           96 /* reserved */
/* 97-99 reserved: retired object binding calls. */
#define PIPEINHERIT     100 /* reserved */
/* 104 reserved: retired extended object binding call. */
#define OQUERY          106 /* reserved */
#define SYSSNAPSHOT     107
/* 108-113 RESERVED: former TCP6* compatibility family.  The numbers stay
 * allocated forever but have no dispatch entry; use the OBJ* five-call ABI. */
#define TCP6LISTEN      108 /* reserved */
#define TCP6CONNECT     109 /* reserved */
#define TCP6ACCEPT      110 /* reserved */
#define TCP6READ        111 /* reserved */
#define TCP6WRITE       112 /* reserved */
#define TCP6CLOSE       113 /* reserved */
/* OSPAWN(92) is RESERVED: use OBJINVOKE(ObjOpOspawn) on a VkindSpace Handle
 * carrying Vexecute.  OTERMINATE(93) is the direct historical call again and
 * takes one positive direct-child PID. */
#define	OSPAWN		92 /* reserved */
#define	OTERMINATE	93
/*
 * OSPAWN's fifth argument.  cpu is an exact online logical kernel CPU
 * (0..conf.nmach-1), or ~0ULL for unpinned.  membudget is bytes and
 * 0 inherits the parent's budget; flags=0 keeps the original 24-byte
 * options layout; flags=1 appends Oinherit.
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
/* Direct bounded introspection of the caller's own object handle table.
 * Arguments are (VobjListRec *records, unsigned long count); the result is
 * the total number of live handles, or the copied count when records is
 * non-NULL.  It never names another process and transfers no authority. */
#define	OBJLIST		141
/* Direct typed child wait.  The single argument points to one ObjWait
 * record; the kernel fills pid, the three times and the bounded status text
 * exactly as the historical _WAIT did.  No ProcessSelf Handle is created. */
#define	OBJWAIT		142
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
 * This is explicit delegation, never implicit inheritance. */
struct Oinheritv {
 uvlong nhandles;
 struct { uvlong handle; uvlong rights; } h[8];
};

#endif /* PLAN2001_ABI_SYS_H */
