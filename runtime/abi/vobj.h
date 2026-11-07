/*
 * Volatile object and ObjectFIFO userspace ABI.
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

#define VOBJ_ABI_VERSION 2

/* Independent, attenuable rights on an object or subscription handle. */
enum {
	Vread		= 1<<0,	/* read immutable object bytes */
	Vsend		= 1<<1,	/* publish references into a FIFO */
	Vrecv		= 1<<2,	/* receive references from a subscription */
	Vmeta		= 1<<3,	/* query identity/metrics */
	Vclose		= 1<<4,	/* reserved; close is always allowed */
	Vsubscribe	= 1<<5,	/* create independent subscriptions (TEE) */
	Vcontrol	= 1<<6,	/* revoke/close a FIFO for all subscribers */
	Vdelegate	= 1<<7,	/* create attenuated handles */
	Vwrite		= 1<<8,	/* write stream/GE bytes where the provider allows */
	Vaccept		= 1<<9,	/* accept connections from a listener object */
	Vconnect	= 1<<10,	/* create a connection from a connection provider */
	Vcreate		= 1<<11,	/* create persistent objects in an O Space */
	Vcommit		= 1<<12,	/* commit a persistent O Space transaction */
	Vexecute	= 1<<13,	/* execute/load a GE/O object into a child */
	Vall		= 0xffff,
};

enum {
	VkindBytes	= 0,	/* immutable byte content, including empty */
	VkindFifo	= 1,	/* message-framed ObjectFIFO */
	VkindTcp	= 2,	/* TCP listener or connection */
	VkindGe		= 3,	/* persistent GE/O object capability */
	VkindSpace	= 4,	/* authorized open O Space capability */
	VkindUdp6	= 5,	/* native UDP6 datagram endpoint capability */
	VkindPipe	= 6,	/* one end of a native byte-stream Pipe pair */
	VkindUart	= 7,	/* native UART port, mode from OPEN */
	VkindKbd	= 8,	/* exclusive PS/2 scan-code source */
	VkindUsb	= 9,	/* USB service: endpoint metadata, no I/O */
	VkindUsbEp	= 10,	/* one opened USB endpoint; no data path yet */
	VkindConsole	= 11,	/* native process output stream */
	VkindDevices = 13,	/* common device discovery/open authority */
	VkindDevice = 14,	/* one native message device session */
	VkindProcessSelf = 12,	/* caller's own BSEG heap break */
};

/* VOBJOPEN providers.  These select a concrete provider, not a data schema. */
enum {
	VopenTcpListen	= 1,
	VopenTcpConnect	= 2,
	VopenGe		= 3,
	VopenSpace	= 4,	/* authorized name -> open O Space */
	VopenGeSpace	= 5,	/* authorized open O Space + OID -> GE object */
	VopenUdp6	= 6,	/* authorized native UDP6 endpoint bind */
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

/* VOBJINFO result. */
typedef struct VobjInfo {
	unsigned long long	incarnation;
	unsigned long long	space;
	unsigned long long	oid;
	unsigned long long	size;
	unsigned long long	holds;
	unsigned long long	subscribers;
	unsigned long long	sends;
	unsigned long long	drops;
	unsigned int		kind;
	unsigned int		flags;
	unsigned int		rights;
	unsigned int		sub_capacity;
	unsigned int		sub_queued;
	unsigned int		sub_dropped;
	unsigned int		fifo_closed;
	unsigned int		sub_closed;
	unsigned int		provider;
	unsigned int		abi_version;
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

typedef struct VobjTcpReq {
	unsigned char		addr[16];
	unsigned int		port;
	unsigned int		flags;
} VobjTcpReq;

typedef struct VobjGeReq {
	int			ospace;
	unsigned int		rights;
	unsigned long long	oid;
} VobjGeReq;

/* VOBJFIFOSTAT result. */
typedef struct VobjFifostat {
	unsigned long long	sends;
	unsigned long long	drops;
	unsigned int		subscribers;
	unsigned int		queued;
	unsigned int		capacity;
	unsigned int		fifo_closed;
	unsigned int		sub_closed;
	unsigned int		reserved;
} VobjFifostat;

#endif /* PLAN2001_ABI_VOBJ_H */
