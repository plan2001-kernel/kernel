#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static vlong	blkalloc_lk(Arena*, int);
static vlong	blkalloc(int, uint, int);
static Blk*	initblk(Blk*, vlong, vlong, int);
static void	readblk(Blk*, Bptr, int, uintptr);

int
checkflag(Blk *b, int set, int clr)
{
	long v;

	v = agetl(&b->flag);
	return (v & (set|clr)) == set;
}

void
setflag(Blk *b, int set, int clr)
{
	long ov, nv;

	while(1){
		ov = agetl(&b->flag);
		nv = (ov & ~clr) | set;
		if(acasl(&b->flag, ov, nv))
			break;
	}
}

void
syncblk(Blk *b)
{
	bassert(b, checkflag(b, Bfinal, 0));
	bassert(b, b->bp.addr >= 0);
	tracex("syncblk", b->bp, b->type, -1);
	if(pwrite(fs->fd, b->buf, Blksz, b->bp.addr) == -1)
		broke("%B %s: %r", b->bp, Eio);
	setflag(b, 0, Bdirty);
}

static void
readblk(Blk *b, Bptr bp, int flg, uintptr)
{
	vlong off, xh, ck, rem, n;
	char *p;

	off = bp.addr;
	rem = Blksz;
	p = b->buf;
	while(rem != 0){
		n = pread(fs->fd, p, rem, off);
		if(n <= 0)
			error("%s: %r", Eio);
		rem -= n;
		off += n;
		p += n;
	}
	b->cnext = nil;
	b->cprev = nil;
	b->hnext = nil;

	b->bp.addr = bp.addr;
	b->bp.hash = -1;
	b->bp.gen = -1;

	b->nval = 0;
	b->valsz = 0;
	b->nbuf = 0;
	b->bufsz = 0;
	b->logsz = 0;

	p = b->buf + 2;
	b->type = (flg&GBraw) ? Tdat : UNPACK16(b->buf+0);
	switch(b->type){
	default:
		error("invalid block type %d @%llx", b->type, bp);
		break;
	case Tdat:
	case Tsuper:
		b->data = b->buf;
		break;
	case Tarena:
		b->data = p;
		break;
	case Tdlist:
	case Tlog:
		b->logsz = UNPACK16(p);		p += 2;
		b->logh = UNPACK64(p);		p += 8;
		b->logp = unpackbp(p, Ptrsz);	p += Ptrsz;
		bassert(b, p - b->buf == Loghdsz);
		b->data = p;
		break;
	case Tpivot:
		b->nval = UNPACK16(p);		p += 2;
		b->valsz = UNPACK16(p);		p += 2;
		b->nbuf = UNPACK16(p);		p += 2;
		b->bufsz = UNPACK16(p);		p += 2;
		bassert(b, p - b->buf == Pivhdsz);
		b->data = p;
		break;
	case Tleaf:
		b->nval = UNPACK16(p);		p += 2;
		b->valsz = UNPACK16(p);		p += 2;
		bassert(b, p - b->buf == Leafhdsz);
		b->data = p;
		break;
	}
	if(b->type == Tlog || b->type == Tdlist){
		xh = b->logh;
		ck = bufhash(b->data, b->logsz);
	}else{
		xh = bp.hash;
		ck = blkhash(b);
	}
	if((flg&GBnochk) == 0 && ck != xh)
		error("%s: %ullx %llux != %llux", Ecorrupt, bp.addr, xh, ck);
	bassert(b, b->magic == Magic);
}

static Arena*
pickarena(uint ty, uint hint, int tries)
{
	uint n, r;

	r = aincl(&fs->roundrobin, 1)/2048;
	if(ty == Tdat)
		n = hint % (fs->narena - 1) + r + 1;
	else
		n = r;
	return &fs->arenas[(n + tries) % fs->narena];
}

static void
freerange(Avltree *t, vlong off, vlong len)
{
	Arange *r, *s;

	assert(len % Blksz == 0);
	if((r = calloc(1, sizeof(Arange))) == nil)
		error(Enomem);
	r->off = off;
	r->len = len;
	assert(avllookup(t, &r->avl, 0) == nil);
	avlinsert(t, &r->avl);

Again:
	s = (Arange*)avlprev(&r->avl);
	if(s != nil){
		if(s->off+s->len == r->off){
			avldelete(t, &r->avl);
			s->len = s->len + r->len;
			free(r);
			r = s;
			goto Again;
		}
		assert(s->off+s->len < r->off);
	}
	s = (Arange*)avlnext(&r->avl);
	if(s != nil){
		if(r->off+r->len == s->off){
			avldelete(t, &r->avl);
			s->off = r->off;
			s->len = s->len + r->len;
			free(r);
			r = s;
			goto Again;
		}
		assert(r->off+r->len < s->off);
	}
}

static void
grabrange(Avltree *t, vlong off, vlong len)
{
	Arange *r, *s, q;
	vlong l;

	assert(len % Blksz == 0);
	q.off = off;
	q.len = len;
	r = (Arange*)avllookup(t, &q.avl, -1);
	if(r == nil || off + len > r->off + r->len)
		abort();

	if(off == r->off){
		r->off += len;
		r->len -= len;
	}else if(off + len == r->off + r->len){
		r->len -= len;
	}else if(off > r->off && off+len < r->off + r->len){
		s = emalloc(sizeof(Arange), 0);
		l = r->len;
		s->off = off + len;
		r->len = off - r->off;
		s->len = l - r->len - len;
		avlinsert(t, &s->avl);
	}else
		abort();

	if(r->len == 0){
		avldelete(t, &r->avl);
		free(r);
	}
}

static Blk*
mklogblk(Arena *a, vlong o)
{
	Blk *lb;

	lb = a->logbuf[0];
	if(lb == a->logtl)
		lb = a->logbuf[1];
	bassert(lb, agetl(&lb->ref) == 1);
	aswapl(&lb->flag, Bstatic);
	initblk(lb, o, -1, Tlog);
	traceb("logblk" , lb->bp);
	lb->lasthold0 = lb->lasthold;
	lb = holdblk(lb);
	lb->lasthold = getcallerpc(&a);
	return lb;
}

/*
 * Logs an allocation. Must be called
 * with arena lock held. Duplicates some
 * of the work in allocblk to prevent
 * recursion.
 */
static void
logappend(Arena *a, vlong off, vlong len, int op)
{
	vlong o, start, end;
	Blk *lb;
	char *p;

	assert((off & 0xff) == 0);
	assert(op == LogAlloc || op == LogFree || op == LogSync);
	if(op != LogSync){
		start = a->h0->bp.addr;
		end = start + a->size + 2*Blksz;
		assert(off >= start);
		assert(off < end);
	}
	lb = a->logtl;
	bassert(lb, agetl(&lb->ref) > 0);
	bassert(lb, lb->type == Tlog);
	bassert(lb, lb->logsz >= 0);

	if(checkflag(lb, 0, Bdirty))
		setflag(lb, Bdirty, Bfinal);

	/*
	 * move to the next block when we have
	 * too little room in the log:
	 * We're appending up to 16 bytes as
	 * part of the operation, followed by
	 * 16 bytes of new log entry allocation
	 * and chaining.
	 */
	if(lb->logsz >= Logspc - Logslop){
		o = blkalloc_lk(a, 0);
		if(o == -1)
			error(Efull);
		p = lb->data + lb->logsz;
		PACK64(p, o|LogAlloc1);
		lb->logsz += 8;
		lb->logp = (Bptr){o, -1, -1};
		lb = mklogblk(a, o);
	}
	if(len == Blksz){
		if(op == LogAlloc)
			op = LogAlloc1;
		else if(op == LogFree)
			op = LogFree1;
	}
	off |= op;
	p = lb->data + lb->logsz;
	PACK64(p, off);
	lb->logsz += 8;
	if(op >= Log2wide){
		PACK64(p+8, len);
		lb->logsz += 8;
	}
	if(lb != a->logtl) {
		finalize(lb);
		syncblk(lb);

		finalize(a->logtl);
		syncblk(a->logtl);
		dropblk(a->logtl);
		a->logtl = lb;
		a->nlog++;
	}
}

void
loadlog(Arena *a, Bptr bp)
{
	vlong ent, off, len, gen;
	int op, i, n;
	char *d;
	Blk *b;


	dprint("loadlog %B\n", bp);
	traceb("loadlog", bp);
	b = a->logbuf[0];
	while(1){
		bassert(b, checkflag(b, Bstatic, Bcached));
		holdblk(b);
		readblk(b, bp, 0, getcallerpc(&a));
		dprint("\tload %B chain %B\n", bp, b->logp);
		a->nlog++;
		for(i = 0; i < b->logsz; i += n){
			d = b->data + i;
			ent = UNPACK64(d);
			op = ent & 0xff;
			off = ent & ~0xff;
			n = (op >= Log2wide) ? 16 : 8;
			switch(op){
			case LogSync:
				gen = ent >> 8;
				dprint("\tlog@%x: sync %lld\n", i, gen);
				if(gen >= agetv(&fs->qgen)){
					if(a->logtl == nil){
						b->logp = Zb;
						b->logsz = i;
						a->logtl = b;
						cachedel(b->bp.addr);
						setflag(b, Bdirty, 0);
						/*
						 * Plan2001: the truncated tail on the disk
						 * too, or the disk's chain goes on past it to
						 * blocks allocated after the sync, which are
						 * free now; a walk of the chain (check, the
						 * free of the old log after compresslog)
						 * would cache and free them.
						 */
						if(!agetl(&fs->rdonly)){
							finalize(b);
							syncblk(b);
						}
						return;
					}
					dropblk(b);
					return;
				}
				break;
	
			case LogAlloc:
			case LogAlloc1:
				len = (op >= Log2wide) ? UNPACK64(d+8) : Blksz;
				dprint("\tlog@%x alloc: %llx+%llx\n", i, off, len);
				grabrange(a->free, off & ~0xff, len);
				a->used += len;
				break;
			case LogFree:
			case LogFree1:
				len = (op >= Log2wide) ? UNPACK64(d+8) : Blksz;
				dprint("\tlog@%x free: %llx+%llx\n", i, off, len);
				freerange(a->free, off & ~0xff, len);
				a->used -= len;
				break;
			default:
				dprint("\tlog@%x: log op %d\n", i, op);
				abort();
				break;
			}
		}
		if(b->logp.addr == -1){
			a->logtl = b;
			return;
		}
		bp = b->logp;
		dropblk(b);
	}
}

/*
 * Allocate from an arena, with lock
 * held. May be called multiple times
 * per operation, to alloc space for
 * the alloc log.
 */
static vlong
blkalloc_lk(Arena *a, int seq)
{
	Arange *r;
	vlong b;

	if(seq)
		r = (Arange*)avlmin(a->free);
	else
		r = (Arange*)avlmax(a->free);
	if(!usereserve && a->size - a->used <= a->reserve)
		return -1;
	if(r == nil)
		broke(Estuffed);

	/*
	 * A bit of sleight of hand here:
	 * while we're changing the sorting
	 * key, but we know it won't change
	 * the sort order because the tree
	 * covers disjoint ranges
	 */
	if(seq){
		b = r->off;
		r->len -= Blksz;
		r->off += Blksz;
	}else{
		r->len -= Blksz;
		b = r->off + r->len;
	}
	if(r->len == 0){
		avldelete(a->free, &r->avl);
		free(r);
	}
	a->used += Blksz;
	tracex("blkalloc" , Zb, b, getcallerpc(&a));
	return b;
}

static vlong
blkalloc(int ty, uint hint, int seq)
{
	Arena *a;
	vlong b;
	int tries;

	tries = 0;
Again:
	a = pickarena(ty, hint, tries);
	/*
	 * Loop through the arena up to 2 times.
	 * The first pass tries to find an arena
	 * that has space and is not in use, the
	 * second waits until an arena is free.
	 */
	if(tries == 2*fs->narena)
		error(Efull);
	tries++;
	if(tries < fs->narena){
		if(canqlock(&a->qlk) == 0)
			goto Again;
	}else
		qlock(&a->qlk);
	if(waserror()){
		qunlock(&a->qlk);
		nexterror();
	}
	/*
	 * Plan2001: data stops at twice the reserve: a full disk's
	 * writer gets Efull, while the tree updates and syncs still
	 * have the reserve (an update that fails makes gefs read-only)
	 */
	if(ty == Tdat && !usereserve && a->size - a->used <= 2*a->reserve)
		b = -1;
	else
		b = blkalloc_lk(a, seq);
	if(b == -1){
		qunlock(&a->qlk);
		poperror();
		goto Again;
	}
	logappend(a, b, Blksz, LogAlloc);
	qunlock(&a->qlk);
	poperror();
	return b;
}

static Blk*
initblk(Blk *b, vlong bp, vlong gen, int ty)
{
	Blk *ob;

	b->type = ty;
	b->bp.addr = bp;
	b->bp.hash = -1;
	b->bp.gen = gen;
	ob = cacheget(bp);
	if(ob != nil)
		fatal("double alloc: %#p %B %#p %B", b, b->bp, ob, ob->bp);
	switch(ty){
	case Tdat:
		b->data = b->buf;
		break;
	case Tarena:
		b->data = b->buf+2;
		break;
	case Tdlist:
	case Tlog:
		b->logsz = 0;
		b->logp = (Bptr){-1, -1, -1};
		b->data = b->buf + Loghdsz;
		break;
	case Tpivot:
		b->data = b->buf + Pivhdsz;
		break;
	case Tleaf:
		b->data = b->buf + Leafhdsz;
		break;
	}
	setflag(b, Bdirty, 0);
	b->nval = 0;
	b->valsz = 0;
	b->nbuf = 0;
	b->bufsz = 0;
	b->logsz = 0;
	b->alloced = getcallerpc(&b);

	return b;
}

Blk*
newblk(Tree *t, int ty)
{
	vlong bp;
	Blk *b;

	bp = blkalloc(ty, 0, 0);
	b = cachepluck();
	initblk(b, bp, t->memgen, ty);
	b->alloced = getcallerpc(&t);
	return b;
}

void
finalize(Blk *b)
{
	if(b->type != Tdat)
		PACK16(b->buf, b->type);

	switch(b->type){
	default:
		abort();
		break;
	case Tpivot:
		PACK16(b->buf+2, b->nval);
		PACK16(b->buf+4, b->valsz);
		PACK16(b->buf+6, b->nbuf);
		PACK16(b->buf+8, b->bufsz);
		break;
	case Tleaf:
		PACK16(b->buf+2, b->nval);
		PACK16(b->buf+4, b->valsz);
		break;
	case Tdlist:
	case Tlog:
		b->logh = bufhash(b->data, b->logsz);
		PACK16(b->buf+2, b->logsz);
		PACK64(b->buf+4, b->logh);
		packbp(b->buf+12, Ptrsz, &b->logp);
		break;
	case Tdat:
	case Tarena:
	case Tsuper:
		break;
	}

	b->bp.hash = blkhash(b);
	setflag(b, Bdirty|Bfinal, 0);
}

Blk*
getblk(Bptr bp, int flg)
{
	Blk *b;
	int i;

	i = ihash(bp.addr) % nelem(fs->blklk);
	qlock(&fs->blklk[i]);
	if(waserror()){
		qunlock(&fs->blklk[i]);
		if(flg&GBtry)
			return nil;
		else
			aincl(&fs->rdonly, 1);
		nexterror();
	}
	if((b = cacheget(bp.addr)) != nil){
		bassert(b, checkflag(b, 0, Bfreed));
		bassert(b, b->bp.gen == bp.gen);
	} else {
		b = cachepluck();
		b->alloced = getcallerpc(&bp);
		readblk(b, bp, flg, getcallerpc(&bp));
		b->bp.gen = bp.gen;
		cacheins(b);
	}
	b->lasthold = getcallerpc(&bp);
	qunlock(&fs->blklk[i]);
	poperror();

	return b;
}


Blk*
holdblk(Blk *b)
{
	aincl(&b->ref, 1);
	b->lasthold = getcallerpc(&b);
	return b;
}

void
dropblk(Blk *b)
{
	if(b == nil)
		return;
	b->lastdrop = getcallerpc(&b);
	if(aincl(&b->ref, -1) != 0)
		return;
	/*
	 * freed blocks go to the LRU bottom
	 * for early reuse.
	 */
	if(checkflag(b, Bfreed, 0))
		lrubot(b);
	else
		lrutop(b);
}

ushort
blkfill(Blk *b)
{
	switch(b->type){
	case Tpivot:
		return 2*b->nbuf + b->bufsz +  2*b->nval + b->valsz;
	case Tleaf:
		return 2*b->nval + b->valsz;
	default:
		fprint(2, "invalid block @%lld\n", b->bp.addr);
		abort();
	}
}
