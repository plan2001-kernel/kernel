#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static void
initarena(Arena *a, uvlong hdaddr, vlong asz)
{
	Blk *b, *h0, *h1;
	uvlong addr;
	char *p;

	b = cachepluck();

	addr = hdaddr+2*Blksz;	/* leave room for arena hdr */

	a->loghd.addr = -1;
	a->loghd.hash = -1;
	a->loghd.gen = -1;

	memset(b->buf, 0, sizeof(b->buf));
	b->type = Tlog;
	b->bp.addr = addr;
	b->logsz = 0;
	b->logp = (Bptr){-1, -1, -1};
	b->data = b->buf + Loghdsz;
	setflag(b, Bdirty, 0);

	p = b->buf + Loghdsz;
	b->logp = (Bptr){-1, -1, -1};
	PACK64(p, addr|LogFree);	p += 8;	/* addr */
	PACK64(p, asz-2*Blksz);		p += 8;	/* len */
	PACK64(p, b->bp.addr|LogAlloc);	p += 8;	/* addr */
	PACK64(p, Blksz);		p += 8;	/* len */
	PACK64(p, (uvlong)LogSync);	p += 8;	/* barrier */
	b->logsz = p - b->data;
	finalize(b);
	syncblk(b);
	dropblk(b);

	a->loghd = b->bp;
	a->loghd.gen = -1;
	a->size = asz;
	a->used = Blksz;

	h0 = cachepluck();
	h1 = cachepluck();

	memset(h0->buf, 0, sizeof(h0->buf));
	h0->type = Tarena;
	h0->bp.addr = hdaddr;
	h0->data = h0->buf+2;
	packarena(h0->data, Arenasz, a);
	finalize(h0);
	syncblk(h0);
	a->h0 = h0;

	memset(h1->buf, 0, sizeof(h1->buf));
	h1->type = Tarena;
	h1->bp.addr = hdaddr+Blksz;
	h1->data = h1->buf+2;
	packarena(h1->data, Arenasz, a);
	finalize(h1);
	syncblk(h1);
	a->h1 = h1;
}

/*
 * Plan2001 Stage 4: seed a fresh O volume with one Environment and its
 * immutable code objects.  Files are host preparation inputs only: the
 * finished image's runtime reads O objects by OID.  A single catalog
 * leaf holds the records; a demo's manifest and code must fit in it.
 */
enum {
	OID_ENV_HOST	= 0xfffffffffffffffcULL,
	OID_FIRST_HOST	= 2,
	Seedmax		= 128*KiB,
};

typedef struct Seedrec Seedrec;
struct Seedrec {
	char	k[Keymax];
	int	nk;
	char	v[Inlmax];
	int	nv;
};

extern char *seeddir;

static int
seedcmp(void *a, void *b)
{
	Seedrec *x = (Seedrec*)a, *y = (Seedrec*)b;
	Key kx, ky;

	kx.k = x->k;
	kx.nk = x->nk;
	ky.k = y->k;
	ky.nk = y->nk;
	return keycmp(&kx, &ky);
}

static void
seedadd(Seedrec **r, int *n, int *a, char *k, int nk, char *v, int nv)
{
	Seedrec *x;

	if(nk > Keymax || nv > Inlmax)
		sysfatal("seed: record too big");
	if(*n == *a){
		*a = *a ? 2*(*a) : 32;
		*r = realloc(*r, *a * sizeof(Seedrec));
		if(*r == nil)
			sysfatal("seed: malloc: %r");
	}
	x = &(*r)[(*n)++];
	memmove(x->k, k, nk);
	x->nk = nk;
	memmove(x->v, v, nv);
	x->nv = nv;
}

static void
seedobjat(Seedrec **r, int *n, int *a, uvlong oid, uchar *data, long len)
{
	enum { Seedchunk = 480 };
	char k[Keymax], v[Inlmax];
	long off, m;
	u32int idx;

	off = 0;
	idx = 0;
	do{
		k[0] = Kobj;
		PACK64(k+1, oid);
		PACK64(k+9, ~1ULL);
		PACK32(k+17, idx);
		m = 0;
		if(idx == 0){
			PACK32(v, (u32int)len);
			m = 4;
		}
		if(len - off > Seedchunk - m)
			m += Seedchunk - m;
		else
			m += len - off;
		if(m > (idx == 0 ? 4 : 0)){
			if(idx == 0)
				memmove(v+4, data+off, m-4);
			else
				memmove(v, data+off, m);
		}
		seedadd(r, n, a, k, 21, v, m);
		off += m - (idx == 0 ? 4 : 0);
		idx++;
	}while(off < len);
}

static void
seedobj(Seedrec **r, int *n, int *a, uvlong oid, uchar *data, long len)
{
	seedobjat(r, n, a, oid, data, len);
}

/*
 * The seeded records need not fit one GE leaf: a realistically sized
 * executable uses many chunk records.  Build an ordinary sorted GE tree
 * bottom-up, with a separator key and a child pointer per block, and write
 * every block directly.  Small seeds still become a single leaf.
 */
typedef struct Seedblk Seedblk;
struct Seedblk {
	Bptr	bp;
	int	fill;
	char	k[Keymax];
	int	nk;
};

static void
seedptr(Blk *b, Seedblk *s)
{
	char buf[Ptrsz+2], *p;
	Kvp kv;

	kv.key.k = s->k;
	kv.key.nk = s->nk;
	kv.val.v = buf;
	kv.val.nv = sizeof buf;
	p = packbp(buf, sizeof buf, &s->bp);
	PACK16(p, s->fill);
	setval(b, &kv);
}

static Bptr
seedtree(Seedrec *r, int nr, int *htp)
{
	Seedblk *cur, *next;
	Bptr bp;
	Blk *b;
	Kvp kv;
	int i, j, ncur, nnext, ht, sz, esz;

	cur = emalloc(nr*sizeof(Seedblk), 1);
	ncur = 0;
	for(i = 0; i < nr;){
		if((b = newblk(&fs->snap, Tleaf)) == nil)
			sysfatal("seed: allocate leaf: %r");
		for(j = i; j < nr; j++){
			sz = 2 + r[j].nk + 2 + r[j].nv;
			if(b->nval > 0 && 2*(b->nval+1) + b->valsz + sz > Leafspc)
				break;
			kv.key.k = r[j].k;
			kv.key.nk = r[j].nk;
			kv.val.v = r[j].v;
			kv.val.nv = r[j].nv;
			setval(b, &kv);
		}
		if(j == i)
			sysfatal("seed: a record does not fit a leaf");
		finalize(b);
		cur[ncur].bp = b->bp;
		cur[ncur].fill = blkfill(b);
		memmove(cur[ncur].k, r[i].k, r[i].nk);
		cur[ncur].nk = r[i].nk;
		ncur++;
		syncblk(b);
		dropblk(b);
		i = j;
	}
	ht = 1;
	while(ncur > 1){
		next = emalloc(ncur*sizeof(Seedblk), 1);
		nnext = 0;
		for(i = 0; i < ncur;){
			if((b = newblk(&fs->snap, Tpivot)) == nil)
				sysfatal("seed: allocate pivot: %r");
			for(j = i; j < ncur; j++){
				esz = 2 + cur[j].nk + 2 + Ptrsz + 2;
				if(b->nval > 0 && 2*(b->nval+1) + b->valsz + esz > Pivspc)
					break;
				seedptr(b, &cur[j]);
			}
			if(j == i)
				sysfatal("seed: a child does not fit a pivot");
			finalize(b);
			next[nnext].bp = b->bp;
			next[nnext].fill = blkfill(b);
			memmove(next[nnext].k, cur[i].k, cur[i].nk);
			next[nnext].nk = cur[i].nk;
			nnext++;
			syncblk(b);
			dropblk(b);
			i = j;
		}
		free(cur);
		cur = next;
		ncur = nnext;
		ht++;
	}
	bp = cur[0].bp;
	free(cur);
	*htp = ht;
	return bp;
}

static int
seedread(char *path, uchar **bp, long *np)
{
	Dir *d;
	uchar *p;
	long n, m, r;
	int fd;

	r = 0;

	if((fd = open(path, OREAD)) < 0)
		return -1;
	if((d = dirfstat(fd)) == nil){
		close(fd);
		return -1;
	}
	n = d->length;
	free(d);
	p = emalloc(n+1, 1);
	m = 0;
	while(m < n){
		r = read(fd, p+m, n-m);
		if(r <= 0)
			break;
		m += r;
	}
	close(fd);
	if(r < 0){
		free(p);
		return -1;
	}
	p[m] = 0;
	*bp = p;
	*np = m;
	return 0;
}

static void
seedfile(Seedrec **r, int *n, int *a, char *name, char *oidname)
{
	char path[512], hex[17];
	uchar *data, *hb;
	long dn, hn;
	uvlong oid;

	snprint(path, sizeof path, "%s/%s", seeddir, name);
	if(seedread(path, &data, &dn) < 0)
		return;
	snprint(path, sizeof path, "%s/%s", seeddir, oidname);
	if(seedread(path, &hb, &hn) < 0)
		sysfatal("seed: %s: %r", path);
	while(hn > 0 && (hb[hn-1] == '\n' || hb[hn-1] == '\r' || hb[hn-1] == ' '))
		hn--;
	if(hn != 16)
		sysfatal("seed: %s: not 16 hex digits", oidname);
	memmove(hex, hb, 16);
	hex[16] = 0;
	oid = strtoull(hex, nil, 16);
	print("seed: object %s oid=%#llux len=%ld\n", name, oid, dn);
	seedobj(r, n, a, oid, data, dn);
	free(data);
	free(hb);
}

static void
oseed(Bptr *rootp, int *htp)
{
	char *env, *p, *e, *q, path[512];
	char name[128], def[65], mpy[65], oidhex[17], k[Keymax], v[Inlmax], hex[65];
	uchar *data, *eb, *hb, *ab, *rp, *re;
	long n, en, hn, an, tot;
	Seedrec *r;
	uvlong oid;
	u32int nk, nv;
	int nr, ar, off, i;

	r = nil;
	nr = ar = 0;
	/* Flat volume: no catalog or SpaceID. */
	snprint(path, sizeof path, "%s/env", seeddir);
	if(seedread(path, &eb, &en) < 0)
		sysfatal("seed: %s: %r", path);
	snprint(path, sizeof path, "%s/envhex", seeddir);
	if(seedread(path, &hb, &hn) < 0)
		sysfatal("seed: %s: %r", path);
	while(hn > 0 && (hb[hn-1] == '\n' || hb[hn-1] == '\r' || hb[hn-1] == ' '))
		hn--;
	if(hn != 64)
		sysfatal("seed: envhex: not 64 characters");
	seedobj(&r, &nr, &ar, OID_ENV_HOST, eb, en);

	/* a line: name definition mpy code-oid */
	env = (char*)eb;
	for(p = env; *p != 0;){
		if((e = strchr(p, '\n')) == nil)
			e = p + strlen(p);
		if(strncmp(p, "runtime ", 8) == 0){
			p = *e ? e+1 : e;
			continue;
		}
		q = p;
		off = 0;
#define FIELD(dst, max) do{ \
	off = 0; \
	while(q < e && *q != ' ' && off < (max)-1) dst[off++] = *q++; \
	dst[off] = 0; \
	if(q < e && *q == ' ') q++; \
}while(0)
		FIELD(name, sizeof name);
		FIELD(def, sizeof def);
		FIELD(mpy, sizeof mpy);
		FIELD(oidhex, sizeof oidhex);
#undef FIELD
		if(strlen(def) != 64 || strlen(mpy) != 64 || strlen(oidhex) != 16)
			sysfatal("seed: bad manifest line");
		oid = strtoull(oidhex, nil, 16);
		snprint(path, sizeof path, "%s/mod/%s.mpy", seeddir, def);
		if(seedread(path, &data, &n) < 0)
			sysfatal("seed: %s: %r", path);
		seedobj(&r, &nr, &ar, oid, data, n);
		free(data);
		p = *e ? e+1 : e;
	}
	seedfile(&r, &nr, &ar, "exe", "exeoid");
	seedfile(&r, &nr, &ar, "rawexec", "rawexecoid");
	seedfile(&r, &nr, &ar, "mp", "mpoid");
	seedfile(&r, &nr, &ar, "bad", "badoid");

	/*
	 * Optional raw Kobj/Kowner fixture records.  The host crash tests use
	 * this to build a disposable legacy Space with multiple versions and
	 * tombstones before any owner-aware write.  It is a seed-input format,
	 * not part of the on-disk O profile.
	 */
	snprint(path, sizeof path, "%s/raw", seeddir);
	if(seedread(path, &ab, &an) >= 0){
		if(an < 8 || memcmp(ab, "RAWREC01", 8) != 0)
			sysfatal("seed: raw: bad magic");
		rp = ab + 8;
		re = ab + an;
		while(rp < re){
			if(re - rp < 8)
				sysfatal("seed: raw: truncated record");
			nk = UNPACK32((char*)rp);
			rp += 4;
			nv = UNPACK32((char*)rp);
			rp += 4;
			if(nk > Keymax || nv > Inlmax ||
			   (uvlong)(re - rp) < (uvlong)nk + nv)
				sysfatal("seed: raw: bad record");
			seedadd(&r, &nr, &ar, (char*)rp, nk,
				(char*)rp + nk, nv);
			rp += nk + nv;
		}
		free(ab);
		print("seed: raw fixture records\n");
	}

	/* the header: version 1, then Environment and next OID */
	memmove(hex, hb, 64);
	hex[64] = 0;
	k[0] = Kobj;
	PACK64(v, 1);
	PACK64(v+8, 1);
	PACK64(v+16, 1);
	memmove(v+24, hb, 64);
	PACK64(v+24+64, OID_FIRST_HOST);
	an = 0;
	snprint(path, sizeof path, "%s/alloc", seeddir);
	if(seedread(path, &ab, &an) >= 0){
		if(an > 256 - 72)
			sysfatal("seed: alloc: too long");
		memmove(v+24+72, ab, an);
		free(ab);
	}
	seedadd(&r, &nr, &ar, k, 1, v, 24+72+an);

	/* All seeded OIDs belong directly to the single volume. */

	free(eb);
	free(hb);
	qsort(r, nr, sizeof(Seedrec), seedcmp);
	tot = 0;
	for(i = 0; i < nr; i++)
		tot += 2 + r[i].nk + 2 + r[i].nv;
	*rootp = seedtree(r, nr, htp);
	free(r);
	print("seed: %d records, %ld bytes, Environment %s\n", nr, tot, hex);
}

/*
 * Plan2001: format a native O volume (docs/O-FORMAT.md).  Reuse
 * GE's arenas, logs and an ordinary leaf for the flat catalog: there is
 * no FS root, adm tree or user table.  The superblock flag carries the
 * profile and PoC version; opening never calls this.
 */
void
reamo(char *dev)
{
	Blk *sb0, *sb1, *tb;
	Bptr root;
	vlong sz, asz, off;
	Arena *a;
	char kbuf[Keymax], vbuf[Inlmax];
	Kvp kv;
	Dir *d;
	int i, ht;

	tb = nil;

	if(waserror())
		sysfatal("ream O %s: %s", dev, errmsg());
	if((fs->fd = open(dev, ORDWR)) == -1)
		sysfatal("open %s: %r", dev);
	if((d = dirfstat(fs->fd)) == nil)
		sysfatal("ream O: %r");
	sz = d->length;
	free(d);

	print("reaming O volume %s\n", dev);
	if(sz < 128*MiB+Blksz)
		sysfatal("ream O: disk too small");
	sz = sz - sz%Blksz - 2*Blksz;
	fs->narena = (sz + 4096ULL*GiB - 1) / (4096ULL*GiB);
	if(fs->narena < 8)
		fs->narena = 8;
	if(fs->narena >= 32)
		fs->narena = 32;
	fs->arenas = emalloc(fs->narena*sizeof(Arena), 1);

	off = Blksz;
	asz = sz/fs->narena;
	asz = asz - (asz % Blksz) - 2*Blksz;

	sb0 = cachepluck();
	sb1 = cachepluck();
	sb0->bp = (Bptr){0, -1, -1};
	sb1->bp = (Bptr){sz+Blksz, -1, -1};

	fs->arenabp = emalloc(fs->narena * sizeof(Bptr), 1);
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		print("\tarena %d: %lld blocks at %llx\n", i, asz/Blksz, off);
		initarena(a, off, asz);
		fs->arenabp[i] = a->h0->bp;
		off += asz+2*Blksz;
	}
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		loadarena(a, a->h0->bp);
		loadlog(a, a->loghd);
	}

	/* The O volume stores flat object and owner records. */
	if(seeddir != nil){
		oseed(&root, &ht);
		fs->snap.bp = root;
		fs->snap.ht = ht;
	}else{
		if((tb = newblk(&fs->snap, Tleaf)) == nil)
			sysfatal("ream O: allocate volume header: %r");
		holdblk(tb);
		kbuf[0] = Kobj;
		PACK64(vbuf, 1);
		PACK64(vbuf+8, 1);
		PACK64(vbuf+16, 1);
		kv.key.k = kbuf;
		kv.key.nk = 1;
		kv.val.v = vbuf;
		kv.val.nv = 24;
		setval(tb, &kv);
		finalize(tb);
		syncblk(tb);
		fs->snap.ht = 1;
		fs->snap.bp = tb->bp;
	}
	fs->snapdl.hd.addr = -1;
	fs->snapdl.hd.hash = -1;
	fs->snapdl.tl.addr = -1;
	fs->snapdl.tl.hash = -1;
	fs->flag = Ovolflag | Ovolvers;
	fs->nextqid = 0;
	dropblk(tb);

	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		finalize(a->logtl);
		syncblk(a->logtl);
		packarena(a->h0->data, Blksz, a);
		finalize(a->h0);
		syncblk(a->h0);
		packarena(a->h1->data, Blksz, a);
		finalize(a->h1);
		syncblk(a->h1);
		fs->arenabp[i] = a->h0->bp;
		dropblk(a->h0);
		dropblk(a->h1);
	}

	packsb(sb0->buf, Blksz, fs);
	packsb(sb1->buf, Blksz, fs);
	finalize(sb0);
	finalize(sb1);
	syncblk(sb0);
	syncblk(sb1);
	dropblk(sb0);
	dropblk(sb1);
	poperror();
}
