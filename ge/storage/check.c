#include "gefs.h"

/*
 * Plan2001: the blocks check reaches (trees, data, logs, deadlists
 * and their entries), by address: a block neither free nor reached
 * is leaked.  Meaningful after a sync, with nothing in flight.
 */
enum { Rtree, Rdata, Rlog, Rdead, Nr };
static char	*rname[Nr] = { "tree", "data", "log", "dead" };

/*
 * Plan2001: a check's state is the volume's (Gefs.ck), not one global
 * set: two volumes may be checked at the same time, each by its own
 * console.
 */
typedef struct Ck Ck;
struct Ck {
	uchar	*reach;
	vlong	nreach;
	vlong	nreached[Nr];
	vlong	nleaf, nleaf0, nleaf1, nkv, npiv, nmsg, nclob;	/* the tree's shape */
};

static void
mark(Gefs *fs, vlong addr, int kind)
{
	Ck *ck = fs->ck;
	vlong i;

	i = addr/Blksz;
	if(ck->reach != nil && i >= 0 && i < ck->nreach && !(ck->reach[i/8] & 1<<(i%8))){
		ck->reach[i/8] |= 1<<(i%8);
		ck->nreached[kind]++;
	}
}

static int
isfree(Gefs *fs, vlong bp)
{
	Arange *r, q;
	Arena *a;

	q.off = bp;
	q.len = Blksz;

	a = getarena(fs, bp);
	r = (Arange*)avllookup(a->free, &q.avl, -1);
	if(r == nil)
		return 0;
	return bp < (r->off + r->len);
}

static int
checktree(Gefs *fs, int fd, Blk *b, int h, vlong pred, Kvp *lo, Kvp *hi)
{
	Ck *ck = fs->ck;
	Kvp x, y;
	Msg mx, my;
	int i, r, fill;
	Blk *c;
	int fail;
	Bptr bp;

	fail = 0;
	mark(fs, b->bp.addr, Rtree);
	if(b->type == Tleaf){
		ck->nleaf++;
		ck->nkv += b->nval;
		if(b->nval == 0)
			ck->nleaf0++;
		else if(b->nval < 4)
			ck->nleaf1++;
	}else{
		ck->npiv++;
		ck->nmsg += b->nbuf;
		for(i = 0; i < b->nbuf; i++){
			getmsg(b, i, &mx);
			if(mx.op == Oclobber)
				ck->nclob++;
		}
	}
	if(h < 0){
		fprint(fd, "node too deep (loop?\n");
		fail++;
		return fail;
	} 
	if(b->type == Tleaf){
		if(h != 0){
			fprint(fd, "unbalanced leaf\n");
			fail++;
		}
		if(h != 0 && b->nval < 2 && debug){
			fprint(fd, "warning: underfilled leaf %B\n", b->bp);
			fail++;
		}
	}
	if(b->type == Tpivot && b->nval < 2 && debug)
		fprint(fd, "warning: underfilled pivot %B\n", b->bp);
	getval(b, 0, &x);
	if(lo && keycmp(&lo->key, &x.key) > 0){
		fprint(fd, "out of range keys %P != %P\n", lo, &x);
		showblk(fs, fd, b, "out of range", 1);
		fail++;
	}
	for(i = 1; i < b->nval; i++){
		getval(b, i, &y);
		if(hi && keycmp(&y.key, &hi->key) >= 0){
			fprint(fd, "out of range keys %P >= %P\n", &y, hi);
			fail++;
		}
		if(b->type == Tpivot){
			bp = getptr(&x, &fill);
			/* Plan2001: the free list against every pointer, older ones too */
			if(isfree(fs, bp.addr)){
				fprint(fd, "freed block in use: %B\n", bp);
				fail++;
			}
			if(bp.gen <= pred)
				goto Skip;
			if((c = getblk(fs, bp, 0)) == nil){
				fprint(fd, "corrupt block: %B\n", bp);
				fail++;
				continue;
			}
			if(blkfill(c) != fill){
				fprint(fd, "mismatched block fill\n");
				fail++;
			}
			if(checktree(fs, fd, c, h - 1, pred, &x, &y))
				fail++;
			dropblk(c);
		}
Skip:
		r = keycmp(&x.key, &y.key);
		switch(r){
		case -1:
			break;
		case 0:
			fprint(fd, "duplicate keys %P, %P\n", &x, &y);
			fail++;
			break;
		case 1:
			fprint(fd, "misordered keys %P, %P\n", &x, &y);
			fail++;
			break;
		}
		x = y;
	}
	if(b->type == Tpivot){
		getval(b, b->nval-1, &y);
		bp = getptr(&x, &fill);
		if(isfree(fs, bp.addr)){	/* Plan2001: the last child was not looked at */
			fprint(fd, "freed block in use: %B\n", bp);
			fail++;
		}
		if(bp.gen > pred){
			if((c = getblk(fs, bp, 0)) == nil){
				fprint(fd, "corrupt block: %B\n", bp);
				fail++;
			}
			if(c != nil && checktree(fs, fd, c, h - 1, pred, &y, nil))
				fail++;
			dropblk(c);
		}
		if(b->nbuf > 0){
			getmsg(b, 0, &mx);
			if(hi && keycmp(&mx.kv.key, &hi->key) >= 0){
				fprint(fd, "out of range messages %P != %N\n", hi, &mx);
				fail++;
			}
		}
		for(i = 1; i < b->nbuf; i++){
			getmsg(b, i, &my);
			switch(my.op){
			case Owstat:		/* kvp dirent */
				if((my.kv.val.v[0] & ~(Owsize|Owmode|Owmtime|Owatime|Owuid|Owgid|Owmuid)) != 0){
					fprint(fd, "invalid stat op %x\n", my.kv.val.v[0]);
					fail++;
				}
				break;
			default:
				if(my.op <= 0 || my.op >= Nmsgtype){
					fprint(fd, "invalid message op %d\n", my.op);
					fail++;
				}
				break;
			}
			if(hi && keycmp(&y.key, &hi->key) > 0){
				fprint(fd, "out of range keys %P >= %P\n", &y, hi);
				fail++;
			}
			if(keycmp(&mx.kv.key, &my.kv.key) == 1){
				fprint(fd, "misordered keys %P, %P\n", &x, &y);
				fail++;
				break;
			}
			mx = my;
		}

	}
	return fail;
}

static int
checklog(Gefs *fs, int fd, Bptr hd, Bptr tl)
{
	Bptr pb, bp, nb;
	Blk *b;
	
	pb = Zb;
	for(bp = hd; bp.addr != -1; bp = nb){
		if(waserror()){
			fprint(fd, "error loading %B\n", bp);
			return 0;
		}
		pb = bp;
		mark(fs, bp.addr, Rlog);
		if(isfree(fs, bp.addr)){	/* Plan2001 */
			fprint(fd, "freed log block in use: %B\n", bp);
			poperror();
			return 0;
		}
		b = getblk(fs, bp, 0);
		nb = b->logp;
		dropblk(b);
		poperror();
		if(bp.addr == tl.addr)
			break;
	}
	if(tl.addr != -1 && pb.addr != tl.addr){
		fprint(fd, "truncated chain %B\n", hd);
		return 0;
	}
	return 1;
}

static int
checkfree(Gefs *fs, int fd)
{
	Arena *a;
	Arange *r, *n;
	int i, ok;

	ok = 1;
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(&a->qlk);
		r = (Arange*)avlmin(a->free);
		for(n = (Arange*)avlnext(&r->avl); n != nil; n = (Arange*)avlnext(&n->avl)){
			if(r->off >= n->off){
				fprint(fd, "misordered length %llx >= %llx\n", r->off, n->off);
				ok = 0;
			}
			if(r->off+r->len >= n->off){
				fprint(fd, "overlaping range %llx+%llx >= %llx\n", r->off, r->len, n->off);
				ok = 0;
			}
			r = n;
		}
		if(!checklog(fs, fd, a->loghd, Zb)){
			fprint(fd, "arena %d: broken freelist\n", i);
			ok = 0;
		}
		qunlock(&a->qlk);
	}
	return ok;
}

/* Plan2001: a deadlist's entries, the killed blocks, are not free yet */
static void
markdl(Gefs *fs, Bptr hd, Bptr tl)
{
	Bptr bp, nb;
	Blk *b;
	char *p;
	int done;

	done = 0;
	for(bp = hd; bp.addr != -1 && !done; bp = nb){
		b = getblk(fs, bp, 0);
		for(p = b->data; p != b->data+b->logsz; p += 8)
			mark(fs, UNPACK64(p), Rdead);
		nb = b->logp;
		done = (bp.addr == tl.addr);
		dropblk(b);
	}
}

static int
checkdlist(Gefs *fs, int fd)
{
	char pfx[1];
	Dlist dl;
	Scan s;
	int ok;

	ok = 1;
	if(!checklog(fs, fd, fs->snapdl.hd, fs->snapdl.tl)){
		fprint(fd, "bad snap dlist (%B, %B): %s\n", fs->snapdl.hd, fs->snapdl.tl, errmsg());
		ok = 0;
	}else
		markdl(fs, fs->snapdl.hd, fs->snapdl.tl);
	pfx[0] = Kdlist;
	btnewscan(fs, &s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		kv2dlist(&s.kv, &dl);
		if(!checklog(fs, fd, dl.hd, dl.tl)){
			fprint(fd, "bad dlist %P: %s\n", &s.kv, errmsg());
			ok = 0;
		}else
			markdl(fs, dl.hd, dl.tl);
	}
	btexit(&s);
	return ok;
}

static int
checkdata(Gefs *fs, int fd, Tree *t, vlong pred)
{
	char pfx[1];
	Bptr bp;
	Scan s;
	Blk *b;

	pfx[0] = Kdat;
	btnewscan(fs, &s, pfx, 1);
	btenter(t, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		if(waserror()){
			btexit(&s);
			nexterror();
		}
		bp = unpackbp(s.kv.val.v, s.kv.val.nv);
		mark(fs, bp.addr, Rdata);
		if(isfree(fs, bp.addr)){	/* Plan2001: older data too */
			fprint(fd, "free block in use: %B\n", bp);
			error("free block in use");
		}
		if(bp.gen > pred){
			b = getblk(fs, bp, GBraw);
			dropblk(b);
		}
		poperror();
	}
	btexit(&s);
	return 0;
}

static vlong
countlbl(Gefs *fs, int fd, vlong id)
{
	char pfx[1];
	int nref;
	Scan s;

	nref = 0;
	pfx[0] = Klabel;
	btnewscan(fs, &s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		if(s.kv.val.nv >= 9 && UNPACK64(s.kv.val.v+1) == id){
			fprint(fd, "\tlabel %.*s => %lld\n", (int)(s.kv.key.nk-1), s.kv.key.k+1, UNPACK64(s.kv.val.v+1));
			nref++;
		}
	}
	btexit(&s);
	return nref;
}

static vlong
countref(Gefs *fs, vlong id)
{
	char pfx[1];
	Tree t;
	int nref;
	Scan s;

	nref = 0;
	pfx[0] = Ksnap;
	btnewscan(fs, &s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		unpacktree(fs, &t, s.kv.val.v, s.kv.val.nv);
		if(t.pred == -1 && t.base == id)
			nref++;
	}
	btexit(&s);
	return nref;
}

/* Plan2001: the blocks neither free nor reached */
static int
checkleak(Gefs *fs, int fd)
{
	Ck *ck = fs->ck;
	vlong addr, base, n, i;
	Arena *a;
	int j;

	n = 0;
	for(j = 0; j < fs->narena; j++){
		a = &fs->arenas[j];
		base = a->h0->bp.addr + 2*Blksz;
		for(addr = base; addr < base + a->size; addr += Blksz){
			i = addr/Blksz;
			if(i >= ck->nreach || (ck->reach[i/8] & 1<<(i%8)) || isfree(fs, addr))
				continue;
			/*
			 * not read: getblk would cache it with gen -1, and the
			 * next getblk of the address fails its gen assert
			 */
			if(n++ < 10)
				fprint(fd, "leaked block %llx\n", addr);
		}
	}
	fprint(fd, "leaked blocks: %lld (%lld KiB)\n", n, n*Blksz/1024);
	for(j = 0; j < Nr; j++)
		fprint(fd, "reached %s: %lld KiB\n", rname[j], ck->nreached[j]*Blksz/1024);
	fprint(fd, "reached leaves %lld (empty %lld, < 4 %lld) kvs %lld; pivots %lld msgs %lld clobbers %lld\n",
		ck->nleaf, ck->nleaf0, ck->nleaf1, ck->nkv, ck->npiv, ck->nmsg, ck->nclob);
	return n == 0;
}

int
checkfs(Gefs *fs, int fd)
{
	Ck *ck;
	int ok, height, nref, nlbl;
	vlong gen;
	char pfx[1];
	Arena *a;
	Tree *t;
	Scan s;
	Blk *b;

	ok = 1;
	if(fs->ck != nil)
		error("gefs: check already running");
	assert(!canqlock(&fs->mutlk));
	ck = fs->ck = emalloc(sizeof(Ck), 1);
	a = &fs->arenas[fs->narena-1];
	ck->nreach = (a->h0->bp.addr + a->size + 2*Blksz)/Blksz + 1;
	ck->reach = emalloc((ck->nreach+7)/8, 1);
	memset(ck->nreached, 0, sizeof(ck->nreached));
	ck->nleaf = ck->nleaf0 = ck->nleaf1 = ck->nkv = ck->npiv = ck->nmsg = ck->nclob = 0;
	mark(fs, fs->sb0->bp.addr, Rlog);
	mark(fs, fs->sb1->bp.addr, Rlog);
	if(waserror()){
		fprint(fd, "error checking %s\n", errmsg());
		free(ck->reach);
		free(ck);
		fs->ck = nil;
		return 0;
	}
	fprint(fd, "checking freelist\n");
	if(!checkfree(fs, fd))
		ok = 0;
	fprint(fd, "checking deadlist\n");
	if(!checkdlist(fs, fd))
		ok = 0;
	fprint(fd, "checking snap tree: %B\n", fs->snap.bp);
	if((b = getroot(&fs->snap, &height)) != nil){
		if(checktree(fs, fd, b, height-1, -1, nil, 0))
			ok = 0;
		dropblk(b);
	}
	pfx[0] = Ksnap;
	btnewscan(fs, &s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		gen = UNPACK64(s.kv.key.k+1);
		if(waserror()){
			fprint(fd, "moving on: %s\n", errmsg());
			ok = 0;
			continue;
		}
		if((t = opentree(fs, gen)) == nil){
			fprint(fd, "invalid snap id %lld\n", gen);
			ok = 0;
			poperror();
			continue;
		}
		fprint(fd, "checking snap %lld: %B\n", gen, t->bp);
		if(waserror()){
			closesnap(t);
			nexterror();
		}
		nref = countref(fs, gen);
		nlbl = countlbl(fs, fd, gen);
		fprint(fd, "\tnref %d nlbl %d\n", nref, nlbl);
		if(t->nref < nref || t->nlbl < nlbl){
			fprint(fd, "mismatched refs: (%d, %d) != (%d, %d)\n", t->nref, nref, t->nlbl, nlbl);
			ok = 0;
		}
		b = getroot(t, &height);
		if(waserror()){
			dropblk(b);
			nexterror();
		}
		if(checktree(fs, fd, b, height-1, t->pred, nil, 0))
			ok = 0;
		if(checkdata(fs, fd, t, t->pred))
			ok = 0;
		dropblk(b);
		poperror();
		closesnap(t);
		poperror();
		poperror();
	}
	btexit(&s);
	/* Plan2001: reported, not failed: in-flight frees look leaked */
	checkleak(fs, fd);
	free(ck->reach);
	ck->reach = nil;
	free(ck);
	fs->ck = nil;
	poperror();
	if(ok)
		fprint(fd, "ok\n");
	else
		fprint(fd, "not ok\n");
	return ok;
}
