typedef struct Etherpkt	Etherpkt;
typedef struct Netaddr	Netaddr;
typedef struct Netfile	Netfile;
typedef struct Netif	Netif;

enum
{
	Nmaxaddr=	64,
	Nmhash=		31,
};

/*
 *  one per multiplexed connection
 */
struct Netfile
{
	int	inuse;
	ulong	mode;
	char	owner[KNAMELEN];

	int	type;			/* multiplexor type */

	char	prom;			/* promiscuous mode */
	char	scan;			/* base station scanning interval */
	char	bridge;			/* bridge mode */
	char	bypass;			/* bypass transmission */
	char	headersonly;		/* headers only - no data */

	uchar	maddr[8];		/* bitmask of multicast addresses requested */
	int	nmaddr;			/* number of multicast addresses */

	Queue	*in;			/* input buffer */
};

/*
 *  a network address
 */
struct Netaddr
{
	Netaddr	*next;		/* allocation chain */
	Netaddr	*hnext;
	uchar	addr[Nmaxaddr];
	int	ref;
};

/*
 *  a network interface
 */
struct Netif
{
	QLock qlk;

	/* multiplexing */
	char	name[KNAMELEN];		/* interface name */
	int	nfile;			/* max number of Netfiles */
	Netfile	**f;
	Netfile	*bypass;

	/* about net */
	int	limit;			/* flow control */
	int	alen;			/* address length */
	int	mbps;			/* megabits per sec */
	int	link;			/* link status */
	uchar	addr[Nmaxaddr];
	uchar	bcast[Nmaxaddr];
	Netaddr	*maddr;			/* known multicast addresses */
	int	nmaddr;			/* number of known multicast addresses */
	Netaddr *mhash[Nmhash];		/* hash table of multicast addresses */
	int	prom;			/* number of promiscuous opens */
	int	scan;			/* number of base station scanners */
	int	all;			/* number of -1 multiplexors */

	/* statistics */
	int	misses;
	uvlong	inpackets;
	uvlong	outpackets;
	int	crcs;		/* input crc errors */
	int	oerrs;		/* output errors */
	int	frames;		/* framing errors */
	int	overflows;	/* packet overflows */
	int	buffs;		/* buffering errors */
	int	soverflows;	/* software overflow */

	/* routines for touching the hardware */
	void	*arg;
	void	(*promiscuous)(void*, int);
	void	(*multicast)(void*, uchar*, int);
	void	(*scanbs)(void*, uint);	/* scan for base stations */
	char*	(*ifstat)(void*, char*, char*);
};

/*
 *  Ethernet specific
 */
enum
{
	Eaddrlen=	6,
	ETHERMINTU =	60,		/* minimum transmit size */
	ETHERMAXTU =	1514,		/* maximum transmit size */
	ETHERHDRSIZE =	14,		/* size of an ethernet header */

	/* ethernet packet types */
	ETARP		= 0x0806,
	ETIP4		= 0x0800,
	ETIP6		= 0x86DD,
};

struct Etherpkt
{
	uchar	d[Eaddrlen];
	uchar	s[Eaddrlen];
	uchar	type[2];
	uchar	data[1500];
};
