/*
 * Universal five-operation object ABI: CREATE, OPEN, CLOSE, BIND, INVOKE.
 *
 * A Handle is a process-local, generation-tagged reference with rights.  It
 * is not an OID, a kernel pointer or ambient authority.  An identity is the
 * scope-qualified (incarnation, space, oid) tuple from runtime/abi/vobj.h;
 * identity, Handle, capability, provider backing and namespace binding are
 * distinct concepts.
 *
 * READ/WRITE and device frame/record operations are userland adapters over
 * OBJINVOKE.  READ of a byte-stream provider returns a byte count; a device
 * receive channel is a capability-mapped region ring described below.  There
 * is no generic kernel message FIFO, Pipe or payload-copy IPC endpoint.
 *
 * All fields are fixed-width and little-endian for amd64.  The kernel
 * validates version, operation, length, alignment and address before using
 * any nested pointer.
 */
#ifndef PLAN2001_ABI_OBJ_H
#define PLAN2001_ABI_OBJ_H

#define OBJ_ABI_VERSION 1

struct Ospawn;

/* obj_open providers.  Numbers 1, 2, 3, 8, 9, 10, 11, 12 and 14 are reserved
 * historical identifiers for the retired TCP6/UDP6 object interface, the old
 * ObjOpenGe alias, the retired Pipe provider, ObjOpenUart, ObjOpenKbd,
 * ObjOpenUsb and the synthetic ProcessSelf handle.  They have no provider,
 * declaration or handler.  All physical device kinds are opened through
 * ObjOpenDevices/ObjOpenDevice. */
enum {
	ObjOpenHandle		= 4,
	ObjOpenBinding		= 5,
	ObjOpenSpace		= 6,	/* authorized name -> open O Space handle */
	ObjOpenGeSpace		= 7,	/* Space handle + OID -> persistent object */
	ObjOpenConsole		= 13,	/* native process output stream */
	ObjOpenDevices		= 15,	/* authorized common device catalog */
	ObjOpenDevice		= 16,	/* selection through a catalog Handle */
	ObjOpenNamespaceCurrent = 17,	/* caller's namespace control */
};

/* obj_create kinds.  ObjKindBytes is volatile immutable content and
 * ObjKindRegion is a volatile capability-mapped region; ObjKindGe is
 * explicitly persistent and requires an authorized open Space handle, never
 * a silent promotion of a volatile identity. */
enum {
	ObjKindBytes		= 0,
	ObjKindGe		= 2,
	ObjKindRegion		= 4,	/* volatile capability-mapped pages */
	ObjCreateKindMask	= 0xff,
};

/* obj_bind modes.  Remove uses handle=0 and rights=0; it changes only the
 * caller's current namespace and does not revoke already opened Handles. */
enum {
	ObjBindCopy		= 0,
	ObjBindExclusive	= 1,
	ObjBindShare		= 2,
	ObjBindModeMask		= 0xff,
	ObjBindRemove		= 0x100,
};

/* OBJINVOKE operations.  Numbers 4, 5, 6, 7, 8, 11, 14, 15, 16, 17, 18, 19,
 * 20, 21, 22, 23, 24, 33, 35, 40 and 46 are reserved historical identifiers
 * for the retired FIFO/Pipe interface, the retired TCP6/UDP6 object
 * interface, handle-less process listing/count, USB service list/endpoint
 * open, the one-shot Pipe selection, the ProcessSelf wait, the former OSPAWN
 * route and the UART/KBD/device-control operations that are now IOCTL(145).
 * They have no dispatch handler.
 *
 * Process Handle-table introspection is SYSCTL(144) SysctlHandleList;
 * completion observation and control are VkindProcess OBJINVOKE operations;
 * creation is SPAWN(146) or RFORKEX(143); device control is IOCTL(145). */
enum {
	ObjOpInfo	= 1,	/* generic identity/type/size/rights metadata */
	ObjOpRead	= 2,
	ObjOpWrite	= 3,
	ObjOpGrant	= 9,	/* RETIRED: OBJOPEN(ObjOpenHandle) */
	ObjOpRevoke	= 10,	/* GE/O grant or volatile-region map revocation */
	ObjOpCreatePersist = 12,	/* RETIRED: OBJCREATE(ObjKindGe) */
	ObjOpCommit	= 13,	/* Space transaction commit (via Space/GE handle) */
	/* 16 is reserved: retired Pipe peer installation. */
	/* 17, 18 and 19 are reserved: UART configuration/status and KBD LED
	 * control moved to IOCTL(145). */
	/* 20 and 21 are reserved: retired USB service list and endpoint open.
	 * USB metadata and endpoint acquisition use ObjOpDeviceList and
	 * ObjOpenDevice like every other supported device family. */
	/* 22 is reserved: the retired one-shot Pipe RFORK selection.  RFORKEX
	 * declares its Handle delegation in the creation call itself. */
	ObjOpPutOid	= 25,	/* Space handle: replace one named OID, no commit */
	ObjOpHeapBreak	= 26,	/* RETIRED: direct BRK_(24) is used */
	ObjOpTimeNow	= 27,	/* RETIRED: direct _NSEC(53) */
	ObjOpSleep	= 28,	/* RETIRED: direct SLEEP(17) is used */
	ObjOpErrstrExchange = 29,	/* RETIRED: direct ERRSTR(41) */
	ObjOpNotifyRegister = 30,	/* RETIRED: direct NOTIFY(28) is used */
	ObjOpNoted	= 31,	/* RETIRED: direct NOTED(29) is used */
	ObjOpRfork	= 32,	/* RETIRED: direct RFORKEX(143) is used */
	ObjOpOspawn	= 33,	/* RETIRED: direct SPAWN(146) is used */
	ObjOpOterminate	= 34,	/* RETIRED: direct OTERMINATE(93) is used */
	ObjOpDeviceList = 37,	/* catalog: bounded discovery records */
	ObjOpDeviceSend = 38,	/* USB device: one complete provider message */
	ObjOpDeviceRecv = 39,	/* device: one USB completion Handle */
	/* 40 is reserved: device control moved to IOCTL(145). */
	ObjOpExit	= 36,	/* RETIRED: direct EXITS(8) is used */
	ObjOpNamespaceCreate = 41,	/* selected roles -> sealed view Handle */
	ObjOpNamespaceAttach = 42,	/* view Handle + mode -> attachment token */
	ObjOpNamespaceDetach = 43,	/* attachment token -> detach */
	ObjOpNamespaceVisible = 44,	/* bounded visible identity records */
	ObjOpDeviceRx	= 45,	/* device: acquire the receive region Handle */
	ObjOpDeviceTx	= 46,	/* device: acquire the transmit region Handle */
	/* VkindRegion: install this region at arg0 (0 = choose) and return the
	 * process-local virtual address.  A Handle carrying Vwrite requests the
	 * single writable mapping; Vread alone requests a read-only mapping.
	 * Unmap takes the process-local address in arg0 and affects only the
	 * caller's address space. */
	ObjOpRegionMap	= 47,
	ObjOpRegionUnmap = 48,
	/* VkindProcess: durable, repeatable completion observation.  WAIT takes
	 * an optional timeout in arg0 (-1 = infinite, 0 = poll) and copies an
	 * ObjProcessInfo to out; it does not consume the completion.  TERMINATE
	 * requires Vcontrol and requests non-cancellable termination at the target
	 * boundary, independently of user note handlers. Repeated requests before
	 * exit begins are idempotent; exiting/completed targets reject requests.
	 * INFO requires Vmeta and never waits for completion. */
	ObjOpProcessInfo = 49,
	ObjOpProcessWait = 50,
	ObjOpProcessTerminate = 51,
};

/* Classification after the SYSCTL/IOCTL/object/scalar split:
 *  - Object-bound (real Handle + identity/rights/lifetime): GE/O read/write/
 *    commit, Space+OID PutOid, namespace views, capability regions, and
 *    common device payload sessions (UART bytes, Ethernet frames, USB
 *    transfers).
 *  - SYSCTL(144): system snapshot, own-process Handle list, environment,
 *    privileged volume activation and own-process CPU/memory introspection.
 *  - IOCTL(145): UART configuration/status, KBD LED control and supported
 *    provider-specific device control.
 *  - Direct primitives: SPAWN/EXITS/RFORKEX, memory, time, synchronization,
 *    NOTIFY/NOTED and ERRSTR.  Process observation/control use OBJINVOKE.
 *  - Discovery and opening: every supported physical family is an
 *    ObjOpDeviceList record opened through ObjOpenDevice with an authorized
 *    catalog Handle.  Selectors are hints, never authority.
 *  - Removed: the TCP6/UDP6 object interface, the separate UART, KBD and
 *    USB opening providers, FIFO, and Pipe.  Raw Ethernet L2 remains;
 *    upper-layer protocols and frame fan-out are userspace.
 */

/* Namespace operations require ObjOpenNamespaceCurrent with Vcontrol.
 * Create takes 1..32 fixed 32-byte NUL-terminated role names in in/inlen
 * and returns a VkindNamespaceView Handle. Attach takes the view Handle in
 * arg0 and NS_REPL/NS_BEFORE/NS_AFTER in arg1; its result is the token.
 * Detach takes the token in arg0. Visible writes up to 32 NsVisible records
 * to out (outlen must be a whole-record capacity); result is record count.
 * No operation accepts extra arguments or buffers. */

/* Common device ABI v2.  See docs/DEVICE-ABI.md for the normative authority,
 * ownership and completion contract.  Discovery selectors are boot-local
 * hints, never authority.  Opening always requires a catalog Handle with
 * Vcreate; USB CTL endpoint references additionally require Vcontrol.
 *
 * Family-specific open options are a bounded, versioned record.  The kernel
 * validates version, option length, flags, mode/kind and selectors before
 * acquiring a port, the PS/2 controller, a NIC or a USB endpoint.  Rights
 * requested at open are an attenuation of the family's grantable set, not a
 * caller claim of authority.  No new syscall is introduced.
 */
#define OBJ_DEVICE_ABI_VERSION 2
#define OBJ_DEVICE_LISTMAX 128
#define OBJ_DEVICE_MAXPAYLOAD 4096
#define OBJ_DEVICE_MAXMESSAGE 16384
#define OBJ_DEVICE_OPTMAX 64
#define OBJ_DEVICE_OPT_VERSION 1

enum {
    ObjDeviceUsbControl = 1,       /* synchronous control session (SEND/RECV) */
    ObjDeviceEthernet = 2,         /* raw L2 region-ring TX and RX */
    ObjDeviceUart = 3,             /* authorized bounded byte stream */
    ObjDeviceKbd = 4,              /* exclusive PS/2 scan-code stream */
    ObjDeviceUsbEndpoint = 5,      /* referenced DATA or CTL endpoint */
    ObjDeviceUsbClearHalt = 1,
    ObjDeviceEtherStatus = 2,
    ObjDeviceEtherDetach = 3,
};

/* KBD open must request the exclusive scan-code claim explicitly. */
#define OBJ_DEVICE_KBD_EXCLUSIVE 1u

typedef struct ObjDeviceOpenOptions {
    unsigned int version;          /* OBJ_DEVICE_OPT_VERSION */
    unsigned int mode;             /* UART/USB endpoint: OREAD/OWRITE/ORDWR */
    unsigned int kind;             /* USB endpoint: OBJ_USB_EP_DATA/CTL */
    unsigned int flags;            /* KBD: OBJ_DEVICE_KBD_EXCLUSIVE; else 0 */
} ObjDeviceOpenOptions;

/* Ethernet: complete L2 octets, including MACs/EtherType, excluding FCS.
 * RX (ObjOpDeviceRx) returns one read-only VkindRegion Handle carrying an
 * ObjRingHeader and fixed slots; the kernel is the only writer mapping.
 * TX (ObjOpDeviceTx) returns one VkindRegion Handle whose unique writable
 * mapping is the authorized userspace publisher; the kernel consumes complete
 * published frames and advances header.ack_seq.  Neither direction is a
 * completed-message receive and no USB setup or synchronous USB completion is
 * implied. */
typedef struct ObjEtherRxMeta {
    unsigned int version;
    unsigned int controller;
    unsigned int length;
    unsigned int reserved;
    unsigned long long sequence;
} ObjEtherRxMeta;

typedef struct ObjEtherStatus {
    unsigned int version;
    unsigned int controller;
    unsigned int minframe;
    unsigned int maxframe;
    unsigned char mac[6];
    unsigned char reserved[2];
    int link;
    unsigned int closed;
    unsigned long long arrivals;
    unsigned long long published;
    unsigned long long handoff_drops;
    unsigned long long publish_drops;
    unsigned long long tx_accepted;
    unsigned long long highwater;
    unsigned int handoff_capacity;
    unsigned int handoff_queued;
    unsigned long long tx_dropped;
    unsigned long long tx_errors;
} ObjEtherStatus;

long obj_device_ether_status(int device, ObjEtherStatus *status);

/* Capability-region ring transport (device receive/transmit channels and
 * console session streams).  One writer owns the unique writable mapping; any
 * number of authorized readers map the region read-only.  The writer stores a
 * complete immutable record into slot (seq-1)%slots, publishes the slot with
 * the even value 2*seq, then publishes header.write_seq = seq with release
 * ordering and calls REGIONWAKE(base, OBJ_RING_PUBOFF, ...).  Readers load
 * header.write_seq with acquire ordering and treat a slot whose sequence is
 * not the expected even value as overwritten and therefore dropped.
 *
 * For kernel-published receive/console rings the kernel owns the writer
 * mapping.  For an Ethernet transmit ring the authorized publisher owns the
 * unique writable mapping and the kernel is the read-only consumer: it loads
 * write_seq, copies complete frames into NIC-owned Blocks and publishes
 * header.ack_seq = consumed sequence with release ordering, then wakes
 * REGIONWAIT observers of OBJ_RING_ACKOFF.  A publisher must not reuse a slot
 * before it is acknowledged; the ring is full when
 * write_seq - ack_seq == slots.  A publisher that overruns the acknowledgement
 * loses the older slots, which the kernel skips.
 *
 * Publication word: byte offset OBJ_RING_PUBOFF (8-byte aligned) in the
 * region.  Consumed-sequence word: byte offset OBJ_RING_ACKOFF (8-byte
 * aligned).  Slot data starts at OBJ_RING_HEADER + index*OBJ_RING_SLOT + 16.
 * The header is zeroed and preinitialized by the kernel. */
#define OBJ_RING_VERSION 1u
#define OBJ_RING_MAGIC   0x5250324fu /* "RP2O" little-endian */
#define OBJ_RING_PUBOFF  16u
#define OBJ_RING_ACKOFF  32u
#define OBJ_RING_HEADER  4096u
#define OBJ_RING_SLOTS   64u
#define OBJ_RING_SLOT    2048u
#define OBJ_RING_DATA    (OBJ_RING_SLOT - 16u)
#define OBJ_RING_BYTES   (OBJ_RING_HEADER + OBJ_RING_SLOTS*OBJ_RING_SLOT)

typedef struct ObjRingHeader {
    unsigned int version;
    unsigned int magic;
    unsigned int slot_size;
    unsigned int slots;
    unsigned long long write_seq;   /* publication word at OBJ_RING_PUBOFF */
    unsigned long long drops;
    unsigned long long ack_seq;     /* consumed sequence at OBJ_RING_ACKOFF */
} ObjRingHeader;

typedef struct ObjRingSlot {
    unsigned long long seq;         /* 0 empty, odd writing, even published */
    unsigned int length;
    unsigned int flags;
} ObjRingSlot;

typedef struct ObjDeviceEntry {
    unsigned int version;          /* OBJ_DEVICE_ABI_VERSION */
    unsigned int family;
    int selector0;                 /* USB devno, NIC controller, UART port */
    int selector1;                 /* USB endpoint number; 0 otherwise */
    unsigned int maxsend;          /* setup+OUT, frame, or stream bound */
    unsigned int maxrecv;          /* IN bytes, frame, or stream bound */
    unsigned int flags;            /* zero */
    int kind;                      /* USB endpoint ttype; 0 otherwise */
    unsigned int reserved;         /* zero */
} ObjDeviceEntry;

typedef struct ObjOpenDeviceReq {
    unsigned int version;
    unsigned int family;
    int catalog;                   /* process-local discovery authority */
    int selector0;
    int selector1;
    unsigned int flags;            /* request flags; must be zero */
    unsigned int optlen;           /* 0 or sizeof ObjDeviceOpenOptions */
    unsigned int reserved;         /* zero */
    unsigned char options[OBJ_DEVICE_OPTMAX];
} ObjOpenDeviceReq;

/* USB control RECV (ObjOpDeviceRecv) installs one Vread|Vmeta|Vdelegate
 * bytes Handle as result, and copies this completion into out.  A failed
 * install leaves the completion pending.  A USB SEND completes
 * synchronously and returns a positive session-local sequence.  Ethernet
 * frames use the transmit region, never SEND.
 * Exactly one unreceived USB completion is permitted; another SEND is Einuse.
 * USB input is the original 8-byte setup followed by exactly wLength OUT
 * bytes (none for IN).  IN response may be shorter than wLength.  OUT's
 * completion is an empty immutable bytes object.  Errors use kernel errstr;
 * no completion is enqueued on failed SEND, whose hardware effects may have
 * begun.  No implicit retry, caller-buffer retention or IRQ-context work.
 */
typedef struct ObjDeviceResult {
    unsigned long long sequence;
    unsigned int family;
    unsigned int length;
} ObjDeviceResult;

/* obj_invoke(ObjOpPutOid): replace the content of one explicitly named
 * persistent OID through a VkindSpace Handle carrying Vwrite.  arg0 is the
 * OID; in/inlen are the bounded payload.  It uses the same Ohand Owrite
 * authority and per-Space gate as OSPACEPUT and does NOT commit; the pending
 * write joins the Space transaction until an explicit ObjOpCommit.  The
 * payload bound is the native object transaction bound (64 MiB), not the
 * 1 MiB single-call OBJ-I/O bound, so a whole OID_GRAPH keeps its former
 * maximum size.  This is not a new syscall number and it never allocates an
 * OID; creation uses OBJCREATE(ObjKindGe). */
#define OBJ_PUT_OID_MAX (64ULL*1024*1024)

/* Scope-qualified object identity, layout-compatible with VobjId. */
typedef struct ObjId {
	unsigned long long	incarnation;
	unsigned long long	space;
	unsigned long long	oid;
} ObjId;

/* obj_open(ObjOpenConsole): bind one native process output stream.  stream
 * is Iohout=1 or Iohdiag=2; the authority is the Proc's existing iomask.
 * The returned Handle carries Vwrite and is the replacement for the retired
 * IOWRITE(62) number.  It is not a file descriptor, pathname or Chan.  A
 * console owner may route its bytes into a region session instead. */
typedef struct ObjOpenConsoleReq {
	int			stream;
	unsigned int		reserved;
} ObjOpenConsoleReq;

/* Heap break, sleep, note registration, note return, rfork, child wait,
 * oterminate and exits use their direct historical syscalls.  There is no
 * ProcessSelf object and no OBJINVOKE variant for them. */

/* obj_open(ObjOpenHandle): the source capability and an optional attenuation.
 * rights==0 copies the source rights.  Knowing the numeric Handle alone in
 * another process is not authority: a Handle is process-local. */
typedef struct ObjOpenHandleReq {
	int			handle;
	unsigned int		reserved;
} ObjOpenHandleReq;

/* obj_open(ObjOpenSpace): a bounded name of an existing O Space.  The name
 * is not a path and grants nothing by itself; osopenspace() applies the
 * bootstrap/volume authorization and returns an attenuated handle. */
/* obj_open(ObjOpenGeSpace): reopen an existing persistent object through an
 * authorized Space handle.  space is a process-local VkindSpace handle, not
 * an OID or a global name. */
typedef struct ObjOpenGeSpaceReq {
	int			space;
	unsigned int		reserved;
	unsigned long long	oid;
} ObjOpenGeSpaceReq;

/* obj_open(ObjOpenDevice) family requirements:
 *
 * Uart (selector0 = discovery port, selector1 = 0): options required;
 * version/kind/flags must be zero; mode is OREAD(0), OWRITE(1) or ORDWR(2).
 * The Handle carries only rights attenuated from that mode.  READ/WRITE are
 * bounded byte streams, UartCtl requires Vcontrol and UartStatus/Info require
 * Vmeta.  This is not a frame or packet API.
 *
 * Kbd (selector0 = selector1 = 0): options required; version/mode/kind zero
 * and flags == OBJ_DEVICE_KBD_EXCLUSIVE.  The open claims the scan-code
 * controller.  READ returns raw scan codes; KbdLeds requires Vcontrol.
 *
 * UsbControl (selector0 = devno, selector1 = epno): no options.  Opens the
 * existing synchronous control session with SEND/RECV semantics below.
 *
 * UsbEndpoint (selector0 = devno, selector1 = epno): options required with
 * kind OBJ_USB_EP_DATA or Obj_USB_EP_CTL and a valid mode.  DATA performs the
 * existing exclusive data open; CTL references the endpoint for provider
 * administration.  CTL endpoint references additionally require Vcontrol on
 * the catalog.  There is still no USB bulk data API.
 *
 * Ethernet (selector0 = controller, selector1 = 0): no options.
 */

/* USB endpoint kinds are the existing usbcore values; modes use OREAD,
 * OWRITE and ORDWR. */
#define OBJ_USB_EP_DATA  0
#define OBJ_USB_EP_CTL   1

/* Byte-stream bound shared by the UART and KBD providers. */
#define OBJ_STREAM_MAXIO 65536
#define OBJ_KBD_MAXREAD 4096
#define OBJ_UART_STATMAX 65536

/* Handle inheritance is never armed through OBJINVOKE.  Use the direct
 * RFORKEX(143) call, whose options record declares the crossing Handles and
 * their rights in the same atomic operation as child creation. */

/* IOCTL IoctlKbdLeds: set the shared keyboard LED state through an
 * authorized KBD Handle carrying Vcontrol.  Authorization is the Handle
 * capability, not a repeated identity check: a legitimately delegated
 * Vcontrol Handle remains usable by a non-privileged child.  Handle 0 is
 * rejected, so LED state is never a global-handle privilege bypass.  The
 * request carries the LED bits in arg0 and no data buffer. */

/*
 * One typed, versioned INVOKE request/response.
 *
 * op selects the provider operation.  rights is reserved: the kernel derives
 * authority from the Handle, never from a caller claim.  in/inlen and
 * out/outlen are validated according to the operation direction.  result is
 * a byte count, a Handle or 0 depending on the operation; aux carries a
 * secondary value (a device SEND result discriminator, for example).
 */
typedef struct ObjCall {
	unsigned int		version;
	unsigned int		op;
	unsigned int		flags;
	unsigned int		rights;
	unsigned long long	arg0;
	unsigned long long	arg1;
	void			*in;
	unsigned long long	inlen;
	void			*out;
	unsigned long long	outlen;
	long long		result;
	unsigned long long	aux;
	unsigned int		status;
	unsigned int		reserved;
} ObjCall;

/* Process lifecycle is capability-based.  SPAWN and RFORKEX return a
 * process-local VkindProcess Handle carrying Vobserve|Vcontrol|Vmeta|
 * Vdelegate; a child sees 0 from RFORKEX.  The Handle is the authority, not
 * a PID.  Observation is repeatable and independent of launcher lifetime.
 * Control requires a separate Vcontrol grant. */

/* Coherent observable lifecycle.  CREATED covers construction before the
 * process is schedulable; RUNNING is a live execution context; COMPLETED is
 * an immutable result.  A completion record is retained while any Handle or
 * transient publication reference exists and is released at the last one. */
enum {
	ObjProcCreated		= 0,
	ObjProcRunning		= 1,
	ObjProcCompleted	= 2,
};

enum {
	ObjProcCauseNone	= 0,
	ObjProcCauseExit	= 1,	/* voluntary EXITS */
	ObjProcCauseTerminated	= 2,	/* authorized control termination */
	ObjProcCauseFault	= 3,	/* unhandled fault or fatal note */
};

#define OBJ_PROCESS_MSG_MAX 128

typedef struct ObjProcessInfo {
	unsigned int		version;	/* OBJ_ABI_VERSION */
	unsigned int		state;		/* ObjProc* state */
	unsigned int		cause;		/* ObjProcCause* (COMPLETED) */
	unsigned int		reserved;
	long long		pid;		/* identifier only, not authority */
	unsigned long long	generation;	/* boot-local identity generation */
	unsigned long long	time[3];	/* user, sys, real (ms) */
	unsigned int		msglen;		/* bytes in msg, no NUL */
	unsigned int		reserved2;
	char			msg[OBJ_PROCESS_MSG_MAX];
	unsigned int		waiters;	/* observers enrolled at snapshot time */
	unsigned int		reserved3;
} ObjProcessInfo;

#define PROCESS_WAIT_INFINITE ((unsigned long long)-1)

/* SPAWN(int Space handle, uvlong OID, char **argv, int argc,
 * struct Ospawn *options) creates one static x86-64 ELF child from an
 * explicit GE/O Space capability and OID.  The Handle must be a VkindSpace
 * Handle carrying Vexecute; the same Ohand Oexecute gate and per-Space
 * authorization as OEXECUTE remain in force.  argv/options are validated by
 * the shared spawn core, including the Ospawn and optional
 * Oinherit/Oinheritv blocks.  The child receives the same registers, stack,
 * argv, CPU affinity, memory budget, namespace/view, environment and
 * Vhand inheritance semantics.  The result is the child PID. */

#define OBJ_EXITMSG_MAX 128

/* Process-lifecycle adapters over the VkindProcess Handle. */
long obj_process_info(int handle, ObjProcessInfo *out);
long obj_process_wait(int handle, ObjProcessInfo *out, long long timeout);
long obj_process_terminate(int handle);
long obj_ospawn(int space, unsigned long long oid, char **argv, int argc,
		struct Ospawn *options);

/*
 * libc/libo-style wrappers over the five kernel entry points.  The raw
 * syscall stubs are objcreate/objopen/objclose/objbind/objinvoke; these
 * underscored names add argument packing and typed adapters.
 */
long obj_create(unsigned long long space, unsigned int kind,
		void *data, unsigned long long len, ObjId *out);
long obj_open(unsigned long long provider, void *req, long reqlen,
		unsigned long long rights, ObjId *out);
long obj_close(int handle);
long obj_bind(int handle, const char *role, unsigned long long len,
		unsigned long long rights, unsigned int mode);
long obj_invoke(int handle, ObjCall *call, long calllen);

long obj_create_persist(int space, void *data, unsigned long long len,
		unsigned long long *oid);
long obj_put_oid(int space, unsigned long long oid, void *buf,
		unsigned long long len);
long obj_commit(int handle, unsigned long long *version);

/* READ/WRITE accept only timeout 0, which preserves the provider's existing
 * blocking behavior.  A finite timeout is rejected explicitly because no
 * current provider implements one; it is never silently ignored. */
long obj_read(int handle, void *buf, unsigned long long len,
		unsigned long long timeout);
/* A persistent GE READ also returns the O version it read in *version.
 * Other providers leave it zero.  A zero-length read with buf==NULL is the
 * existing size query; it reports the size and the published version. */
long obj_read_version(int handle, void *buf, unsigned long long len,
		unsigned long long timeout, unsigned long long *version);
long obj_write(int handle, void *buf, unsigned long long len,
		unsigned long long timeout);
long obj_info(int handle, void *info);
long obj_grant(int handle, unsigned long long rights);
/* GE/O capability revocation and volatile-region map revocation.  A region
 * Handle needs Vcontrol to revoke; existing mappings survive, new mappings
 * through any Handle are denied. */
long obj_revoke(int handle);
/* Own-process Handle-table introspection over SYSCTL, not an INVOKE
 * operation.  It reports only the caller's own table and copies at most
 * count records.  It returns the total live count, so an undersized buffer
 * is safe; *copied (when non-NULL) receives the number actually copied. */
long obj_list(void *records, unsigned long long count);
long obj_list_count(void *records, unsigned long long count,
		unsigned long long *copied);

/* Native byte-stream providers over the same five-call ABI.  Device control
 * (UART configuration/status, KBD LEDs) is IOCTL(145), not OBJINVOKE. */

/* Volatile capability regions.  Creation takes a page-multiple size and
 * returns a Handle carrying Vread|Vwrite|Vmeta|Vdelegate|Vcontrol.  A writer
 * mapping is unique for the region; read-only mappings are shared.  The
 * mapping survives closing the Handle and is released by obj_region_unmap()
 * or process exit.  obj_revoke() on a Vcontrol Handle blocks all future
 * mappings; installed mappings and page backing remain until unmap/exit and
 * the last reference. */
long obj_region_create(unsigned long long size, ObjId *out);
long obj_region_map(int handle, unsigned long long addr);
long obj_region_unmap(unsigned long long addr);

/* Region-scoped publication wait/wake over the direct REGIONWAIT(147) and
 * REGIONWAKE(148) calls.  base is the mapping returned by obj_region_map,
 * offset is region-relative and 8-byte aligned, and the 64-bit publication
 * word is compared only for equality.  An RO mapping may wait; only the
 * writable mapping may wake.  See runtime/abi/sys.h for status values and
 * ordering rules.  Userland acquires/stores the word with its own native
 * atomic operations; the kernel never copies the payload. */
long region_wait(unsigned long long base, unsigned long long offset,
		unsigned long long expected, long long timeout);
long region_wake(unsigned long long base, unsigned long long offset,
		unsigned long long count);
long obj_uart_ctl(int handle, void *cfg);
long obj_uart_status(int handle, void *buf, unsigned long long len);
long obj_kbd_leds(int handle, unsigned int leds);

/* Common device catalog.  Opening a supported family requires an authorized
 * catalog Handle; options is a bounded ObjDeviceOpenOptions record for the
 * families documented above.  obj_catalog_device_open is a userland
 * convenience that performs the two authorized operations and still returns
 * the provider's exact error. */
long obj_devices_open(unsigned long long rights);
long obj_device_list(int catalog, ObjDeviceEntry *entries,
		unsigned int count);
long obj_device_open(int catalog, unsigned int family, int selector0,
		int selector1, unsigned long long rights, void *options,
		unsigned long long optlen);
long obj_catalog_device_open(unsigned int family, int selector0,
		int selector1, unsigned long long rights, void *options,
		unsigned long long optlen);

/* Device SEND: one complete USB control request.  Ethernet frames use the
 * transmit region instead, so a SEND return is always a session-local USB
 * completion sequence. */
long obj_device_send(int device, void *message, unsigned long long length);
/* USB control completion retrieval: one completed immutable bytes Handle. */
long obj_device_recv(int device, ObjDeviceResult *completion);
/* Ethernet receive-channel acquisition: one read-only region Handle carrying
 * an ObjRingHeader.  This is not a completed-message receive. */
long obj_device_rx(int device);
/* Ethernet transmit-channel acquisition: one VkindRegion Handle whose unique
 * writable mapping is the frame publisher.  Not a per-frame syscall. */
long obj_device_tx(int device);
/* Typed device control over IOCTL(145). */
long obj_device_ioctl(int device, unsigned int operation, void *data,
		unsigned long long len, unsigned long long arg0);
long obj_device_usb_global_ctl(int catalog, unsigned int request, void *data,
		unsigned long long len);
long obj_device_usb_ep_ctl(int endpoint, unsigned int request, void *data,
		unsigned long long len);
long obj_device_usb_clear_halt(int device);
long obj_device_ether_detach(int device);

#endif /* PLAN2001_ABI_OBJ_H */
