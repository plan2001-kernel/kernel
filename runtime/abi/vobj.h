/*
 * Volatile object and capability-system userspace ABI.
 *
 * Fixed-width, little-endian layout for amd64.  A volatile identity is
 * (incarnation, space, oid): the incarnation is per-boot, the space is a
 * caller-chosen scope tag (0 is the runtime/default scope), and the oid is
 * allocated monotonically within that boot.  An identity never implies GE/O
 * persistence and is never reused while the boot incarnation is alive.
 *
 * Handles are process-local, generation-tagged references with rights.  A
 * handle is not an OID, a kernel pointer, or a capability by itself; every
 * operation validates the handle and its rights.
 */
#ifndef PLAN2001_ABI_VOBJ_H
#define PLAN2001_ABI_VOBJ_H

#define VOBJ_ABI_VERSION 3

/* Independent, attenuable rights on an object or subscription handle. */
enum {
	Vread		= 1<<0,	/* read immutable object bytes */
	Vsend		= 1<<1,	/* send one provider message (USB/Ethernet) */
	Vrecv		= 1<<2,	/* receive one provider completion/channel */
	Vmeta		= 1<<3,	/* query identity/metrics */
	Vclose		= 1<<4,	/* reserved; close is always allowed */
	Vsubscribe	= 1<<5,	/* acquire a device receive channel */
	Vcontrol	= 1<<6,	/* provider control or capability revocation */
	Vdelegate	= 1<<7,	/* create attenuated handles */
	Vwrite		= 1<<8,	/* write stream/GE bytes where the provider allows */
	Vaccept		= 1<<9,	/* reserved historical connection right */
	Vconnect	= 1<<10,	/* reserved historical connection right */
	Vcreate		= 1<<11,	/* create persistent objects in an O Space */
	Vcommit		= 1<<12,	/* commit a persistent O Space transaction */
	Vexecute	= 1<<13,	/* execute/load a GE/O object into a child */
	Vobserve	= 1<<14,	/* read a process completion record and wait */
	Vall		= 0xffff,
};

enum {
	VkindBytes	= 0,	/* immutable byte content, including empty */
	VkindGe		= 3,	/* persistent GE/O object capability (1,2 reserved) */
	VkindSpace	= 4,	/* authorized open O Space capability */
	VkindUart	= 7,	/* native UART port, opened through the device catalog */
	VkindKbd	= 8,	/* exclusive PS/2 scan-code source */
	VkindUsbEp	= 10,	/* catalog-opened USB endpoint reference */
	VkindConsole	= 11,	/* native process output stream */
	VkindDevices = 13,	/* common device discovery/open authority */
	VkindDevice = 14,
	VkindRegion	= 17,	/* volatile capability-mapped memory region */
	VkindProcess	= 18,	/* volatile process observation/control capability */
	VkindObjectSpace = 19,	/* independent named capability graph node */
	/* Kinds 1 (message FIFO), 2 (TCP), 5 (UDP6), 6 (Pipe), 9 (USB service)
	 * and 12 (ProcessSelf) are reserved historical identifiers and are
	 * never installed. */
};

/* VOBJOPEN providers.  These select a concrete provider, not a data schema.
 * Providers 1, 2 and 6 are reserved historical TCP6/UDP6 identifiers. */
enum {
	VopenGe		= 3,
	VopenSpace	= 4,	/* authorized name -> open O Space */
	VopenGeSpace	= 5,	/* authorized open O Space + OID -> GE object */
};

/* Generic object I/O operations for VOBJIO. */
enum {
	VioRead		= 1,
	VioWrite	= 2,
};

enum {
	VflagReadonly	= 1,	/* published payload is an immutable snapshot */
};

/* A scope-qualified volatile object identity. */
typedef struct VobjId {
	unsigned long long	incarnation;
	unsigned long long	space;
	unsigned long long	oid;
} VobjId;

/* VOBJINFO result: generic identity/type/size/rights metadata only. */
typedef struct VobjInfo {
	unsigned long long	incarnation;
	unsigned long long	space;
	unsigned long long	oid;
	unsigned long long	size;
	unsigned long long	holds;
	unsigned int		kind;
	unsigned int		flags;
	unsigned int		rights;
	unsigned int		provider;
	unsigned int		abi_version;
	unsigned int		reserved;
	unsigned char		volume[16];
} VobjInfo;

/* VOBJLIST record.  Handles are process-local; the identity is informational. */
typedef struct VobjListRec {
	unsigned int		handle;
	unsigned int		kind;
	unsigned int		rights;
	unsigned int		reserved;
	unsigned long long	incarnation;
	unsigned long long	space;
	unsigned long long	oid;
} VobjListRec;

typedef struct VobjGeReq {
	int			ospace;
	unsigned int		rights;
	unsigned long long	oid;
} VobjGeReq;

#endif /* PLAN2001_ABI_VOBJ_H */
