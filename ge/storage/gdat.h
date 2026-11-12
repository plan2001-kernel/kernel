typedef struct Arange	Arange;
typedef struct Arena	Arena;
typedef struct Bfree	Bfree;
typedef struct Blk	Blk;
typedef struct Bptr	Bptr;
typedef struct Bucket	Bucket;
typedef struct Chan	Chan;
typedef struct Dlist	Dlist;
typedef struct Key	Key;
typedef struct Kvp	Kvp;
typedef struct Limbo	Limbo;
typedef struct Msg	Msg;
typedef struct Ospace	Ospace;
typedef struct Osweepmsg	Osweepmsg;
typedef struct Qent	Qent;
typedef struct Scan	Scan;
typedef struct Scanp	Scanp;
typedef struct Syncq	Syncq;
typedef struct Trace	Trace;
typedef struct Tree	Tree;
typedef struct Val	Val;

enum {
	KiB	= 1024ULL,
	MiB	= 1024ULL*KiB,
	GiB	= 1024ULL*MiB,
	TiB	= 1024ULL*GiB,

	Lgblk	= 14,
	Blksz	= (1ULL<<Lgblk),

	Nrefbuf	= 1024,			/* number of ref incs before syncing */
	Nflushtab = 1024,		/* flush table size */
	Nsec	= 1000LL*1000*1000,	/* nanoseconds to the second */
	Maxent	= 256,			/* maximum size of ent key, with terminator */
	Maxname	= Maxent-1-9-1,		/* maximum size of a name element */

	/*
	 * Kpmax must be no more than 1/4 of pivspc, or
	 * there is no way to get a valid split of a
	 * maximally filled tree.
	 */
	Keymax	= Maxent,		/* key data limit */
	Inlmax	= 512,			/* inline data limit */
	Ptrsz	= 24,			/* off, hash, gen */
	Pptrsz	= 26,			/* off, hash, gen, fill */
	Fillsz	= 2,			/* block fill count */
	Offksz	= 17,			/* type, qid, off */
	Snapsz	= 9,			/* tag, snapid */
	Dpfxsz	= 9,			/* directory prefix */
	Upksz	= 9,			/* directory prefix */
	Dlksz	= 1+8+8,		/* tag, death, birth */
	Dlvsz	= Ptrsz+Ptrsz,		/* hd,tl of deadlist */
	Dlkvpsz	= Dlksz+Dlvsz,		/* full size of dlist kvp */
	Treesz	= 4+4+4+4		/* ref, ht, flg, gen, pred, succ, base, root */
		  +8+8+8+8+Ptrsz,
	Kvmax	= Keymax + Inlmax,	/* Key and value */
	Kpmax	= Keymax + Ptrsz,	/* Key and pointer */
	Arenasz	= 8+8+8+8,		/* loghd, loghash, size, used */
	
	Pivhdsz		= 10,
	Leafhdsz	= 6,
	Loghdsz		= 2+2+8+Ptrsz,			/* type, len, hash, chain */
	Rootsz		= 4+Ptrsz,			/* root pointer */
	Pivsz		= Blksz - Pivhdsz,
	Bufspc		= (Blksz - Pivhdsz)/2,		/* pivot room */
	Pivspc		= Blksz - Pivhdsz - Bufspc,
	Logspc		= Blksz - Loghdsz,
	Logslop		= 16+16+8,			/* val, nextb, chain */
	Leafspc 	= Blksz - Leafhdsz,
	Msgmax  	= 1 + (Kvmax > Kpmax ? Kvmax : Kpmax),
	Estacksz	= 64,
	Maxprocs	= 128,
};

enum {
	Eactive	= 1UL<<30,	/* epoch active flag */
};

enum {
	/*
	 * dent: pqid[8] qid[8] -- a directory entry key.
	 * ptr:  off[8] hash[8] gen[8] -- a key for an Dir block.
	 * dir:  legacy serialized dirent
	 */

	/*
	 * Retained on-disk key numbers.  The native O engine uses Kobj/Ko;
	 * the inherited FS pathname layer that used these is gone.
	 */
	Kdat,	/* qid[8] off[8] => ptr:		pointer to data page */
	Kent,	/* pqid[8] name[n] => dir[n]:		serialized Dir */
	Kup,	/* qid[8] => Kent:			parent dir */

	/* snapshot keys */
	Klabel,	/* name[] => snapid[]:			snapshot label */
	Ksnap,	/* sid[8] => ref[8], tree[52]:		snapshot root */
	Kdlist,	/* snap[8] gen[8] => hd[ptr],tl[ptr]	deadlist  */

	/* configuration */
	Kconf,	/* name[] => value[] */

	/* Plan2001: native O object stores (obj.c, docs/O-FORMAT.md) */
	Kobj,	/* [oid[8] ~ver[8] idx[4]] => header or chunk */

	/* Plan2001: native O volume records (docs/O-FORMAT.md) */

	/* Plan2001: immutable per-object owner metadata. */
	Kowner,	/* oid[8] ~ver[8] => magic[8] owner[8] */
};

enum {
	Bdirty	= 1 << 0,
	Bfinal	= 1 << 1,
	Bfreed	= 1 << 2,
	Bcached	= 1 << 3,
	Bqueued	= 1 << 4,
	Blimbo	= 1 << 5,
	Bstatic	= 1 << 6,
};

enum {
	Lmut	= 1 << 0,	/* can we modify snaps via this label */
	_Lauto	= 1 << 1,	/* deprecated: was auto snap */
	_Ltsnap	= 1 << 2,	/* deprecated: was skipping timed snaps */
};

/*
 * Plan2001: the native O volume profile (docs/O-FORMAT.md).  The
 * low byte of fs->flag is the PoC format version; the high bit tells O
 * from a legacy FS volume (whose flag is 0).  O records live in the GE
 * snapshot tree under flat Kobj/Kowner keys.
 */
enum {
	Ovolflag	= 1ULL << 63,
	Ovolvers	= 2,
	Ovolvermask	= 0xff,
};

#define Zb (Bptr){-1, -1, -1}
#define Tmfmt "YYYY.MM.DD[_]hh:mm:ss"

/* internal errors */
//#define Efs	(abort(), "fs broke")
extern char Efs[];
extern char Enoval[];
extern char Ecorrupt[];
extern char Efsvers[];
extern char Eimpl[];
extern char Ebotch[];
extern char Eio[];
extern char Enofid[];
extern char Efid[];
extern char Etype[];
extern char Edscan[];
extern char Esrch[];
extern char Eexist[];
extern char Emode[];
extern char Efull[];
extern char Estuffed[];
extern char Eauth[];
extern char Elength[];
extern char Eperm[];
extern char Einuse[];
extern char Ebadf[];
extern char Ename[];
extern char Enomem[];
extern char Eattach[];
extern char Enosnap[];
extern char Esnap[];
extern char Edir[];
extern char Esyntax[];
extern char Enouser[];
extern char Enogrp[];
extern char Efsize[];
extern char Ebadu[];
extern char Erdonly[];
extern char Elocked[];
extern char Eauthp[];
extern char Eauthd[];
extern char Eauthph[];
extern char Enone[];
extern char Enoauth[];
extern char Ephase[];
extern char Ecdir[];
extern char Ebadctl[];
extern char Enoqid[];
extern char Echeck[];
extern char Eopen[];
extern char Eoffset[];

extern char Esnapu[];
extern char Esnapx[];
extern char Esnapr[];

extern char Ewstatt[];
extern char Ewstatb[];
extern char Ewstatd[];
extern char Ewstatg[];
extern char Ewstatl[];
extern char Ewstatm[];
extern char Ewstato[];
extern char Ewstatq[];
extern char Ewstatu[];
extern char Enempty[];

/*
 * All metadata blocks share a common header:
 * 
 *	type[2]
 *
 * The None type is reserved for file data blocks
 * and refcount blocks.
 *
 * The superblock has this layout:
 *	version[8]	always "gefsNNNNN"
 *	blksz[4]	block size in bytes
 *	bufsz[4]	portion of leaf nodes
 *			allocated to buffers,
 *			in bytes
 *	height[4]	tree height of root node
 *	rootb[8]	address of root in last
 *			snapshot.
 *	rooth[8]	hash of root node
 *	narena[4]	number of arenas in tree
 *	flag[8]	feature flag
 *	gen[8]		The flush generation
 *
 * The arena zone blocks have this layout, and
 * are overwritten in place:
 *
 *	log[8]		The head of the alloc log
 *	logh[8]		The hash of the alloc log
 *
 * The log blocks have this layout, and are one of
 * two types of blocks that get overwritten in place:
 *
 *	hash[8]		The hash of the previous log block
 *
 *	The remainder of the block is filled with log
 *	entries. Each log entry has at least 8 bytes
 *	of entry. Some are longer. The opcode is or'ed
 *	into the low order bits of the first vlong.
 *	These ops take the following form:
 *
 *	Alloc, Free:
 *		off[8] len[8]
 *	Alloc1, Free1:
 *		off[8]
 *	Ref:
 *		off[8]
 *	Flush:	
 *		gen[8]
 *
 * Pivots have the following layout:
 *
 *	nval[2]
 *	valsz[2]
 *	nbuf[2]
 *	bufsz[2]
 *
 * Leaves have the following layout:
 *
 *	nval[2]
 *	valsz[2]
 *	pad[4]sure, 
 *
 * Within these nodes, pointers have the following
 * layout:
 *
 *	off[8] hash[8] fill[2]
 */
enum {
	Tdat,
	Tpivot,
	Tleaf,
	Tlog,
	Tdlist,
	Tarena,
	Tsuper = 0x6765,	/* 'ge' bigendian */
};

enum {
	Vinl,	/* Inline value */
	Vref,	/* Block pointer */
};

enum {
	GBraw	= 1<<0,
	GBwrite	= 1<<1,
	GBnochk	= 1<<2,
	GBtry	= 1<<3,
};

enum {
	Onop,		/* nothing */
	Oinsert,	/* new kvp */
	Odelete,	/* delete kvp */
	Oclearb,	/* free block ptr if exists */
	Oclobber,	/* remove file if it exists */
	Owstat,		/* update kvp dirent */
	Orelink,	/* rechain forwards */
	Oreprev,	/* rechain backwards */
	Oincref,	/* adjust refs on snap */
	Nmsgtype,	/* maximum message type */
};

enum {
	Magic = 0x979b929e98969c8c,
};

/*
 * Wstat ops come with associated data, in the order
 * of the bit flag.
 */
enum{
	/* wstat flag */
	Owsize	= 1<<0,	/* [8]fsize: update file size */
	Owmode	= 1<<1,	/* [4]mode: update file mode */
	Owmtime	= 1<<2, /* [8]mtime: update mtime, in nsec */
	Owatime	= 1<<3, /* [8]atime: update atime, in nsec */
	Owuid	= 1<<4,	/* [4]uid: set uid */
	Owgid	= 1<<5,	/* [4]uid: set gid */
	Owmuid	= 1<<6,	/* [4]uid: set muid */
	Owqpath = 1<<7, /* [8]qpath: set qid.path */
};

/*
 * Operations for the allocation log.
 */
enum {
	LogNop,		/* unused */
	/* 1-wide entries */
	LogAlloc1,	/* alloc a block */
	LogFree1,	/* free a block */
	LogSync,	/* sync barrier for replay */

	/* 2-wide entries */
#define	Log2wide	LogAlloc
	LogAlloc,	/* alloc a range */
	LogFree,	/* free a range */
};

enum {
	DFblk,
	DFbp,
	DFtree,
	DFclose,
	DFdlist,
};

struct Limbo {
	Gefs	*fs;	/* Plan2001: the GE volume the object belongs to */
	Limbo	*next;
	int	op;
};

/*
 * Plan2001: a volume's kernel glue (sys/src/9/port/gefs/kern.c).  It was
 * kern.c's one global `g': the device, connection and console file
 * descriptors, the disk's raw file for cache flushes, and the halt state.
 * The native engine keeps only its flush and halt state; block I/O goes
 * through the volume's native callback (Gefs.oblkrw), not a Chan.
 */
typedef struct Gkern	Gkern;

struct Gkern {
	int	flushwarned;
	ulong	nflush;
	int	halting;
	int	halted;
};

struct Bptr {
	vlong	addr;
	uvlong	hash;
	vlong	gen;
};

struct Key {
	char	*k;
	int	nk;
};

struct Val {
	short	nv;
	char	*v;
};

struct Kvp {
	Key	key;
	Val	val;
};

struct Msg {
	char	op;
	Kvp	kv;
};

struct Dlist {
	Limbo	limbo;
	Dlist	*cnext;	/* cache next entry */
	Dlist	*cprev;	/* cache prev entry */
	Dlist	*chain;	/* hash table chain */
	Blk	*ins;	/* loaded head */

	vlong	gen;	/* deadlist gen */
	vlong	bgen;	/* birth gen */
	Bptr	hd;	/* deadlist head */
	Bptr	tl;	/* deadlist tail */
};

struct Arange {
	Avl	avl;
	vlong	off;
	vlong	len;
};

struct Bucket {
	Blk	*b;
};

/*
 * Plan2001: the native O sweep/halt queue.  It is private to the native
 * O profile and carries no legacy filesystem payload.
 */
enum {
	OsweepSync,
	OsweepHalt,
};

struct Osweepmsg {
	int	op;
};

struct Tree {
	Limbo	limbo;

	/* in-memory */
	Lock	lk;
	Along	memref;	/* number of in-memory references to this */
	vlong	memgen;	/* wip next generation */
	int	dirty;

	/* on-disk */
	int	nref;	/* number of forks */
	int	nlbl;	/* number of labels */
	int	ht;	/* height of the tree */
	uint	flag;	/* flag set */
	Bptr	bp;	/* block pointer of root */
	vlong	gen;	/* generation */
	vlong	pred;	/* previous snapshot */
	vlong	succ;	/* next snapshot */
	vlong	base;	/* base snapshot */
};

struct Bfree {
	Limbo	limbo;
	Bptr bp;
};

enum {
	/* in priority order */
	Qnone,
	Qfence,
	Qwrite,
	Qfree,
};

struct Qent {
	vlong	qgen;
	Bptr	bp;
	Blk	*b;
	int	op;
};

struct Syncq {
	Gefs	*fs;
	QLock	lk;
	Rendez	fullrz;
	Rendez	emptyrz;
	Qent	*heap;
	int	nheap;
	int	heapsz;
};

struct Trace {
	int	tid;
	int	qgen;
	char	msg[16];
	Bptr	bp;
	vlong	v0;
	vlong	v1;
};

/*
 * Overall state of a GE volume.
 * Shadows the superblock contents.
 */
struct Gefs {
	int	blksz;
	int	bufspc;
	Tree	snap;
	/*
	 * when freeing blocks, there are 3 places that
	 * the dirty blocks may come from; we can get them
	 * from freeing blocks when updating the snap tree,
	 * from freeing dlists, or from dropping dlists.
	 * 
	 * we can't reuse the blocks until after the superblock
	 * points at a new tree that doesn't include them, so
	 * this is where we stash the blocks until the sync()
	 * call completes.
	 */
	Dlist	snapdl;		/* on disk */
	Dlist	dropdl;		/* in mem */
	int	narena;
	vlong	flag;
	vlong	nextqid;	/* protected by mutlk */
	vlong	nextgen;	/* protected by mutlk */
	Avlong	qgen;
	Bptr	*arenabp;

	/* superblocks */
	Blk	*sb0;	/* primary */
	Blk	*sb1;	/* backup */

	/* arena allocation */
	Arena	*arenas;
	Along	roundrobin;
	long	syncing;
	long	nsyncers;

	QLock	syncserial;	/* full sync admission, held across syncrz sleep */
	QLock	synclk;
	Rendez	syncrz;

	Chan	*osweepchan;

	QLock	mutlk;
	Along	nworker;

	Along	epoch;
	Along	lepoch[32];
	Aptr	limbo[3]; /* Limbo* */
	Along	nlimbo;
	Lock	epochlk;

	Syncq	syncq[32];

	Along	rdonly;
	int	noauth;

	/* Plan2001: the pathless native block provider (sysgefs.c). */
	void	*oblk;
	vlong	oblksize;
	long	(*oblkrw)(void*, void*, long, uvlong, int);
	int	(*oblkflush)(void*);

	/* slow block io */
	QLock	blklk[32];
	
	/* deadlist cache */
	Dlist	**dlcache;
	Dlist	*dlhead;
	Dlist	*dltail;
	int	dlcount;
	int	dlcmax;

	/* block lru */
	QLock	lrulk;
	Rendez	lrurz;
	Bucket	*bcache;
	Blk	*chead;
	Blk	*ctail;
	usize	ccount;
	usize	cmax;

	/* preallocated deferred frees */
	QLock	bfreelk;
	Rendez	bfreerz;
	Bfree	*bfree;

	RWLock	flushq[Nflushtab];

	Trace	*trace;
	Along	traceidx;
	long	ntrace;

	/*
	 * Plan2001: the parts of a volume that used to be one global in
	 * kern.c, blk.c, obj.c and check.c: its kernel glue, its scratch
	 * block, the processes that may take its reserve, its object
	 * transactions, and the state of a check that is running.
	 */
	int	id;		/* this instance's number, for names */
	Gkern	k;
	Proc	*syncproc;	/* the syncing process: it may take the reserve */
	Proc	*sweepproc;
	void	*otxs;		/* obj.c's transactions, under mutlk */
	void	*ck;		/* check.c's state while it runs */
};

struct Arena {
	QLock	qlk;
	Gefs	*fs;
	Avltree *free;
	Blk	**queue;
	int	nqueue;
	Blk	*logbuf[2];	/* preallocated log pages */
	Blk	*h0;		/* arena header */
	Blk	*h1;		/* arena footer */
	Blk	**q;		/* write queue */
	vlong	nq;
	vlong	size;
	vlong	used;
	vlong	reserve;
	/* allocation log */
	vlong	lastlogsz;	/* size after last compression */
	vlong	nlog;		/* number of blocks in log */
	Bptr	loghd;		/* allocation log */
	Blk	*logtl;		/* end of the log, open for writing */
	Syncq	*sync;
};

/*
 * Flat O-volume transaction context, without SpaceID.
 * OID allocation, versioning and writer exclusivity are volume-scoped.
 */
struct Ospace {
	Gefs	*fs;
	QLock	lk;
	void	*tx;		/* obj.c's Otx, while the handle has one */
};

enum {
	POmod,
	POrot,
	POsplit,
	POmerge,
};

struct Scanp {
	int	bi;
	int	vi;
	Blk	*b;
};

struct Scan {
	Gefs	*fs;	/* Plan2001: the volume its tree belongs to */
	vlong	offset;	/* last read offset */
	char	first;
	char	donescan;
	char	overflow;
	char	present;
	int	ht;
	Kvp	kv;
	Key	pfx;
	char	kvbuf[Kvmax];
	char	pfxbuf[Keymax];
	Scanp	*path;
};

struct Blk {
	Limbo	limbo;
	/* cache entry */
	Blk	*cnext;
	Blk	*cprev;
	Blk	*hnext;

	/* serialized to disk in header */
	short	type;	/* @0, for all */
	union {
		struct {
			short	nval;	/* @2, for Leaf, Pivot: data[0:2] */
			short	valsz;	/* @4, for Leaf, Pivot: data[2:4] */
			short   nbuf;	/* @6, for Pivot */
			short   bufsz;	/* @8, for Pivot */
		};
		struct {
			int	logsz;	/* @2 for allocation log */
			uvlong	logh;	/* @4 for log body hash */
			Bptr	logp;	/* @12 next deadlist chain */
		};
	};

	/* debug */
	uintptr queued;
	uintptr lasthold;
	uintptr lasthold0;
	uintptr lastdrop;
	uintptr	enqueued;
	uintptr cached;
	uintptr uncached;
	uintptr	alloced;
	uintptr	freed;

	Bptr	bp;
	Along	ref;
	Along	flag;
	char	*data;
	char	buf[Blksz];
	vlong	magic;
};

/* Plan2001: a queue of size messages, with the kernel's locks */
struct Chan {
	QLock	lk;
	Rendez	fullrz;	/* lk; a sender waits for room */
	Rendez	emptyrz;	/* lk; a receiver waits for one */
	int	size;	/* size of queue */
	int	n;	/* in it */
	void	**rp;
	void	**wp;
	void*	args[];	/* list of saved pointers, [->size] */
};
