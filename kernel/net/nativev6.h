/*
 * E7.2 native Ethernet-to-IPv6 consumer (opt-in test profile).
 * One fixed-purpose Ipv6Native instance, no generic subscription framework.
 */
typedef struct Ipv6Native Ipv6Native;

enum {
	NvInit = 0,
	NvRunning,
	NvStopping,
	NvStopped,
	NvFailed,
};

struct Ipv6Native {
	Ether		*ether;
	Fs		*f;
	Conv		*conv;
	Ipifc		*ifc;
	Queue		*rxq;
	uchar		mac[Eaddrlen];
	uchar		ll[IPaddrlen];

	Rendez		done;
	Lock		udplock;
	int		udp6active;
 int tcp6active;
	int		state;
	int		stop;
	int		worker_started;
	int		worker_exited;
	int		rxreg;
	int		rxclosed;
	Proc		*worker;

	uvlong		rxframes;
	uvlong		rxbad;
	uvlong		rxother;
	uvlong		rxdrop;
	uvlong		rxdelivered;
	uvlong		txframes;
	uvlong		txcalls;
	uvlong		txheld;
	uvlong		txerrs;
	uvlong		txdown;
};

typedef struct Ipv6NativeStats Ipv6NativeStats;
struct Ipv6NativeStats {
	int	started;
	int	state;
	int	worker_exited;
	uvlong	rxframes;	/* queued by the native RX filter */
	uvlong	rxbad;		/* short/filtered L2 */
	uvlong	rxother;	/* non-IPv6 EtherType */
	uvlong	rxdrop;		/* queue full/closed */
	uvlong	rxdelivered;	/* handed to ipiput6 */
	uvlong	txframes;	/* ethercore_tx accepted */
	uvlong	txcalls;
	uvlong	txheld;
	uvlong	txerrs;		/* native medium TX failure */
	uvlong	txdown;		/* link-down drops */
};

extern int	ipv6native_start(Ether*, char*, Ipv6Native**);
extern int	ipv6native_boot_start(void);
extern Ipv6Native*	ipv6native_current(void);
extern int	ipv6native_stop(Ipv6Native*);
extern int	ipv6native_inject(Ipv6Native*, Block*);
extern void	ipv6native_etherinput(void*, Ether*, Block*);
extern void	ipv6native_stats(Ipv6Native*, Ipv6NativeStats*);
extern void	ipv6native_setlimit(Ipv6Native*, int);
extern uchar*	ipv6native_lladdr(Ipv6Native*);
extern Fs*	ipv6native_fs(Ipv6Native*);
extern Ipifc*	ipv6native_ifc(Ipv6Native*);
