#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

char*
packbp(char *p, int sz, Bptr *bp)
{
	assert(sz >= Ptrsz);
	PACK64(p, bp->addr);	p += 8;
	PACK64(p, bp->hash);	p += 8;
	PACK64(p, bp->gen);	p += 8;
	return p;
}

Bptr
unpackbp(char *p, int sz)
{
	Bptr bp;

	assert(sz >= Ptrsz);
	bp.addr = UNPACK64(p);	p += 8;
	bp.hash = UNPACK64(p);	p += 8;
	bp.gen = UNPACK64(p);
	return bp;
}

char*
packarena(char *p, int sz, Arena *a)
{
	char *e;

	assert(sz >= Arenasz);
	e = p + Arenasz;
	PACK64(p, a->loghd.addr);	p += 8;	/* freelist addr */
	PACK64(p, a->loghd.hash);	p += 8;	/* freelist hash */
	PACK64(p, a->size);		p += 8;	/* arena size */
	PACK64(p, a->used);		p += 8;	/* arena used */
	assert(p <= e);
	return p;
}

char*
unpackarena(Arena *a, char *p, int sz)
{
	char *e;

	assert(sz >= Arenasz);
	memset(a, 0, sizeof(*a));

	e = p + Arenasz;
	a->loghd.addr = UNPACK64(p);	p += 8;
	a->loghd.hash = UNPACK64(p);	p += 8;
	a->loghd.gen = -1;		p += 0;
	a->size = UNPACK64(p);		p += 8;
	a->used = UNPACK64(p);		p += 8;
	a->logtl = nil;

	assert(p <= e);
	return p;
}

char*
packsb(char *p0, int sz, Gefs *fi)
{
	vlong nextqid, nextgen, qgen;
	uvlong h;
	char *p;
	int i;

	assert(sz == Blksz);
	assert(fi->narena < 512);
	p = p0;
	nextqid = fi->nextqid;
	nextgen = fi->nextgen;
	qgen = agetv(&fi->qgen);
	memcpy(p, "gefs9.00", 8);	p += 8;
	PACK32(p, Blksz);		p += 4;
	PACK32(p, Bufspc);		p += 4;
	PACK32(p, fi->narena);		p += 4;
	PACK32(p, fi->snap.ht);		p += 4;
	PACK64(p, fi->snap.bp.addr);	p += 8;
	PACK64(p, fi->snap.bp.hash);	p += 8;
	PACK64(p, fi->snapdl.hd.addr);	p += 8;
	PACK64(p, fi->snapdl.hd.hash);	p += 8;
	PACK64(p, fi->snapdl.tl.addr);	p += 8;
	PACK64(p, fi->snapdl.tl.hash);	p += 8;
	PACK64(p, fi->flag);		p += 8;
	PACK64(p, nextqid);		p += 8;
	PACK64(p, nextgen);		p += 8;
	PACK64(p, qgen);		p += 8;
	for(i = 0; i < fi->narena; i++){
		PACK64(p, fi->arenabp[i].addr);	p += 8;
		PACK64(p, fi->arenabp[i].hash);	p += 8;
	}
	h = bufhash(p0, p - p0);
	PACK64(p, h);			p += 8;
	return p;
}
