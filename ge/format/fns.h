#pragma varargck type "B"	Bptr

extern Gefs*	fs;
extern int	debug;
extern int	usereserve;
extern Errctx**	errctx;
extern Blk*	blkbuf;
extern char*	seeddir;

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
#define	PACK16(p,v)	do{(p)[0]=(uvlong)(v)>>8;(p)[1]=(uvlong)(v);}while(0)
#define	PACK32(p,v)	do{(p)[0]=(uvlong)(v)>>24;(p)[1]=(uvlong)(v)>>16;(p)[2]=(uvlong)(v)>>8;(p)[3]=(uvlong)(v);}while(0)
#define	PACK64(p,v)	do{(p)[0]=(uvlong)(v)>>56;(p)[1]=(uvlong)(v)>>48;(p)[2]=(uvlong)(v)>>40;(p)[3]=(uvlong)(v)>>32;\
			   (p)[4]=(uvlong)(v)>>24;(p)[5]=(uvlong)(v)>>16;(p)[6]=(uvlong)(v)>>8;(p)[7]=(uvlong)(v);}while(0)

void*	emalloc(usize, int);
Blk*	newblk(Tree*, int);
Blk*	getblk(Bptr, int);
Blk*	holdblk(Blk*);
void	dropblk(Blk*);

void	lrutop(Blk*);
void	lrubot(Blk*);
void	cacheins(Blk*);
void	cachedel(vlong);
Blk*	cacheget(vlong);
Blk*	cachepluck(void);

void	syncblk(Blk*);
ushort	blkfill(Blk*);
uvlong	blkhash(Blk*);
uvlong	bufhash(void*, usize);
u32int	ihash(uvlong);
void	finalize(Blk*);

void	reamo(char*);
void	loadarena(Arena*, Bptr);
void	loadlog(Arena*, Bptr);
void	setval(Blk*, Kvp*);

int	checkflag(Blk *, int, int);
void	setflag(Blk *, int, int);

int	keycmp(Key *, Key *);

#define dprint(...) \
		if(debug) fprint(2, __VA_ARGS__)

#define fatal(...) \
	fprint(2, __VA_ARGS__), abort()

#define tracex(msg, bp, v0, v1) \
		if(fs->trace != nil) _trace(msg, bp, v0, v1)

#define bassert(b, cond) \
	if(cond){}else _babort(b->bp.addr, "cond")

#define traceb(msg, bp)	tracex(msg, bp, -1, -1)
#define tracev(msg, v0)	tracex(msg, Zb, v0, -1)
#define tracem(msg)	tracex(msg, Zb, -1, -1)

jmp_buf*	_waserror(void);
_Noreturn void	error(char*, ...);
_Noreturn void	broke(char*, ...);
_Noreturn void	nexterror(void);
_Noreturn void	_babort(vlong, char*);
#define waserror()	(setjmp(*_waserror()))
#define errmsg()	((*errctx)->err)
#define poperror()	assert((*errctx)->nerrlab-- > 0)
#define estacksz()	((*errctx)->nerrlab)
void	_trace(char*, Bptr, vlong, vlong);

char*	packarena(char*, int, Arena*);
char*	packbp(char*, int, Bptr*);
char*	packsb(char*, int, Gefs*);

char*	unpackarena(Arena*, char*, int);
Bptr	unpackbp(char*, int);
