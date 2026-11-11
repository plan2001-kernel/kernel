#pragma varargck type "N"	Msg*	/* Plan2001: %M is the kernel's (ip) */
#pragma varargck type "P"	Kvp*
#pragma varargck type "K"	Key*
#pragma varargck type "V"	Val*
#pragma varargck type "B"	Bptr
#pragma varargck type "R"	Arange*
#pragma varargck type "X"	char*
#pragma varargck type "J"	Qid	/* Plan2001: %Q is the kernel's (quote) */

extern int	debug;
extern int	permissive;
extern int	usereserve;
extern char*	reamuser;
extern int	noneid;
extern int	nogroupid;
extern int	admid;

/*
 * GCC promotes unsigned char to signed int.  A byte >= 0x80 in a high
 * position would then sign-extend when widened to u64int and corrupt GE/O
 * hashes/pointers.  Make every byte explicitly unsigned.
 */
#define	UNPACK8(p)	((u32int)((uchar*)(p))[0])
#define	UNPACK16(p)	(((u64int)((uchar*)(p))[0]<<8)|\
			 ((u64int)((uchar*)(p))[1]))
#define	UNPACK32(p)	(((u64int)((uchar*)(p))[0]<<24)|\
			 ((u64int)((uchar*)(p))[1]<<16)|\
			 ((u64int)((uchar*)(p))[2]<<8)|\
			 ((u64int)((uchar*)(p))[3]))
#define	UNPACK64(p)	(((u64int)((uchar*)(p))[0]<<56)|\
			 ((u64int)((uchar*)(p))[1]<<48)|\
			 ((u64int)((uchar*)(p))[2]<<40)|\
			 ((u64int)((uchar*)(p))[3]<<32)|\
			 ((u64int)((uchar*)(p))[4]<<24)|\
			 ((u64int)((uchar*)(p))[5]<<16)|\
			 ((u64int)((uchar*)(p))[6]<<8)|\
			 ((u64int)((uchar*)(p))[7]))

#define	PACK8(p,v)	do{(p)[0]=(v);}while(0)
#define	PACK16(p,v)	do{(p)[0]=(v)>>8;(p)[1]=(v);}while(0)
#define	PACK32(p,v)	do{(p)[0]=(v)>>24;(p)[1]=(v)>>16;(p)[2]=(v)>>8;(p)[3]=(v);}while(0)
#define	PACK64(p,v)	do{(p)[0]=(v)>>56;(p)[1]=(v)>>48;(p)[2]=(v)>>40;(p)[3]=(v)>>32;\
			   (p)[4]=(v)>>24;(p)[5]=(v)>>16;(p)[6]=(v)>>8;(p)[7]=(v);}while(0)

void*	emalloc(usize, int);
int	dataroom(Gefs*, vlong);
Blk*	newblk(Tree*, int);
Blk*	dupblk(Tree*, Blk*);
Blk*	getroot(Tree*, int*);
Blk*	getblk(Gefs*, Bptr, int);
Blk*	holdblk(Blk*);
void	dropblk(Blk*);

void	lrutop(Blk*);
void	lrubot(Blk*);
void	cacheins(Blk*);
void	gcachedel(Gefs*, vlong);
Blk*	cacheget(Gefs*, vlong);
Blk*	cachepluck(Gefs*);

void	qinit(Gefs*, Syncq*);
void	qput(Syncq*, Qent);

Arena*	getarena(Gefs*, vlong);
void	syncblk(Blk*);
void	enqueue(Blk*);
void	epochstart(Gefs*, int);
void	epochend(Gefs*, int);
int	epochclean(Gefs*);
void	limbo(int op, Limbo*);
void	freeblk(Gefs*, Tree*, Blk*);
void	freebp(Gefs*, Tree*, Bptr);
int	logbarrier(Arena *, vlong);
void	killblk(Gefs*, Tree*, Bptr);
ushort	blkfill(Blk*);
uvlong	blkhash(Blk*);
uvlong	bufhash(void*, usize);
u32int	ihash(uvlong);
void	finalize(Blk*);

Tree*	updatesnap(Tree*, char*, int);
void	tagsnap(Tree*, char*, int);
int	delsnap(Tree*, vlong, char*);
void	freedl(Dlist*, int);
Tree*	opentree(Gefs*, vlong);
Tree*	opensnap(Gefs*, char*, int*);

void	closesnap(Tree*);
void	loadarena(Arena*, Bptr);
void	loadge(Gefs*, char*);
void*	geopenblk(char*, void*, vlong, vlong, int,
		long (*)(void*, void*, long, uvlong, int), int (*)(void*));
void	gestart(void*);
void	gesync(Gefs*, int);
void*	oopenblk(char*, void*, vlong, vlong, int,
		long (*)(void*, void*, long, uvlong, int), int (*)(void*));
uvlong	ospaceid(Gefs*, char*);
/* Plan2001 Stage 4: opaque wrappers for port/gefs/sysgefs.c's syscalls
 * (6c's cross-module signatures for an incomplete struct differ) */
void*	onativeopen(void*, char*);
long	onativeget(void*, uvlong, void*, long, uvlong*);
void	onativeput(void*, uvlong, void*, long, uvlong);
long	onativecreate(void*, void*, long, uvlong*, uvlong);
uvlong	onativecommit(void*);
void	onativeclose(void*);
Ospace*	ospaceopen(Gefs*, char*);
void	ospaceclose(Ospace*);
long	ospaceget(Ospace*, uvlong, void*, long, uvlong*);
void	ospaceput(Ospace*, uvlong, void*, long, uvlong);
long	ospacecreate(Ospace*, void*, long, uvlong*, uvlong);
uvlong	ospacecommit(Ospace*);
void	loadlog(Arena*, Bptr);
void	flushlog(Arena*);
void	compresslog(Arena*);
void	dlsync(Gefs*);
void	setval(Blk*, Kvp*);

void	btupsert(Tree*, Msg*, int);
int	btdrain(Tree*, Key*, Kvp*, char*, int);
extern int	objfail;
extern int	ownerfault;
int	btlookup(Tree*, Key*, Kvp*, char*, int);
void	btnewscan(Gefs*, Scan*, char*, int);
void	btenter(Tree*, Scan*);
int	btnext(Scan*, Kvp*);
void	btexit(Scan*);

int	checkflag(Blk *b, int, int);
void	setflag(Blk *b, int, int);

char*	estrdup(char*);

int	keycmp(Key *, Key *);
void	cpkey(Key*, Key*, char*, int);
void	cpkvp(Kvp*, Kvp*, char*, int);

/* for dumping */
void	getval(Blk*, int, Kvp*);
void	getmsg(Blk*, int, Msg*);
Bptr	getptr(Kvp*, int*);

void	initshow(void);
void	showblk(Gefs*, int, Blk*, char*, int);
int	checkfs(Gefs*, int);


#define dprint(...) \
		if(debug) fprint(2, __VA_ARGS__)

#define fatal(...) \
	fprint(2, __VA_ARGS__), abort()

#define tracex(msg, bp, v0, v1) \
		if(fs->trace != nil) _trace(fs, msg, bp, v0, v1)

#define bassert(b, cond) \
	if(cond){}else _babort(b->limbo.fs, b->bp.addr, "cond")

#define traceb(msg, bp)	tracex(msg, bp, -1, -1)
#define tracev(msg, v0)	tracex(msg, Zb, v0, -1)
#define tracem(msg)	tracex(msg, Zb, -1, -1)

_Noreturn void	broke(Gefs*, char*, ...);
_Noreturn void	_babort(Gefs*, vlong, char*);
void	_trace(Gefs*, char*, Bptr, vlong, vlong);

void	dlist2kv(Dlist*, Kvp*, char*, int);
void	lbl2kv(char*, vlong, uint, Kvp*, char*, int);
void	link2kv(vlong, vlong, Kvp*, char*, int);
void	retag2kv(vlong, vlong, int, int, Kvp*, char*, int);
void	tree2kv(Tree*, Kvp*, char*, int);

void	kv2dlist(Kvp*, Dlist*);
void	kv2link(Kvp*, vlong*, vlong*);

char*	packarena(char*, int, Arena*);
char*	packbp(char*, int, Bptr*);
char*	packlbl(char*, int, char*);
char*	packsnap(char*, int, vlong);
char*	packtree(char*, int, Tree*);
char*	packsb(char*, int, Gefs*);

char*	unpackarena(Arena*, char*, int);
Bptr	unpackbp(char*, int);
Tree*	unpacktree(Gefs*, Tree*, char*, int);
char*	unpacksb(Gefs*, char*, int);

/* fmt */
int	Bconv(Fmt*);
int	Mconv(Fmt*);
int	Pconv(Fmt*);
int	Rconv(Fmt*);
int	Kconv(Fmt*);
int	Qconv(Fmt*);

Chan*	mkchan(int);
void*	chrecv(Chan*);
void	chsend(Chan*, void*);
void	runtasks(Gefs*, int, void*);
void	runsync(Gefs*, int, void*);
void	runsweep(Gefs*, int, void*);

