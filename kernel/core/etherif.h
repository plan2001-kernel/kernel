enum {
	MaxEther	= 64,
	Ntypes		= 8,
};

typedef struct	DMAT	DMAT;
struct DMTE
{
	uchar	ip[16];
	uchar	mac[Eaddrlen];
	uchar	valid;
};

typedef struct	DMTE	DMTE;
struct DMAT
{
	DMTE	tab[127];	/* prime */
	uvlong	map;
};

typedef struct Macent Macent;
struct Macent
{
	uchar	ea[Eaddrlen];
	ushort	port;
};

typedef struct Ether Ether;

/*
 * E7.1 native Ethernet C core boundary.
 *
 * The RX sink receives a private copy of a complete L2 frame and owns it.
 * It runs in the driver's RX context (VirtIO: rxproc; some inherited
 * drivers: interrupt), so it must not sleep or take a blocking lock.
 */
typedef void (*EtherRxSink)(void*, Ether*, Block*);

typedef struct EtherCoreStats EtherCoreStats;
struct EtherCoreStats {
	int	attached;
	int	registered;
	uvlong	txpackets;
	uvlong	rxpackets;
	uvlong	rxdropped;
};

enum {
	EtherErrOk	= 0,
	EtherErrNoMem	= -1,
	EtherErrNotReady= -2,
	EtherErrNoDevice= -3,
	EtherErrBadLen	= -4,
	EtherErrDown	= -5,
	EtherErrQueue	= -6,
	EtherErrBusy	= -7,
	EtherErrClosed	= -8,
	EtherErrBadArg	= -9,
	EtherErrPreinit	= -10,
};

struct Ether {
	ISAConf isa;			/* hardware info */
	int	tbdf;			/* type+busno+devno+funcno */

	int	ctlrno;
	int	minmtu;
	int 	maxmtu;

	void	(*attach)(Ether*);	/* filled in by reset routine */
	void	(*transmit)(Ether*);
	long 	(*ctl)(Ether*, void*, long); /* custom ctl messages */
	void	(*power)(Ether*, int);	/* power on/off */
	void	(*shutdown)(Ether*);	/* shutdown hardware before reboot */
	void	*ctlr;

	Queue*	oq;

	Netif netif;

	uchar	ea[Eaddrlen];
	Macent	mactab[127];		/* for bridge */

	DMAT*	dmat;
};

extern void ethersetspeed(Ether*, int);
extern void ethersetlink(Ether*, int);
extern void etheriq(Ether*, Block*);
extern void addethercard(char*, int(*)(Ether*));
extern ulong ethercrc(uchar*, int);
extern int parseether(uchar*, char*);

/* E7.1 native core */
extern int ethercore_init(void);
extern int ethercore_initcalls(void);
extern int ethercore_probecalls(void);
extern int ethercore_ncontroller(void);
extern int ethercore_oqsize(Ether*);
extern Ether* ethercore_lookup(int);
extern Ether* ethercore_byindex(int);
extern Ether* ethercore_byname(char*);
extern Ether* ethercore_probe_at(int, char*);
extern int ethercore_attach(Ether*);
extern int ethercore_tx(Ether*, Block*);
extern int ethercore_rxregister(Ether*, EtherRxSink, void*);
extern int ethercore_rxunregister(Ether*, EtherRxSink, void*);
extern void ethercore_stats(Ether*, EtherCoreStats*);
extern void ethercore_shutdown(void);

/* implemented by the retained legacy #l adapter (devether.c) */
extern void etherlegacyrx(Ether*, Block*);

/*
 * Optional legacy-adapter hooks.  A profile without devether.o leaves both
 * nil: the native core then frees the original RX frame and performs the
 * minimal Netif initialization itself.
 */
extern void (*etherlegacyrxhook)(Ether*, Block*);
extern void (*ethernetifinithook)(Netif*, char*, int, int);

#define NEXT(x, l)	(((x)+1)%(l))
#define PREV(x, l)	(((x) == 0) ? (l)-1: (x)-1)
#define	HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))
