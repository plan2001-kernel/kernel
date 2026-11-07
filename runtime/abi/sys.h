#ifndef	PLAN2001_ABI_SYS_H
#define	PLAN2001_ABI_SYS_H

#define	SYSR1		0
#define	_ERRSTR		1
#define	BIND		2
#define	CHDIR		3
#define	CLOSE		4
#define	DUP		5
#define	ALARM		6
#define	EXEC		7
#define	EXITS		8
#define	_FSESSION	9
#define	FAUTH		10
#define	_FSTAT		11
#define	SEGBRK		12
#define	_MOUNT		13
#define	OPEN		14
#define	_READ		15
#define	OSEEK		16
#define	SLEEP		17
#define	_STAT		18
/* RFORK(19) is the direct historical process/group call. */
#define	RFORK		19
#define	_WRITE		20
#define	PIPE		21
#define	CREATE		22
#define	FD2PATH		23
#define	BRK_		24
#define	REMOVE		25
#define	_WSTAT		26
#define	_FWSTAT		27
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
#define	UNMOUNT		35
/* _WAIT(36) is RESERVED: use OBJINVOKE(ObjOpWait). */
#define	_WAIT		36
#define	SEMACQUIRE	37
#define	SEMRELEASE	38
#define	SEEK		39
#define	FVERSION	40
#define	ERRSTR		41
#define	STAT		42
#define	FSTAT		43
#define	WSTAT		44
#define	FWSTAT		45
#define	MOUNT		46
/* AWAIT(47) is RESERVED: libc await()/wait() use ObjOpWait. */
#define	AWAIT		47
#define	PREAD		50
#define	PWRITE		51
#define	TSEMACQUIRE	52
#define _NSEC		53
#define	OVOLOPEN	54
#define	OSPACEOPEN	55
#define	OSPACEGET	56
#define	OSPACEPUT	57
#define	OSPACECOMMIT	58
#define	OSPACECLOSE	59
#define	OEXECUTE	60
#define	OSPACECREATE	61
#define	IOWRITE		62
#define	USBOPEN		63
#define	USBLIST		64
#define	USBEPOPEN	65
#define	USBREAD		66
#define	USBWRITE	67
#define	USBIOCTL	68
#define	USBCLOSE	69
#define	USBEPNAME	70
/* 71-85 RESERVED: former Pipe/KBD/UART compatibility family.  The numbers
 * stay allocated forever but have no dispatch entry; use the OBJ* five-call
 * ABI (Pipe via CREATE/INVOKE/CLOSE, UART/KBD via OPEN/INVOKE/CLOSE). */
#define	PIPENEW		71
#define	PIPEREAD	72
#define	PIPEWRITE	73
#define	PIPECLOSE	74
#define	KBDOPEN		75
#define	KBDREAD		76
#define	KBDCLOSE	77
#define	KBDLEDS		78
#define	UARTCOUNT	79
#define	UARTOPEN	80
#define	UARTREAD	81
#define	UARTWRITE	82
#define	UARTIOCTL	83
#define	UARTSTATUS	84
#define	UARTCLOSE	85
#define	RTCGET		86
#define	RTCSET		87
#define	ENVGET		88
#define	ENVSET		89
#define	ENVDELETE	90
#define	ENVLIST		91
/* N13 experimental object views.  PIPEINHERIT=100 is RESERVED: the number
 * stays allocated forever but has no dispatch entry; use
 * OBJINVOKE(ObjOpPipeInherit). */
#define OBIND           96
#define OLOOKUP         97
#define OUNBIND         98
#define OREVOKE         99
#define PIPEINHERIT     100
#define ONSVIEW         101
#define ONMOUNT         102
#define ONUNMOUNT       103
#define OBINDX          104
#define ONSVISIBLE      105
#define OQUERY          106
#define SYSSNAPSHOT     107
/* 108-113 RESERVED: former TCP6* compatibility family.  The numbers stay
 * allocated forever but have no dispatch entry; use the OBJ* five-call ABI. */
#define TCP6LISTEN      108
#define TCP6CONNECT     109
#define TCP6ACCEPT      110
#define TCP6READ        111
#define TCP6WRITE       112
#define TCP6CLOSE       113
/* OSPAWN(92) is RESERVED: use OBJINVOKE(ObjOpOspawn) on a VkindSpace Handle
 * carrying Vexecute.  OTERMINATE(93) is the direct historical call again and
 * takes one positive direct-child PID. */
#define	OSPAWN		92
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
 * stay allocated forever but have no dispatch entry; use the OBJ* five-call
 * ABI.  The interface address lookup is OBJINVOKE(ObjOpNet6Addr),
 * endpoints are OBJOPEN(ObjOpenUdp6), and data is
 * OBJINVOKE(SendTo/RecvFrom)/OBJCLOSE. */
#define	NET6ADDR	116
#define	NET6OPEN	117
#define	NET6RECVFROM	118
#define	NET6SENDTO	119
#define	NET6CLOSE	120

/* 121-135 RESERVED: former VOBJ* compatibility family.  The numbers stay
 * allocated forever but have no dispatch entry; the OBJ* five-call ABI below
 * is the only object kernel boundary.  Python keeps its historical method
 * names as userland adapters over OBJ* (they add no entry point). */
#define	VOBJCREATE	121
#define	VOBJREAD	122
#define	VOBJINFO	123
#define	VOBJCLOSE	124
#define	VOBJFIFOCREATE	125
#define	VOBJFIFOSUB	126
#define	VOBJFIFOSEND	127
#define	VOBJFIFORECV	128
#define	VOBJFIFOSTAT	129
#define	VOBJGRANT	130
#define	VOBJREVOKE	131
#define	VOBJIO		132
#define	VOBJLIST	133
#define	VOBJOPEN	134
#define	VOBJACCEPT	135

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

/* flags=1 opts into this bounded extension immediately after Ospawn.
 * view: 0 EMPTY, 1 COPY listed roles, 2 SHARE (list must name whole view).
 * Pipe rights: 1 read, 2 write, 3 both. No implicit endpoint selection.
 * network: non-transferable UDP6 permit; revoked by exec/exit.
 */
struct Oinherit {
 uvlong view, nroles;
 char roles[32][32];
 uvlong npipes;
 int pipes[16][2];
 uvlong network;
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
