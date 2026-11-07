#include "gefs.h"

void
kv2dlist(Kvp *kv, Dlist *dl)
{
	char *p, *e;

	p = kv->key.k;
	e = p + kv->key.nk;
	p++;
	dl->gen = UNPACK64(p);	p += 8;
	dl->bgen = UNPACK64(p);	p += 8;
	assert(p <= e);
	
	p = kv->val.v;
	e = p + kv->val.nv;
	dl->hd = unpackbp(p, e-p);	p += Ptrsz;
	dl->tl = unpackbp(p, e-p);	p += Ptrsz;
	assert(p <= e);
}

void
dlist2kv(Dlist *dl, Kvp *kv, char *buf, int nbuf)
{
	char *p, *e;

	assert(nbuf >= Dlkvpsz);
	p = buf;
	e = buf+nbuf;

	kv->key.k = p;
	*p++ = Kdlist;
	PACK64(p, dl->gen);	p += 8;
	PACK64(p, dl->bgen);	p += 8;
	kv->key.nk = (p - kv->key.k);
	
	kv->val.v = p;
	p = packbp(p, e-p, &dl->hd);
	p = packbp(p, e-p, &dl->tl);
	kv->val.nv = (p - kv->val.v);
}

void
tree2kv(Tree *t, Kvp *kv, char *buf, int nbuf)
{
	char *p, *e;

	p = buf;
	e = buf+nbuf;

	kv->key.k = p;
	if((p = packsnap(p, e-p, t->gen)) == nil)
		abort();
	kv->key.nk = p - kv->key.k;

	kv->val.v = p;
	if((p = packtree(p, e-p, t)) == nil)
		abort();
	kv->val.nv = p - kv->val.v;
}

void
retag2kv(vlong gen, vlong link, int dlbl, int dref, Kvp *kv, char *buf, int nbuf)
{
	char *p;

	assert(gen != -1);
	assert(nbuf >= 8+1+1);
	kv->key.k = buf;
	if((p = packsnap(buf, nbuf, gen)) == nil)
		abort();
	kv->key.nk = p - buf;

	kv->val.v = p;
	PACK64(p, link);	p += 8;
	*p = dlbl;		p += 1;
	*p = dref;		p += 1;
	kv->val.nv = p - kv->val.v;
}

void
lbl2kv(char *lbl, vlong gen, uint flg, Kvp *kv, char *buf, int nbuf)
{
	char *p;
	int n;

	n = strlen(lbl);
	assert(nbuf >= 1+n + 1+8+4);

	p = buf;
	kv->key.k = p;
	p[0] = Klabel;		p += 1;
	memcpy(p, lbl, n);	p += n;
	kv->key.nk = p - kv->key.k;

	kv->val.v = p;
	p[0] = Ksnap;		p += 1;
	PACK64(p, gen);		p += 8;
	PACK32(p, flg);		p += 4;
	kv->val.nv = p - kv->val.v;
}

char*
packlbl(char *p, int sz, char *name)
{
	int n;

	n = strlen(name);
	assert(sz >= n+1);
	p[0] = Klabel;		p += 1;
	memcpy(p, name, n);	p += n;
	return p;
}

char*
packsnap(char *p, int sz, vlong id)
{
	assert(sz >= Snapsz);
	p[0] = Ksnap;		p += 1;
	PACK64(p, id);		p += 8;
	return p;
}

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

Tree*
unpacktree(Gefs *fs, Tree *t, char *p, int sz)
{
	assert(sz >= Treesz);
	memset(t, 0, sizeof(Tree));
	t->limbo.fs = fs;	/* nil: a dumper's, printed and dropped */
	t->nref = UNPACK32(p);		p += 4;
	t->nlbl = UNPACK32(p);		p += 4;
	t->ht = UNPACK32(p);		p += 4;
	t->flag = UNPACK32(p);		p += 4;
	t->gen = UNPACK64(p);		p += 8;
	t->pred = UNPACK64(p);		p += 8;
	t->succ = UNPACK64(p);		p += 8;
	t->base = UNPACK64(p);		p += 8;
	t->bp.addr = UNPACK64(p);	p += 8;
	t->bp.hash = UNPACK64(p);	p += 8;
	t->bp.gen = UNPACK64(p);	//p += 8;

	return t;
}

char*
packtree(char *p, int sz, Tree *t)
{
	assert(sz >= Treesz);
	PACK32(p, t->nref);	p += 4;
	PACK32(p, t->nlbl);	p += 4;
	PACK32(p, t->ht);	p += 4;
	PACK32(p, t->flag);	p += 4;
	PACK64(p, t->gen);	p += 8;
	PACK64(p, t->pred);	p += 8;
	PACK64(p, t->succ);	p += 8;
	PACK64(p, t->base);	p += 8;
	PACK64(p, t->bp.addr);	p += 8;
	PACK64(p, t->bp.hash);	p += 8;
	PACK64(p, t->bp.gen);	p += 8;
	return p;
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
	Gefs *fs;

	assert(sz >= Arenasz);
	fs = a->fs;		/* the volume, not the disk's: unpacking keeps it */
	memset(a, 0, sizeof(*a));
	a->fs = fs;

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

char*
unpacksb(Gefs *fi, char *p0, int sz)
{
	Gefs *fs = fi;
	vlong qgen;
	uvlong dh, xh;
	char *p;
	int i;

	assert(sz == Blksz);
	p = p0;
	if(memcmp(p, "gefs9.00", 8) != 0)
		error("%s %.8s", Efsvers, p);
	p += 8;
	fi->blksz = UNPACK32(p);		p += 4;
	fi->bufspc = UNPACK32(p);		p += 4;
	fi->narena = UNPACK32(p);		p += 4;
	fi->snap.ht = UNPACK32(p);		p += 4;
	fi->snap.bp.addr = UNPACK64(p);		p += 8;
	fi->snap.bp.hash = UNPACK64(p);		p += 8;
	fi->snap.bp.gen = -1;			p += 0;
	fi->snapdl.hd.addr = UNPACK64(p);	p += 8;
	fi->snapdl.hd.hash = UNPACK64(p);	p += 8;
	fi->snapdl.hd.gen = -1;			p += 0;
	fi->snapdl.gen = -1;			p += 0;
	fi->snapdl.tl.addr = UNPACK64(p);	p += 8;
	fi->snapdl.tl.hash = UNPACK64(p);	p += 8;
	fi->snapdl.tl.gen = -1;			p += 0;
	fi->snapdl.gen = -1;			p += 0;
	fi->flag = UNPACK64(p);			p += 8;
	fi->nextqid = UNPACK64(p);		p += 8;
	fi->nextgen = UNPACK64(p);		p += 8;
	qgen = UNPACK64(p);			p += 8;
	aswapv(&fs->qgen, qgen);
	fi->arenabp = emalloc(fi->narena * sizeof(Bptr), 0);
	for(i = 0; i < fi->narena; i++){
		fi->arenabp[i].addr = UNPACK64(p);	p += 8;
		fi->arenabp[i].hash = UNPACK64(p);	p += 8;
		fi->arenabp[i].gen = -1;
	}
	xh = bufhash(p0, p - p0);
	dh = UNPACK64(p);			p += 8;
	if(dh != xh)
		error("corrupt superblock: %llx != %llx", dh, xh);
	assert(fi->narena < 256);	/* should be more than anyone needs */
	return p;
}
