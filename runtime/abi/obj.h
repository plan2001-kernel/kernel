/*
 * Universal five-operation object ABI: CREATE, OPEN, CLOSE, BIND, INVOKE.
 *
 * A Handle is a process-local, generation-tagged reference with rights.  It
 * is not an OID, a kernel pointer or ambient authority.  An identity is the
 * scope-qualified (incarnation, space, oid) tuple from runtime/abi/vobj.h;
 * identity, Handle, capability, provider backing and namespace binding are
 * distinct concepts.
 *
 * READ/WRITE, PUBLISH/SUBSCRIBE/RECEIVE and ACCEPT/CONNECT are userland
 * adapters over OBJINVOKE.  READ of a byte-stream provider returns a byte
 * count; RECEIVE of an ObjectFIFO returns one authorized object Handle plus
 * an optional small opaque envelope.  The two are not the same semantics.
 *
 * All fields are fixed-width and little-endian for amd64.  The kernel
 * validates version, operation, length, alignment and address before using
 * any nested pointer.
 */
#ifndef PLAN2001_ABI_OBJ_H
#define PLAN2001_ABI_OBJ_H

#define OBJ_ABI_VERSION 1

/* obj_open providers.  Numbers 1, 2, 3, 8 and 14 are reserved historical
 * identifiers for the retired TCP6/UDP6 object interface, the old ObjOpenGe
 * alias and the synthetic ProcessSelf handle.  They have no provider,
 * declaration or handler. */
enum {
	ObjOpenHandle		= 4,
	ObjOpenBinding		= 5,
	ObjOpenSpace		= 6,	/* authorized name -> open O Space handle */
	ObjOpenGeSpace		= 7,	/* Space handle + OID -> persistent object */
	ObjOpenPipe		= 9,	/* provider tag for a native Pipe endpoint */
	ObjOpenUart		= 10,	/* privileged native UART port open */
	ObjOpenKbd		= 11,	/* privileged exclusive PS/2 claim */
	ObjOpenUsb		= 12,	/* USB service: device/endpoint metadata */
	ObjOpenConsole		= 13,	/* native process output stream */
	ObjOpenDevices		= 15,	/* authorized common device catalog */
	ObjOpenDevice		= 16,	/* selection through a catalog Handle */
	ObjOpenNamespaceCurrent = 17,	/* caller's namespace control */
};

/* obj_create kinds.  The first two are volatile and ObjKindPipe is a native
 * byte-stream pair; ObjKindGe is explicitly persistent and requires an
 * authorized open Space handle, never a silent promotion of a volatile
 * identity. */
enum {
	ObjKindBytes		= 0,
	ObjKindFifo		= 1,
	ObjKindGe		= 2,
	ObjKindPipe		= 3,	/* create one pair; peer via ObjOpPipePeer */
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

/* OBJINVOKE operations.  Numbers 8, 11, 14, 15, 23, 24 and 35 are reserved
 * historical identifiers for the retired TCP6/UDP6 object interface, the
 * handle-less process listing and UART count, and the synthetic ProcessSelf
 * wait.  Number 22 is the retired one-shot Pipe RFORK selection.  They have
 * no dispatch handler.  Process handle introspection is the direct bounded
 * OBJLIST syscall and child wait is the direct typed OBJWAIT syscall. */
enum {
	ObjOpInfo	= 1,
	ObjOpRead	= 2,
	ObjOpWrite	= 3,
	ObjOpStat	= 4,
	ObjOpSend	= 5,
	ObjOpRecv	= 6,
	ObjOpSubscribe	= 7,
	ObjOpGrant	= 9,	/* RETIRED: OBJOPEN(ObjOpenHandle) */
	ObjOpRevoke	= 10,
	ObjOpCreatePersist = 12,	/* RETIRED: OBJCREATE(ObjKindGe) */
	ObjOpCommit	= 13,	/* Space/GE handle: commit pending writes */
	ObjOpPipePeer	= 16,	/* Pipe end 0: install and return the peer Handle */
	ObjOpUartCtl	= 17,	/* UART handle: apply one bounded Uartcfg */
	ObjOpUartStatus	= 18,	/* UART handle: read native status bytes */
	ObjOpKbdLeds	= 19,	/* KBD handle: privileged LED update */
	ObjOpUsbList	= 20,	/* USB service: bounded Usbentry snapshot */
	ObjOpUsbEpOpen	= 21,	/* USB service: open one endpoint Handle */
	/* 22 is reserved: the retired one-shot Pipe RFORK selection.  RFORKEX
	 * declares its Handle delegation in the creation call itself. */
	ObjOpPutOid	= 25,	/* Space handle: replace one named OID, no commit */
	ObjOpHeapBreak	= 26,	/* RETIRED: direct BRK_(24) is used */
	ObjOpTimeNow	= 27,	/* RETIRED: direct _NSEC(53) */
	ObjOpSleep	= 28,	/* RETIRED: direct SLEEP(17) is used */
	ObjOpErrstrExchange = 29,	/* RETIRED: direct ERRSTR(41) */
	ObjOpNotifyRegister = 30,	/* RETIRED: direct NOTIFY(28) is used */
	ObjOpNoted	= 31,	/* RETIRED: direct NOTED(29) is used */
	ObjOpRfork	= 32,	/* RETIRED: direct RFORK(19) is used */
	ObjOpOspawn	= 33,	/* Space handle + OID: spawn a static ELF child */
	ObjOpOterminate	= 34,	/* RETIRED: direct OTERMINATE(93) is used */
	ObjOpDeviceList = 37,	/* catalog: bounded discovery records */
	ObjOpDeviceSend = 38,	/* device: one complete provider message */
	ObjOpDeviceRecv = 39,	/* device: completed immutable bytes Handle */
	ObjOpDeviceCtl = 40,	/* device: authorized provider-specific control */
	ObjOpExit	= 36,	/* RETIRED: direct EXITS(8) is used */
	ObjOpNamespaceCreate = 41,	/* selected roles -> sealed view Handle */
	ObjOpNamespaceAttach = 42,	/* view Handle + mode -> attachment token */
	ObjOpNamespaceDetach = 43,	/* attachment token -> detach */
	ObjOpNamespaceVisible = 44,	/* bounded visible identity records */
};

/* Classification after the OBJINVOKE pragmatization:
 *  - Object-bound (real Handle + identity/rights/lifetime): GE/O read/write/
 *    commit, Space+OID PutOid/Ospawn, ObjectFIFO, namespace views, Pipe, UART,
 *    KBD, USB service/endpoint and common device sessions.
 *  - Direct syscalls: process handle listing (OBJLIST) and child wait
 *    (OBJWAIT), because neither concerns a target object capability.
 *  - Discovery: UART ports are ObjOpDeviceList records, not a handle-less
 *    INVOKE operation; opening still uses ObjOpenUart.
 *  - Removed: the TCP6/UDP6 object interface.  Raw Ethernet and ObjectFIFO
 *    remain; upper-layer protocols are userspace.
 */

/* Namespace operations require ObjOpenNamespaceCurrent with Vcontrol.
 * Create takes 1..32 fixed 32-byte NUL-terminated role names in in/inlen
 * and returns a VkindNamespaceView Handle. Attach takes the view Handle in
 * arg0 and NS_REPL/NS_BEFORE/NS_AFTER in arg1; its result is the token.
 * Detach takes the token in arg0. Visible writes up to 32 NsVisible records
 * to out (outlen must be a whole-record capacity); result is record count.
 * No operation accepts extra arguments or buffers. */

/* Common message-device ABI v1.  See docs/DEVICE-ABI.md for the normative
 * ownership/completion contract.  Discovery selectors are boot-local hints,
 * never authority.  Opening requires a catalog Handle with Vcreate; rights
 * are attenuated from Vsend|Vrecv|Vmeta|Vcontrol|Vdelegate.  There is no new
 * syscall.  Existing provider tags/operations keep their numbers.
 *
 * T12 integrates USB control endpoints only.  Ethernet frames, USB bulk and
 * Wi-Fi have their own payload/metadata contracts in future integrations.
 */
#define OBJ_DEVICE_ABI_VERSION 1
#define OBJ_DEVICE_LISTMAX 128
#define OBJ_DEVICE_MAXPAYLOAD 4096
#define OBJ_DEVICE_MAXMESSAGE 16384

enum {
    ObjDeviceUsbControl = 1,
    ObjDeviceEthernet = 2,
    ObjDeviceUart = 3,             /* discovery record only; open via ObjOpenUart */
    ObjDeviceUsbClearHalt = 1,
    ObjDeviceEtherStatus = 2,
    ObjDeviceEtherDetach = 3,
};

/* Ethernet: complete L2 octets, including MACs/EtherType, excluding FCS.
 * SEND returns accepted bytes, not wire/DMA completion. RECV obtains the
 * existing ObjectFIFO capability; subscribe/receive use the ordinary OBJ
 * operations. No USB setup or synchronous USB completion is implied. */
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
} ObjEtherStatus;

long obj_device_ether_status(int device, ObjEtherStatus *status);

typedef struct ObjDeviceEntry {
    unsigned int version;
    unsigned int family;
    int selector0;                 /* USB device number */
    int selector1;                 /* USB endpoint number */
    unsigned int maxsend;          /* setup + optional OUT data */
    unsigned int maxrecv;          /* IN response bytes */
    unsigned int flags;            /* zero */
    unsigned int reserved;         /* zero */
} ObjDeviceEntry;

typedef struct ObjOpenDeviceReq {
    unsigned int version;
    unsigned int family;
    int catalog;                   /* process-local discovery authority */
    int selector0;
    int selector1;
    unsigned int reserved;
} ObjOpenDeviceReq;

/* RECV installs one Vread|Vmeta|Vdelegate bytes Handle as result, and copies
 * this completion into out.  A failed install leaves the completion pending.
 * SEND completes synchronously and returns a positive session-local sequence.
 * Exactly one unreceived completion is permitted; another SEND is Einuse.
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

long obj_devices_open(unsigned long long rights);
long obj_device_list(int catalog, ObjDeviceEntry *entries, unsigned int count);
long obj_device_open(int catalog, unsigned int family, int selector0,
    int selector1, unsigned long long rights);
long obj_device_send(int device, void *message, unsigned long long length);
long obj_device_recv(int device, ObjDeviceResult *completion);
long obj_device_ctl(int device, unsigned int operation);
long obj_usb_ctl(int handle, unsigned int operation, void *data,
                 unsigned long long len);

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
 * is Iohout=1 or Iohdiag=2; the authority is the Proc's existing iomask (or,
 * for Iohout, an explicit stdout Pipe).  The returned Handle carries Vwrite
 * and is the replacement for the retired IOWRITE(62) number.  It is not a
 * file descriptor, pathname or Chan. */
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

/* obj_open(ObjOpenUart): port is the discovery-local UART index reported by
 * OBJINVOKE(ObjOpDeviceList) records with family ObjDeviceUart (selector0).
 * mode is OREAD(0), OWRITE(1) or ORDWR(2); flags and reserved are zero.  The
 * call requires the existing eve/bootstrap authority.  Read requires Vread,
 * write requires Vwrite, UartCtl requires Vcontrol and UartStatus/Info require
 * Vmeta. */
typedef struct ObjOpenUartReq {
	int			port;
	int			mode;
	unsigned int		flags;
	unsigned int		reserved;
} ObjOpenUartReq;

/* obj_invoke(ObjOpUsbList): one record per live USB endpoint.  The layout is
 * byte-compatible with the kernel/legacy Usbentry (include/usbctl.h): nine
 * int fields followed by a 128-byte name.  out points to an array of records,
 * outlen is the byte capacity and must be a multiple of sizeof(ObjUsbEntry).
 * A capacity above OBJ_USB_LISTMAX records is accepted and clamped to that
 * bound, matching obj_usb_list(); only the copied records are validated and
 * written.  out==NULL or outlen==0 is the legacy size query and returns the
 * total endpoint count without copying.  Otherwise the result is the number
 * of copied records (clamped to the available total), exactly like USBLIST.
 * Listing requires Vmeta on the USB service Handle.  This operation exposes
 * metadata only; it does not open an endpoint and does not provide USBIOCTL
 * or data transfer. */
#define OBJ_USB_LISTMAX 256

/* Endpoint kinds and open modes are the existing usbcore/legacy USB values.
 * ObjUsbEpReq.mode uses OREAD(0), OWRITE(1) or ORDWR(2). */
#define OBJ_USB_EP_DATA  0
#define OBJ_USB_EP_CTL   1
#define OBJ_USB_MODE_READ  0
#define OBJ_USB_MODE_WRITE 1
#define OBJ_USB_MODE_RDWR  2

/* obj_invoke(ObjOpUsbEpOpen): open one USB endpoint through an authorized
 * USB service Handle.  The service Handle must carry Vcreate, which is
 * granted only by OBJOPEN(ObjOpenUsb) to the trusted eve/bootstrap user and
 * never by the metadata-only default rights.  flags and reserved are zero.
 * The request carries no caller rights mask: the new endpoint Handle's
 * rights are derived from kind and mode.  DATA opens with the existing
 * usbcore_dataopen() semantics; CTL acquires the referenced endpoint without
 * a data open.  The result is a new process-local Handle.  OBJCLOSE (or
 * exec/exit) releases the endpoint exactly once when the last reference is
 * dropped.  Data I/O, USBIOCTL and control transfers remain unimplemented. */
typedef struct ObjUsbEpReq {
	int			devno;
	int			epno;
	int			kind;
	int			mode;
	unsigned int		flags;
	unsigned int		reserved;
} ObjUsbEpReq;

typedef struct ObjUsbEntry {
	int			idx;
	int			devno;
	int			epno;
	int			ttype;
	int			mode;
	int			speed;
	int			state;
	int			inuse;
	int			maxpkt;
	char			name[128];
} ObjUsbEntry;
#define OBJ_USB_ENTRY_SIZE 164

/* Byte-stream bound shared by the native Pipe, UART and KBD providers. */
#define OBJ_STREAM_MAXIO 65536
#define OBJ_KBD_MAXREAD 4096
#define OBJ_UART_STATMAX 65536

/* Pipe inheritance is never armed through OBJINVOKE.  Use the direct
 * RFORKEX(143) call, whose options record declares the crossing Handles and
 * their rights in the same atomic operation as child creation. */

/* obj_invoke(ObjOpKbdLeds): set the shared keyboard LED state through an
 * authorized KBD Handle carrying Vcontrol.  The call always requires the
 * trusted eve/bootstrap user; handle 0 is rejected, so LED state is never a
 * global-handle privilege bypass.  in/out and their lengths must be zero;
 * arg0 carries the LED bits.  The result is 0. */

/*
 * One typed, versioned INVOKE request/response.
 *
 * op selects the provider operation.  rights is reserved: the kernel derives
 * authority from the Handle, never from a caller claim.  in/inlen and
 * out/outlen are validated according to the operation direction.  result is
 * a byte count, a Handle or 0 depending on the operation; aux carries a
 * secondary value (ObjectFIFO envelope length, for example).
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

/* RFORK(19) and OTERMINATE(93) are direct historical syscalls.  RFORK keeps
 * the RFPROC child/PID return and the group-only form; OTERMINATE keeps the
 * direct-child, parentpid, kp and exiting checks.  Child wait is the direct
 * typed OBJWAIT syscall; none of these has an OBJINVOKE variant. */
#define OBJ_WAITMSG_MAX 128

typedef struct ObjWait {
	long long		pid;
	unsigned long long	time[3];	/* user, sys, real (ms) */
	unsigned int		msglen;		/* bytes in msg, no NUL */
	unsigned int		reserved;
	char			msg[OBJ_WAITMSG_MAX];
} ObjWait;

/* obj_invoke(ObjOpOspawn): create one static x86-64 ELF child from an
 * explicit GE/O Space capability and OID.  The provider Handle must be a
 * VkindSpace Handle carrying Vexecute; the same Ohand Oexecute gate and
 * per-Space authorization used by OSPAWN remain in force.  The request is
 * copied from user memory, and argv/options are validated exactly as the
 * former OSPAWN(92) validated them, including the Ospawn and optional
 * Oinherit/Oinheritv blocks.  The child receives the same registers, stack,
 * argv, CPU affinity, memory budget, namespace/view, Pipe, environment and
 * Vhand inheritance semantics.  The result is the child PID. */
typedef struct ObjOspawnReq {
	unsigned long long	oid;
	char			**argv;
	long			argc;
	unsigned int		flags;		/* must be zero */
	unsigned int		reserved;
	struct Ospawn		*options;	/* optional, layout unchanged */
} ObjOspawnReq;

#define OBJ_EXITMSG_MAX 128

/* Process-lifecycle adapters.  Wait is the direct typed OBJWAIT syscall;
 * OSPAWN remains an explicit Space+OID object operation; rfork, oterminate
 * and exits use their direct historical syscalls. */
long obj_wait(ObjWait *w);
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
long obj_stat(int handle, void *stat);
long obj_subscribe(int fifo, unsigned long long rights,
		unsigned long long capacity);
long obj_publish(int fifo, int object, void *env, unsigned long long envlen);
long obj_receive(int subscription, void *env, unsigned long long envcap,
		unsigned long long timeout, unsigned long long *got);
long obj_grant(int handle, unsigned long long rights);
long obj_revoke(int handle);
/* Direct bounded process introspection (OBJLIST), not an INVOKE operation.
 * It reports only the caller's own handle table; it never names another
 * process or copies a capability. */
long obj_list(void *records, unsigned long long count);

/* Native byte-stream providers over the same five-call ABI. */
long obj_pipe_create(ObjId *out);
long obj_pipe_peer(int handle);
/* Discovery helper: counts ObjDeviceUart catalog records.  There is no UART
 * object and no handle-less INVOKE operation. */
long obj_uart_count(void);
long obj_uart_open(int port, int mode);
long obj_uart_ctl(int handle, void *cfg);
long obj_uart_status(int handle, void *buf, unsigned long long len);
long obj_kbd_open(void);
long obj_kbd_leds(int handle, unsigned int leds);

/* USB service provider.  obj_usb_list's count is a record count, not bytes;
 * ents==NULL or count==0 performs the legacy size query.  obj_usb_epopen
 * opens one endpoint through a service Handle carrying Vcreate and returns
 * a new endpoint Handle whose rights are derived from kind and mode. */
long obj_usb_open(unsigned long long rights);
long obj_usb_list(int handle, void *ents, unsigned long long count);
long obj_usb_epopen(int handle, int devno, int epno, int kind, int mode);

#endif /* PLAN2001_ABI_OBJ_H */
