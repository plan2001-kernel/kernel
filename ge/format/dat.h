typedef struct Arange	Arange;
typedef struct Arena	Arena;
typedef struct Blk	Blk;
typedef struct Bptr	Bptr;
typedef struct Bucket	Bucket;
typedef struct Dlist	Dlist;
typedef struct Errctx	Errctx;
typedef struct Gefs	Gefs;
typedef struct Key	Key;
typedef struct Kvp	Kvp;
typedef struct Trace	Trace;
typedef struct Tree	Tree;
typedef struct Val	Val;

enum {
	KiB	= 1024ULL,
	MiB	= 1024ULL*KiB,
	GiB	= 1024ULL*MiB,

	Lgblk	= 14,
	Blksz	= (1ULL<<Lgblk),

	Nsec	= 1000LL*1000*1000,	/* nanoseconds to the second */
	Maxent	= 256,			/* maximum size of ent key, with terminator */

	Keymax	= Maxent,		/* key data limit */
	Inlmax	= 512,			/* inline data limit */
	Ptrsz	= 24,			/* off, hash, gen */
	Arenasz	= 8+8+8+8,		/* loghd, loghash, size, used */

	Pivhdsz		= 10,
	Leafhdsz	= 6,
	Loghdsz		= 2+2+8+Ptrsz,			/* type, len, hash, chain */
	Bufspc		= (Blksz - Pivhdsz)/2,		/* pivot room */
	Pivspc		= Blksz - Pivhdsz - Bufspc,
	Logspc		= Blksz - Loghdsz,
	Logslop		= 16+16+8,			/* val, nextb, chain */
	Leafspc 	= Blksz - Leafhdsz,
	Estacksz	= 64,
};

enum {
	/*
	 * dent: pqid[8] qid[8] -- a directory entry key.
	 * ptr:  off[8] hash[8] gen[8] -- a key for a directory block.
	 * dir:  serialized directory
	 */

	/* fs keys */
	Kdat,	/* qid[8] off[8] => ptr:		pointer to data page */
	Kent,	/* pqid[8] name[n] => dir[n]:		serialized Dir */
	Kup,	/* qid[8] => Kent:			parent dir */

	/* snapshot keys */
	Klabel,	/* name[] => snapid[]:			snapshot label */
	Ksnap,	/* sid[8] => ref[8], tree[52]:		snapshot root */
	Kdlist,	/* snap[8] gen[8] => hd[ptr],tl[ptr]	deadlist  */

	/* configuration */
	Kconf,	/* name[] => value[] */

	/* Plan2001: object stores (the kernel's ge/storage/obj.c) */
	Kobj,	/* store[8] [oid[8] ~ver[8] idx[4]] => header or chunk */

	/* Plan2001: native O volume records (docs/O-FORMAT.md) */
	Ko,	/* name[] => value[]: catalog and volume records */

	/* Plan2001: immutable per-object owner metadata. */
	Kowner,	/* store[8] oid[8] ~ver[8] => magic[8] owner[8] */
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

/*
 * Plan2001: the native O volume profile (docs/O-FORMAT.md).  The
 * low byte of fs->flag is the PoC format version; the high bit tells O
 * from a legacy FS volume (whose flag is 0).  O records live in the GE
 * snapshot tree under the Ko key type.
 */
enum {
	Ovolflag	= 1ULL << 63,
	Ovolvers	= 1,
	Ovolvermask	= 0xff,
	OspaceInit	= 1ULL,
};

#define Zb (Bptr){-1, -1, -1}

/* internal errors */
extern char Efs[];
extern char Ecorrupt[];
extern char Eio[];
extern char Efull[];
extern char Estuffed[];
extern char Enomem[];

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
	GBraw	= 1<<0,
	GBwrite	= 1<<1,
	GBnochk	= 1<<2,
	GBtry	= 1<<3,
};

enum {
	Magic = 0x979b929e98969c8c,
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

struct Dlist {
	Bptr	hd;	/* deadlist head */
	Bptr	tl;	/* deadlist tail */
};

struct Errctx {
	long	tid;
	char	err[128];
	jmp_buf	errlab[Estacksz];
	int	nerrlab;
};

struct Arange {
	Avl	avl;
	vlong	off;
	vlong	len;
};

struct Bucket {
	Blk	*b;
};

struct Tree {
	/* in-memory */
	Lock	lk;
	vlong	memgen;	/* wip next generation */

	/* on-disk */
	int	ht;	/* height of the tree */
	Bptr	bp;	/* block pointer of root */
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
 * Overall state of the O volume.
 * Shadows the superblock contents.
 */
struct Gefs {
	Tree	snap;
	Dlist	snapdl;		/* on disk */
	int	narena;
	vlong	flag;
	vlong	nextqid;	/* protected by mutlk */
	vlong	nextgen;	/* protected by mutlk */
	Avlong	qgen;
	Bptr	*arenabp;

	int	fd;
	Along	rdonly;

	/* arena allocation */
	Arena	*arenas;
	Along	roundrobin;

	/* block lru */
	QLock	lrulk;
	Rendez	lrurz;
	Bucket	*bcache;
	Blk	*chead;
	Blk	*ctail;
	usize	cmax;

	/* slow block io */
	QLock	blklk[32];

	Trace	*trace;
};

struct Arena {
	QLock	qlk;
	Avltree *free;
	Blk	*logbuf[2];	/* preallocated log pages */
	Blk	*h0;		/* arena header */
	Blk	*h1;		/* arena footer */
	vlong	size;
	vlong	used;
	vlong	reserve;
	/* allocation log */
	vlong	nlog;		/* number of blocks in log */
	Bptr	loghd;		/* allocation log */
	Blk	*logtl;		/* end of the log, open for writing */
};

struct Blk {
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
	uintptr lasthold;
	uintptr lasthold0;
	uintptr lastdrop;
	uintptr cached;
	uintptr uncached;
	uintptr alloced;
	uintptr freed;

	Bptr	bp;
	Along	ref;
	Along	flag;
	char	*data;
	char	buf[Blksz];
	vlong	magic;
};
