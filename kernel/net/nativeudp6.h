/*
 * E7.3 native UDP6 endpoint: a small kernel C capability backed by the
 * inherited UDP Proto/Conv and its headers=7 datagram metadata mode.
 * Not a userspace socket, fd, Handle or file namespace object.
 */
typedef struct Udp6Endpoint Udp6Endpoint;
typedef struct Udp6Wait Udp6Wait;
typedef struct Udp6Msg Udp6Msg;
typedef struct Udp6Stats Udp6Stats;

enum {
	Udp6Ok = 0,
	Udp6ErrBadArg = -1,
	Udp6ErrNoNet = -2,
	Udp6ErrNoProto = -3,
	Udp6ErrFamily = -4,
	Udp6ErrBind = -5,
	Udp6ErrInUse = -6,
	Udp6ErrClosed = -7,
	Udp6ErrNotBound = -8,
	Udp6ErrTooBig = -9,
	Udp6ErrNoMem = -10,
	Udp6ErrTimeout = -11,
	Udp6ErrBusy = -12,
	Udp6ErrNoRoute = -13,
	Udp6ErrSend = -14,
	Udp6ErrDestroyed = -15,
};

enum {
	U6Open = 0,
	U6Closing,
	U6Closed,
	U6Destroyed,
};

struct Udp6Msg {
	uchar	src[IPaddrlen];
	ushort	sport;
	uchar	dst[IPaddrlen];
	ushort	dport;
	Block	*bp;		/* caller-owned payload, one complete datagram */
};

struct Udp6Endpoint {
	Ipv6Native	*net;
	Fs		*f;
	Proto		*udp;
	Conv		*conv;
	uchar		laddr[IPaddrlen];
	ushort		lport;

	QLock		sendlk;		/* serializes sends; close waits on sendactive */
	Lock		st;		/* state, in-flight counts, counters */
	Udp6Wait	*waiters;
	int		state;
	int		recvactive;
	int		sendactive;
	int		auxactive;
	int		closer;		/* one close owns the CLOSING->CLOSED step */
	int		netcounted;	/* net->udp6active reference */

	uvlong		rxmsgs;
	uvlong		rxbytes;
	uvlong		txaccepted;
	uvlong		txerrors;
	uvlong		txuncertain;
};

struct Udp6Wait {
	Rendez		r;
	Udp6Wait	*next;
	Udp6Endpoint	*ep;
	int		woken;
};

struct Udp6Stats {
	int	bound;
	int	closed;
	int	state;
	int	recvactive;
	int	sendactive;
	uvlong	rxmsgs;
	uvlong	rxbytes;
	uvlong	txaccepted;
	uvlong	txerrors;
	uvlong	txuncertain;
	int	pending;
	int	convstate;
};

typedef struct Udp6AllocStats Udp6AllocStats;
struct Udp6AllocStats {
	uvlong	opens;
	uvlong	closes;
	uvlong	destroys;
	uvlong	live;
	uvlong	peak;
};

extern int	udp6_open(Ipv6Native*, uchar*, ushort, Udp6Endpoint**);
extern int	udp6_recv(Udp6Endpoint*, Udp6Msg*, ulong);
extern int	udp6_sendto(Udp6Endpoint*, uchar*, ushort, Block*);
extern int	udp6_close(Udp6Endpoint*);
extern int	udp6_destroy(Udp6Endpoint**);
extern void	udp6_stats(Udp6Endpoint*, Udp6Stats*);
extern void	udp6_allocstats(Udp6AllocStats*);
extern int	udp6_setlimit(Udp6Endpoint*, int);
extern int	udp6_pending(Udp6Endpoint*);
