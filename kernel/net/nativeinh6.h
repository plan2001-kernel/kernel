/*
 * E7.4 experimental native IPv6 Next Header consumer (opt-in test profile).
 *
 * One fixed-protocol endpoint on the inherited Fs/Proto demultiplexer.  This
 * is kernel C only: no Chan, no userspace raw-packet ABI and no tunnel.
 * Next Header 253 is an RFC 3692 experiment selector, NOT WireGuard.
 */
typedef struct NativeIp6 NativeIp6;
typedef struct NativeIp6Msg NativeIp6Msg;
typedef struct NativeIp6Stats NativeIp6Stats;
typedef int (*NativeIp6Rx)(void*, NativeIp6Msg*);

enum {
	NativeIp6Proto	= 253,	/* experimental, test only */
};

enum {
	NativeIp6Ok	= 0,
	NativeIp6ErrBadArg	= -1,
	NativeIp6ErrNoNet	= -2,
	NativeIp6ErrBusy	= -3,
	NativeIp6ErrNotReg	= -4,
	NativeIp6ErrTooBig	= -5,
	NativeIp6ErrFamily	= -6,
	NativeIp6ErrSend	= -7,
	NativeIp6ErrNoRoute	= -8,
	NativeIp6ErrNoMem	= -9,
};

/*
 * One complete IPv6 Next Header payload.  src/dst/ifc/proto are metadata
 * copied from the validated IPv6 header.  bp->rp is the first payload byte
 * and bp is a single Block.  The callback owns bp on entry and must consume
 * it (nativeip6_send on success) or free it; the return value only reports a
 * semantic rejection for the counters.
 *
 * The admitted callback runs with the rx/arg pair captured at admission.
 * The caller may free arg only after nativeip6_unregister() returns
 * NativeIp6Ok; after NativeIp6ErrBusy the endpoint is still DRAINING and
 * the old arg may still be used by admitted callbacks.
 */
struct NativeIp6Msg {
	uchar	src[IPaddrlen];
	uchar	dst[IPaddrlen];
	Ipifc	*ifc;
	uchar	proto;
	Block	*bp;
};

struct NativeIp6Stats {
	int	registered;	/* true only in REGISTERED */
	int	draining;	/* true in DRAINING; Busy leaves this latched */
	int	active;		/* admitted callbacks not yet returned */
	uvlong	rxframes;
	uvlong	rxbytes;
	uvlong	rxreject;
	uvlong	rxbad;
	uvlong	txaccepted;
	uvlong	txerrors;
	uvlong	txbytes;
};

/*
 * Lifecycle: UNREGISTERED -> REGISTERED -> DRAINING -> UNREGISTERED.
 * register returns Busy while REGISTERED or DRAINING.  unregister returns
 * Busy while another caller owns teardown or callbacks are still active;
 * a later unregister retries finalization.  Only a completed unregister
 * (Ok) permits re-registration and old-arg release.  NotReg means the
 * unregistration is already complete.
 */
extern int	nativeip6_register(Ipv6Native*, NativeIp6Rx, void*);
extern int	nativeip6_unregister(Ipv6Native*);
extern int	nativeip6_send(Ipv6Native*, uchar*, uchar, Block*, int);
extern int	nativeip6_registered(Ipv6Native*);
extern void	nativeip6_setdelay(Ipv6Native*, int);
extern void	nativeip6_stats(Ipv6Native*, NativeIp6Stats*);
