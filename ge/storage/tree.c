#include "gefs.h"

typedef struct Path	Path;

struct Path {
	/* Flowing down for flush */
	Msg	*ins;	/* inserted values, bounded by lo..hi */
	Blk	*b;	/* to shadow */
	Blk	*s;	/* sibling of shadow, for merge/rotate */
	int	idx;	/* insert at */
	int	lo;	/* key range */
	int	hi;	/* key range */
	int	sz;	/* size of range */

	/* Flowing up from flush */
	int	op;	/* change done along path */
	Blk	*nl;	/* new left */
	Blk	*nr;	/* new right, if we split or rotated */
	int	midx;	/* modification index */
	int	npull;	/* number of messages successfully pulled */
	int	pullsz;	/* size of pulled messages */
};

#define efreeblk(t, b) do { \
	if(b != nil) \
		freeblk(fs, t, b); \
	} while(0)

static void
stablesort(Msg *m, int nm)
{
	int i, j;
	Msg t;

	for(i = 1; i < nm; i++){
		for(j = i; j > 0; j--){
			if(keycmp(&m[j-1].kv.key, &m[j].kv.key) <= 0)
				break;
			t = m[j-1];
			m[j-1] = m[j];
			m[j] = t;
		}
	}
}

void
cpkey(Key *dst, Key *src, char *buf, int nbuf)
{
	assert(src->nk <= nbuf);
	memmove(buf, src->k, src->nk);
	dst->k = buf;
	dst->nk = src->nk;
}

void
cpkvp(Kvp *dst, Kvp *src, char *buf, int nbuf)
{
	assert(src->key.nk+src->val.nv <= nbuf);
	memmove(buf, src->key.k, src->key.nk);
	memmove(buf+ src->key.nk, src->val.v, src->val.nv);
	dst->key.k = buf;
	dst->key.nk = src->key.nk;
	dst->val.v = buf+src->key.nk;
	dst->val.nv = src->val.nv;
}

int
keycmp(Key *a, Key *b)
{
	int c, n;

	n = (a->nk < b->nk) ? a->nk : b->nk;
	if((c = memcmp(a->k, b->k, n)) != 0)
		return c < 0 ? -1 : 1;
	if(a->nk < b->nk)
		return -1;
	else if(a->nk > b->nk)
		return 1;
	else
		return 0;
}

static int
msgsz(Msg *m)
{
	/* disp + op + klen + key + vlen + v */
	return 2+1+2+m->kv.key.nk +2+ m->kv.val.nv;
}

static int
valsz(Kvp *kv)
{
	return 2 + 2+kv->key.nk + 2+kv->val.nv;
}

void
getval(Blk *b, int i, Kvp *kv)
{
	char *p;
	int o;

	bassert(b, i >= 0 && i < b->nval);
	p = b->data + 2*i;
	o = UNPACK16(p);	p = b->data + o;
	kv->key.nk = UNPACK16(p);	p += 2;
	kv->key.k = p;		p += kv->key.nk;
	kv->val.nv = UNPACK16(p);	p += 2;
	kv->val.v = p;
}

Bptr
getptr(Kvp *kv, int *fill)
{
	assert(kv->val.nv == Ptrsz+2);
	*fill = UNPACK16(kv->val.v + Ptrsz);
	return unpackbp(kv->val.v, kv->val.nv);
}

/* Exported for reaming */
void
setval(Blk *b, Kvp *kv)
{
	int off, spc;
	char *p;

	spc = (b->type == Tleaf) ? Leafspc : Pivspc;
	b->valsz += 2 + kv->key.nk + 2 + kv->val.nv;
	off = spc - b->valsz;

	if(kv->key.k[0] == Kent) assert(kv->val.nv != 0);
	bassert(b, 2*(b->nval+1) + b->valsz <= spc);
	bassert(b, 2*(b->nval+1) <= off);

	p = b->data + 2*b->nval;
	PACK16(p, off);

	p = b->data + off;
	PACK16(p, kv->key.nk);		p += 2;
	memmove(p, kv->key.k, kv->key.nk);	p += kv->key.nk;
	PACK16(p, kv->val.nv);		p += 2;
	memmove(p, kv->val.v, kv->val.nv);

	b->nval++;
}

static void
setptr(Blk *b, Key *k, Bptr bp, int fill)
{
	char *p, buf[Ptrsz+2];
	Kvp kv;

	kv.key.k = k->k;
	kv.key.nk = k->nk;
	kv.val.v = buf;
	kv.val.nv = sizeof(buf);
	p = packbp(buf, sizeof(buf), &bp);
	PACK16(p, fill);
	setval(b, &kv);
}

static void
setmsg(Blk *b, Msg *m)
{
	char *p;
	int o;

	bassert(b, b->type == Tpivot);
	b->bufsz += msgsz(m)-2;
	bassert(b, 2*(b->nbuf+1) + b->bufsz <= Bufspc);

	p = b->data + Pivspc + 2*b->nbuf;
	o = Bufspc - b->bufsz;
	PACK16(p, o);

	p = b->data + Pivspc + o;
	*p = m->op;			p += 1;
	PACK16(p, m->kv.key.nk);		p += 2;
	memmove(p, m->kv.key.k, m->kv.key.nk);	p += m->kv.key.nk;
	PACK16(p, m->kv.val.nv);		p += 2;
	memmove(p, m->kv.val.v, m->kv.val.nv);

	b->nbuf++;
}

void
getmsg(Blk *b, int i, Msg *m)
{
	char *p;
	int o;

	bassert(b, b->type == Tpivot);
	bassert(b, i >= 0 && i < b->nbuf);
	p = b->data + Pivspc + 2*i;
	o = UNPACK16(p);
	p = b->data + Pivspc + o;
	m->op = *p;		p += 1;
	m->kv.key.nk = UNPACK16(p);	p += 2;
	m->kv.key.k = p;		p += m->kv.key.nk;
	m->kv.val.nv = UNPACK16(p);	p += 2;
	m->kv.val.v = p;
}

static int
bufsearch(Blk *b, Key *k, Msg *m, int *same)
{
	int lo, hi, ri, mid, r;
	Msg cmp;

	ri = -1;
	lo = 0;
	hi = b->nbuf-1;
	while(lo <= hi){
		mid = (hi + lo) / 2;
		getmsg(b, mid, &cmp);
		r = keycmp(k, &cmp.kv.key);
		switch(r){
		case -1:
			hi = mid-1;
			break;
		case 0:
			ri = mid;
			hi = mid-1;
			break;
		case 1:
			lo = mid+1;
			break;
		}
	}
	/*
	 * we can have duplicate messages, and we
	 * want to point to the first of them:
	 * scan backwards.
	 */
	*same = 0;
	if(ri == -1)
		ri = lo-1;
	else
		*same = 1;
	if(m != nil && ri >= 0)
		getmsg(b, ri, m);
	return ri;
}

static int
blksearch(Blk *b, Key *k, Kvp *rp, int *same)
{
	int lo, hi, ri, mid, r;
	Kvp cmp;

	ri = -1;
	lo = 0;
	hi = b->nval-1;
	while(lo <= hi){
		mid = (hi + lo) / 2;
		getval(b, mid, &cmp);
		r = keycmp(k, &cmp.key);
		switch(r){
		case -1:
			hi = mid-1;
			break;
		case 0:
			ri = mid;
			hi = mid-1;
			break;
		case 1:
			lo = mid+1;
			break;
		}
	}
	*same = 0;
	if(ri == -1)
		ri = lo-1;
	else
		*same = 1;
	if(ri >= 0)
		getval(b, ri, rp);
	return ri;
}

static int
buffill(Blk *b)
{
	bassert(b, b->type == Tpivot);
	return 2*b->nbuf + b->bufsz;
}

static int
filledbuf(Blk *b, int nmsg, int needed)
{
	bassert(b, b->type == Tpivot);
	return 2*(b->nbuf+nmsg) + b->bufsz + needed > Bufspc;
}

static int
filledleaf(Blk *b, int needed)
{
	bassert(b, b->type == Tleaf);
	return 2*(b->nval+1) + b->valsz + needed > Leafspc;
}

static int
filledpiv(Blk *b, int reserve)
{
	/* 
	 * We need to guarantee there's room for one message
	 * at all times, so that splits along the whole path
	 * have somewhere to go as they propagate up. For
	 * each key-ptr pair we have 8 bytes of overhead,
	 * so remember to account for that: 2 bytes of fill,
	 * 2 bytes of offset table, and 2 bytes of length
	 * for both key and value.
	 */
	bassert(b, b->type == Tpivot);
	return 2*(b->nval+1) + b->valsz + reserve*(8+Keymax+Ptrsz) > Pivspc;
}

static void
copyup(Blk *n, Path *pp, int *nbytes)
{
	Kvp kv;
	Msg m;

	/*
	 * It's possible for the previous node to have
	 * been fully cleared out by a large number of
	 * delete messages, so we need to check if
	 * there's anything in it to copy up.
	 */
	if(pp->nl != nil){
		getval(pp->nl, 0, &kv);
		if(pp->nl->nbuf > 0){
			getmsg(pp->nl, 0, &m);
			if(keycmp(&kv.key, &m.kv.key) > 0)
				kv.key = m.kv.key;
		}
		setptr(n, &kv.key, pp->nl->bp, blkfill(pp->nl));
		if(nbytes != nil)
			*nbytes += valsz(&kv);
	}
	if(pp->nr != nil){
		getval(pp->nr, 0, &kv);
		if(pp->nr->nbuf > 0){
			getmsg(pp->nr, 0, &m);
			if(keycmp(&kv.key, &m.kv.key) > 0)
				kv.key = m.kv.key;
		}
		setptr(n, &kv.key, pp->nr->bp, blkfill(pp->nr));
		if(nbytes != nil)
			*nbytes += valsz(&kv);
	}
}

static int
apply(Gefs *fs, Kvp *kv, Msg *m, char *buf, int nbuf)
{
	char *p;
	Tree t;

	switch(m->op){
	case Odelete:
		assert(keycmp(&kv->key, &m->kv.key) == 0);
		return 0;
	case Oclearb:
	case Oclobber:
		return 0;
	case Oinsert:
		cpkvp(kv, &m->kv, buf, nbuf);
		return 1;
	case Orelink:
	case Oreprev:
	case Oincref:
		unpacktree(fs, &t, kv->val.v, kv->val.nv);
		p = m->kv.val.v;
		if(m->op == Orelink)
			t.succ = UNPACK64(p);
		else if(m->op == Oreprev)
			t.pred = UNPACK64(p);
		p += 8;
		t.nlbl += *p;		p++;
		t.nref += *p;		p++;
		assert(t.nlbl >= 0 && t.nref >= 0);
		assert(p == m->kv.val.v + m->kv.val.nv);
		packtree(kv->val.v, kv->val.nv, &t);
		return 1;
	default:
		fatal("invalid op %d\n", m->op);
	}
}

static void
setb(Blk **dst, Tree *t, Blk *b)
{
	Gefs *fs = t->limbo.fs;
	if(*dst != nil){
		freeblk(fs, t, *dst);
		dropblk(*dst);
	}
	if(b->nval == 0){
		freeblk(fs, t, b);
		dropblk(b);
		*dst = nil;
	}else{
		enqueue(b);
		*dst = b;
	}
}
		

static int
pullmsg(Path *p, int i, Kvp *v, Msg *m, int *full, int spc)
{
	if(i < 0 || i >= p->hi || *full)
		return -1;

	if(p->ins != nil)
		*m = p->ins[i];
	else
		getmsg(p->b, i, m);
	if(msgsz(m) <= spc)
		return (v == nil) ? 0 : keycmp(&v->key, &m->kv.key);
	*full = 1;
	return -1;
}

/*
 * Creates a new block with the contents of the old
 * block. When copying the contents, it repacks them
 * to minimize the space uses, and applies the changes
 * pending from the downpath blocks.
 *
 * When pidx != -1, 
 */
static void
updateleaf(Tree *t, Path *pup, Path *p)
{
	Gefs *fs = t->limbo.fs;
	char buf[Msgmax];
	int i, j, c, ok, full, spc;
	Blk *b, *n;
	Bptr bp;
	Msg m;
	Kvp v;

	i = 0;
	j = pup->lo;
	b = p->b;
	/*
	 * spc is the amount of room we have
	 * to copy data down from the parent; it's
	 * necessarily a bit conservative, because
	 * deletion messages don't take space -- but
	 * we don't know how what the types of all
	 * messages are.
	 */
	full = 0;
	spc = Leafspc - blkfill(b);
	n = newblk(t, b->type);
	assert(i >= 0 && j >= 0);
	while(i < b->nval || j < pup->hi){
		if(i >= b->nval)
			c = 1;
		else{
			c = -1;
			getval(p->b, i, &v);
			if(j < pup->hi){
				if(pup->ins != nil)
					m = pup->ins[j];
				else
					getmsg(pup->b, j, &m);
				if(msgsz(&m) <= spc)
					c = keycmp(&v.key, &m.kv.key);
				else
					full = 1;
			}
		}
		switch(c){
		/* Value before message: just copy value */
		case -1:
			i++;
			setval(n, &v);
			break;
		/* Value merges with message sequence */
		case 0:
			i++;
			j++;
			cpkvp(&v, &v, buf, sizeof(buf));
			if(v.key.nk > 0 && v.key.k[0] == Kdat)
			if(m.op == Oclearb
			|| m.op == Oinsert
			|| m.op == Odelete){
				bp = unpackbp(v.val.v, v.val.nv);
				freebp(fs, t, bp);
			}
			ok = apply(fs, &v, &m, buf, sizeof(buf));
			goto Copyloop;
		/* Message before value: Insert message sequence */
		case 1:
			j++;
			cpkvp(&v, &m.kv, buf, sizeof(buf));
			ok = 0;
			if(m.op != Oclearb && m.op != Oclobber){
				/* New keys need to start off with Oinsert */
				if(m.op != Oinsert)
					broke(fs, "%s: broken entry: %N\n", Efs, &m);
				spc -= valsz(&m.kv);
				p->pullsz += msgsz(&m);
				ok = 1;
			}
			goto Copyloop;
		Copyloop:
			while(j < pup->hi){
				if(pullmsg(pup, j, &v, &m, &full, spc) != 0)
					break;
				assert(!full);
				if(ok && v.key.nk > 0 && v.key.k[0] == Kdat)
				if(m.op == Oclearb
				|| m.op == Oinsert
				|| m.op == Odelete){
					bp = unpackbp(v.val.v, v.val.nv);
					freebp(fs, t, bp);
				}
				p->pullsz += msgsz(&m);
				ok = apply(fs, &v, &m, buf, sizeof(buf));
				j++;
			}
			if(ok)
				setval(n, &v);
			break;
		}
	}
	p->npull = (j - pup->lo);
	p->op = POmod;
	setb(&p->nl, t, n);
}

/*
 * Creates a new block with the contents of the old
 * block. When copying the contents, it repacks them
 * to minimize the space uses, and applies the changes
 * pending from the downpath blocks.
 *
 * When pidx != -1, 
 */
static void
updatepiv(Tree *t, Path *pup, Path *p, Path *pp)
{
	char buf[Msgmax];
	int i, j, r, sz, full, spc;
	Blk *b, *n;
	Msg m, u;

	b = p->b;
	n = newblk(t, b->type);
	for(i = 0; i < b->nval; i++){
		if(pp != nil && i == p->midx){
			copyup(n, pp, nil);
			if(pp->op == POrot || pp->op == POmerge)
				i++;
		}else{
			getval(b, i, &m.kv);
			setval(n, &m.kv);
		}
	}
	i = 0;
	j = pup->lo;
	sz = 0;
	full = 0;
	/*
	 * Compute free space for pulling messages
	 * from the parent node. The amount of space
	 * is the space we currently have, plus the
	 * amount we pulled down from this node in
	 * our child.
	 */
	spc = Bufspc - buffill(b);
	if(pp != nil)
		spc += pp->pullsz;
	while(i < b->nbuf){
		if(i == p->lo)
			i += pp->npull;
		if(i == b->nbuf)
			break;
		getmsg(b, i, &m);
		/*
		 * a bit tricky: we're signalling full full not when we're done
		 * copying, but when we're out of room for new messages. The
		 * amount of consumed space here is coming from messages that
		 * we pull down from the parent node. If not, we keep cloning
		 * messages without pulling. We do not want to break out when
		 * we report a full buffer here -- it's only too full for new
		 * data when that happens.
		 */
		r = pullmsg(pup, j, &m.kv, &u, &full, spc - sz);
		switch(r){
		case -1:
		case 0:
			setmsg(n, &m);
			i++;
			break;
		case 1:
			cpkvp(&m.kv, &u.kv, buf, sizeof(buf));
			while(pullmsg(pup, j, &m.kv, &u, &full, spc) == 0){
				setmsg(n, &u);
				sz = msgsz(&u);
				p->pullsz += sz;
				spc -= sz;
				j++;
			}
		}
	}
	while(j < pup->hi){
		pullmsg(pup, j, nil, &u, &full, spc);
		if(full)
			break;
		setmsg(n, &u);
		sz = msgsz(&u);
		p->pullsz += sz;
		spc -= sz;
		j++;
	}
	p->npull = (j - pup->lo);
	p->op = POmod;
	setb(&p->nl, t, n);
}

/*
 * Splits a node, returning the block that msg
 * would be inserted into. Split must never
 * grow the total height of the tree by more than 1.
 */
static void
splitleaf(Tree *t, Path *pup, Path *p)
{
	Gefs *fs = t->limbo.fs;
	char buf[Msgmax];
	Blk *b, *d, *l, *r;
	int full, copied, spc, ok, halfsz;
	int i, j, c;
	Bptr bp;
	Msg m;
	Kvp v;

	/*
	 * If the block one entry up the
	 * p is nil, we're at the root,
	 * so we want to make a new block.
	 */
	b = p->b;
	l = newblk(t, b->type);
	r = newblk(t, b->type);
	if(waserror()){
		efreeblk(t, l);
		efreeblk(t, r);
		nexterror();
	}

	d = l;
	i = 0;
	j = pup->lo;
	full = 0;
	copied = 0;
	halfsz = (2*b->nval + b->valsz + pup->sz) / 2;
	if(halfsz > Leafspc/2)
		halfsz = Leafspc/2;
	spc = Leafspc - (halfsz + Msgmax);
	bassert(b, b->nval >= 4);
	while(i < b->nval){
		/*
		 * We're trying to balance size,
		 * but we need at least 2 nodes
		 * in each half of the split if
		 * we want a valid tree.
		 */
		if(d == l)
		if((i == b->nval-2) || (i >= 2 && copied >= halfsz)){
			d = r;
			spc = Leafspc - (halfsz + Msgmax);
		}
		getval(b, i, &v);
 		c = pullmsg(pup, j, &v, &m, &full, spc);
		switch(c){
		case -1:
			i++;
			setval(d, &v);
			copied += valsz(&v);
			break;
		case 0:
			i++;
			j++;
			cpkvp(&v, &v, buf, sizeof(buf));
			copied += valsz(&v);
			if(v.key.nk > 0 && v.key.k[0] == Kdat)
			if(m.op == Oclearb
			|| m.op == Oinsert
			|| m.op == Odelete){
				bp = unpackbp(v.val.v, v.val.nv);
				freebp(fs, t, bp);
			}
			ok = apply(fs, &v, &m, buf, sizeof(buf));
			goto Copyloop;
		case 1:
			j++;
			cpkvp(&v, &m.kv, buf, sizeof(buf));
			copied += valsz(&v);
			ok = 0;
			if(m.op != Oclearb && m.op != Oclobber){
				/* New keys need to start off with Oinsert */
				if(m.op != Oinsert)
					broke(fs, "%s: broken entry: %N\n", Efs, &m);
				spc -= valsz(&m.kv);
				p->pullsz += msgsz(&m);
				ok = 1;
			}
			goto Copyloop;
		Copyloop:
			while(j < pup->hi){
				if(pullmsg(pup, j, &v, &m, &full, spc) != 0)
					break;
				if(ok && v.key.nk > 0 && v.key.k[0] == Kdat)
				if(m.op == Oclearb
				|| m.op == Oinsert
				|| m.op == Odelete){
					bp = unpackbp(v.val.v, v.val.nv);
					freebp(fs, t, bp);
				}
				p->pullsz += msgsz(&m);
				ok = apply(fs, &v, &m, buf, sizeof(buf));
				j++;
			}
			if(ok)
				setval(d, &v);
			break;
		}
	}
	p->npull = (j - pup->lo);
	p->op = POsplit;
	setb(&p->nl, t, l);
	setb(&p->nr, t, r);
	poperror();
}

/*
 * Splits a node, returning the block that msg
 * would be inserted into. Split must never
 * grow the total height of the tree by more
 * than one.
 */
static void
splitpiv(Tree *t, Path *, Path *p, Path *pp)
{
	Gefs *fs = t->limbo.fs;
	int i, copied, halfsz;
	Blk *b, *d, *l, *r;
	Kvp tk, mid;
	Msg m;

	/*
	 * If the bp->lock one entry up the
	 * p is nil, we're at the root,
	 * so we want to make a new bp->lock.
	 */
	b = p->b;
	l = newblk(t, b->type);
	r = newblk(t, b->type);
	if(waserror()){
		efreeblk(t, l);
		efreeblk(t, r);
		nexterror();
	}
	d = l;
	copied = 0;
	halfsz = (2*b->nval + b->valsz)/2;
	bassert(b, b->nval >= 4);
	for(i = 0; i < b->nval; i++){
		/*
		 * Balance size, but ensure we have at least 2 nodes
		 * in each half of the split so we have a valid tree.
		 */
		if(d == l && (i == b->nval-2 || (i >= 2 && copied >= halfsz)))
			d = r;
		if(i == p->idx){
			copyup(d, pp, &copied);
			continue;
		}
		getval(b, i, &tk);
		setval(d, &tk);
		copied += valsz(&tk);
	}
	d = l;
	getval(r, 0, &mid);
	for(i = 0; i < b->nbuf; i++){
		if(i == p->lo)
			i += pp->npull;
		if(i == b->nbuf)
			break;
		getmsg(b, i, &m);
		if(d == l && keycmp(&m.kv.key, &mid.key) >= 0)
			d = r;
		setmsg(d, &m);
	}
	p->op = POsplit;
	setb(&p->nl, t, l);
	setb(&p->nr, t, r);
	poperror();
}

static void
merge(Tree *t, Path *p, Path *pp, int idx, Blk *a, Blk *b)
{
	Blk *d;
	Msg m;
	int i;

	d = newblk(t, a->type);
	for(i = 0; i < a->nval; i++){
		getval(a, i, &m.kv);
		setval(d, &m.kv);
	}
	for(i = 0; i < b->nval; i++){
		getval(b, i, &m.kv);
		setval(d, &m.kv);
	}
	if(a->type == Tpivot){
		for(i = 0; i < a->nbuf; i++){
			getmsg(a, i, &m);
			setmsg(d, &m);
		}
		for(i = 0; i < b->nbuf; i++){
			getmsg(b, i, &m);
			setmsg(d, &m);
		}
	}
	p->midx = idx;
	pp->op = POmerge;
	setb(&pp->nl, t, d);
}

/*
 * Find the first message in buffers of l and r with key >= m.
 * Trybalance already guarantees we have space for any split
 * that we pick here.
 */
static int
splitidx(Blk *l, Blk *r, Msg *m, int idx)
{
	int i;
	Msg n;

	if(l->type == Tleaf)
		return idx;
	for(i = idx; i < l->nbuf + r->nbuf; i++){
		if(i < l->nbuf)
			getmsg(l, i, &n);
		else
			getmsg(r, i - l->nbuf, &n);
		if(keycmp(&m->kv.key, &n.kv.key) <= 0)
			break;
	}
	return i;
}

static void
rotate(Tree *t, Path *p, Path *pp, int midx, Blk *a, Blk *b, int halfpiv)
{
	Gefs *fs = t->limbo.fs;
	int i, o, sz, sp;
	Blk *d, *l, *r;
	Msg m;

	l = newblk(t, a->type);
	r = newblk(t, a->type);
	if(waserror()){
		efreeblk(t, l);
		efreeblk(t, r);
		nexterror();
	}
	d = l;
	sz = 0;
	sp = 0;
	for(i = 0; i < a->nval; i++){
		getval(a, i, &m.kv);
		if(d == l){
			sp = splitidx(a, b, &m, sp);
			if(sz >= halfpiv)
				d = r;
		}
		setval(d, &m.kv);
		sz += valsz(&m.kv);
	}
	for(i = 0; i < b->nval; i++){
		getval(b, i, &m.kv);
		if(d == l){
			sp = splitidx(a, b, &m, sp);
			if(sz >= halfpiv)
				d = r;
		}
		setval(d, &m.kv);
		sz += valsz(&m.kv);
	}
	if(a->type == Tpivot){
		d = l;
		o = 0;
		for(i = 0; i < a->nbuf; i++){
			if(o == sp){
				d = r;
				o = 0;
			}
			getmsg(a, i, &m);
			setmsg(d, &m);
			o++;
		}
		for(i = 0; i < b->nbuf; i++){
			if(o == sp){
				d = r;
				o = 0;
			}
			getmsg(b, i, &m);
			setmsg(d, &m);
			o++;
		}
	}
	p->midx = midx;
	pp->op = POrot;
	setb(&pp->nl, t, l);
	setb(&pp->nr, t, r);
	poperror();
}

static int
rotmerge(Tree *t, Path *p, Path *pp, int idx, Blk *a, Blk *b)
{
	int na, nb, ma, mb, imbalance;

	bassert(a, a->type == b->type);

	na = 2*a->nval + a->valsz;
	nb = 2*b->nval + b->valsz;
	if(a->type == Tleaf){
		ma = 0;
		mb = 0;
	}else{
		ma = 2*a->nbuf + a->bufsz;
		mb = 2*b->nbuf + b->bufsz;
	}
	imbalance = na - nb;
	if(imbalance < 0)
		imbalance *= -1;
	/* works for leaf, because 0 always < Bufspc */
	if(na + nb < (Pivspc - 4*Msgmax) && ma + mb < Bufspc){
		merge(t, p, pp, idx, a, b);
		return 1;
	}else if(imbalance > 4*Msgmax){
		rotate(t, p, pp, idx, a, b, (na + nb)/2);
		return 1;
	}
	return 0;
}

static void
trybalance(Tree *t, Path *p, Path *pp, int idx)
{
	Gefs *fs = t->limbo.fs;
	Blk *l, *m, *r;
	Kvp kl, kr;
	int spc, fill;
	Bptr bp;

	if(p->idx == -1 || pp == nil || pp->nl == nil)
		return;
	if(pp->op != POmod && pp->op != POmerge)
		return;

	l = nil;
	r = nil;
	m = holdblk(pp->nl);
	if(waserror()){
		dropblk(m);
		nexterror();
	}
	spc = (m->type == Tleaf) ? Leafspc : Pivspc;
	if(idx-1 >= 0){
		getval(p->b, idx-1, &kl);
		bp = getptr(&kl, &fill);
		if(fill + blkfill(m) < spc){
			l = getblk(fs, bp, 0);
			if(waserror()){
				dropblk(l);
				nexterror();
			}
			if(rotmerge(t, p, pp, idx-1, l, m))
				p->s = holdblk(l);
			poperror();
			goto Done;
		}
	}
	if(idx+1 < p->b->nval){
		getval(p->b, idx+1, &kr);
		bp = getptr(&kr, &fill);
		if(fill + blkfill(m) < spc){
			r = getblk(fs, bp, 0);
			if(waserror()){
				dropblk(r);
				nexterror();
			}
			if(rotmerge(t, p, pp, idx, m, r))
				p->s = holdblk(r);
			poperror();
			goto Done;
		}
	}
Done:
	dropblk(m);
	dropblk(l);
	dropblk(r);
	poperror();
}

static Path*
flush(Tree *t, Path *path, int npath)
{

	Path *pup, *p, *pp, *rp;

	/*
	 * The path must contain at minimum two elements:
	 * we must have 1 node we're inserting into, and
	 * an empty element at the top of the path that
	 * we put the new root into if the root gets split.
	 */
	assert(npath >= 2);
	rp = nil;
	pp = nil;
	p = &path[npath - 1];
	pup = &path[npath - 2];
	if(p->b->type == Tleaf){
		if(!filledleaf(p->b, pup->sz)){
			updateleaf(t, p-1, p);
			rp = p;
		}else{
			splitleaf(t, pup, p);
		}
		p->midx = -1;
		pp = p;
		pup--;
		p--;
	}
	while(p != path){
		/*
		 * While we will add at most one key to a tree,
		 * we can mutate the children so that we end up
		 * replacing small nodes with large ones; as a
		 * result we need to be a bit more conservative
		 * here, and split earlier than we may otherwise
		 * want to do.
		 */
		if(!filledpiv(p->b, 2)){
			trybalance(t, p, pp, p->idx);
			/*
			 * if we merged the root node, and the
			 * buffer was empty, break out; if we
			 * still have data in the buffer, pull
			 * it down and allow for a degenerate
			 * root node that we retry in upsert.
			 *
			 * if we cleared a degenerate root,
			 * then we want to drop it from the
			 * tree.
			 */
			if(pup == path			/* at root */
			&& pp != nil			/* has child */
			&& pp->nl != nil		/* didn't drop this node */
			&& pp->nr == nil		/* no siblings */
			&& pp->npull == p->b->nbuf){	/* got the whole buffer */
				if(pp->op == POmerge && p->b->nval == 2
				|| pp->op == POmod && p->b->nval == 1){
					pp->npull = p->npull;
					rp = pp;
					goto Out;
				}
			}
			updatepiv(t, pup, p, pp);
			rp = p;
		}else{
			splitpiv(t, pup, p, pp);
		}
		pp = p;
		pup--;
		p--;
	}
	if(pp->nl != nil && pp->nr != nil){
		rp = &path[0];
		rp->nl = newblk(t, Tpivot);
		rp->npull = pp->npull;
		rp->pullsz = pp->pullsz;
		copyup(rp->nl, pp, nil);
		enqueue(rp->nl);
	}
Out:
	return rp;
}

static void
freepath(Tree *t, Path *path, int npath, int ok)
{
	Gefs *fs = t->limbo.fs;
	Path *p;

	for(p = path; p != path + npath; p++){
		if(ok){
			if(p->b) freeblk(fs, t, p->b);
			if(p->s) freeblk(fs, t, p->s);
		}
		dropblk(p->b);
		dropblk(p->s);
		dropblk(p->nl);
		dropblk(p->nr);
	}
	free(path);
}

/*
 * Select child node that with the largest message
 * segment in the current node's buffer.
 */
static void
victim(Blk *b, Path *p)
{
	int i, j, lo, maxsz, cursz;
	Kvp kv;
	Msg m;

	j = 0;
	maxsz = 0;
	p->b = b;
	/* 
	 * Start at the second pivot: all values <= this
	 * go to the first node. Stop *after* the last entry,
	 * because entries >= the last entry all go into it.
	 */
	for(i = 1; i <= b->nval; i++){
		if(i < b->nval)
			getval(b, i, &kv);
		cursz = 0;
		lo = j;
		for(; j < b->nbuf; j++){
			getmsg(b, j, &m);
			if(i < b->nval && keycmp(&m.kv.key, &kv.key) >= 0)
				break;
			/* 2 bytes for offset, plus message size in buffer */
			cursz += msgsz(&m);
		}
		if(cursz > maxsz){
			maxsz = cursz;
			p->op = POmod;
			p->lo = lo;
			p->hi = j;
			p->sz = maxsz;
			p->idx = i - 1;
			p->midx = i - 1;
			p->npull = 0;
			p->pullsz = 0;
		}
	}
}

/*
 * Plan2001: as victim, the child that holds k: its messages in b's
 * buffer.  The next child's separator, if any, into *next (btdrain).
 */
static void
keyvictim(Blk *b, Path *p, Key *k, Kvp *next, char *buf, int nbuf, int *more)
{
	int i, j, lo, hi, sz, same;
	Kvp kv, lk, hk;
	Msg m;

	p->b = b;
	i = blksearch(b, k, &kv, &same);
	if(i == -1)
		i = 0;
	if(i > 0)
		getval(b, i, &lk);
	if(i+1 < b->nval){
		getval(b, i+1, &hk);
		cpkvp(next, &hk, buf, nbuf);
		*more = 1;
	}
	lo = 0;
	if(i > 0)
		for(; lo < b->nbuf; lo++){
			getmsg(b, lo, &m);
			if(keycmp(&m.kv.key, &lk.key) >= 0)
				break;
		}
	sz = 0;
	for(hi = lo; hi < b->nbuf; hi++){
		getmsg(b, hi, &m);
		if(i+1 < b->nval && keycmp(&m.kv.key, &hk.key) >= 0)
			break;
		sz += msgsz(&m);
	}
	j = i;
	p->op = POmod;
	p->lo = lo;
	p->hi = hi;
	p->sz = sz;
	p->idx = j;
	p->midx = j;
	p->npull = 0;
	p->pullsz = 0;
}

static void
fastupsert(Tree *t, Blk *b, Msg *msg, int nmsg)
{
	Gefs *fs = t->limbo.fs;
	int i, c, o, ri, lo, hi, mid, nbuf;
	Msg cmp;
	char *p;
	Blk *r;

	if((r = dupblk(t, b)) == nil)
		error(Enomem);

	nbuf = r->nbuf;
	for(i = 0; i < nmsg; i++)
		setmsg(r, &msg[i]);

	for(i = 0; i < nmsg; i++){
		ri = -1;
		lo = 0;
		hi = nbuf+i-1;
		while(lo <= hi){
			mid = (hi + lo) / 2;
			getmsg(r, mid, &cmp);
			c = keycmp(&msg[i].kv.key, &cmp.kv.key);
			switch(c){
			case -1:
				hi = mid-1;
				break;
			case 0:
				ri = mid+1;
				lo = mid+1;
				break;
			case 1:
				lo = mid+1;
				break;
			}
		}
		if(ri == -1)
			ri = hi+1;
		p = r->data + Pivspc + 2*(nbuf+i);
		o = UNPACK16(p);
		p = r->data + Pivspc + 2*ri;
		memmove(p+2, p, 2*(nbuf+i-ri));
		PACK16(p, o);
	}
	enqueue(r);

	lock(&t->lk);
	t->bp = r->bp;
	t->dirty = 1;
	unlock(&t->lk);

	freeblk(fs, t, b);
	dropblk(b);
	dropblk(r);
}
	

/*
 * deep: btdrain's.  The path goes down to the leaf of the key deep,
 * whatever room the pivots have, so that the messages for that leaf
 * on the way reach it.
 */
static int
btupsert1(Tree *t, Msg *msg, int nmsg, Key *deep, Kvp *next, char *nbuf, int nnbuf)
{
	Gefs *fs = t->limbo.fs;
	int i, npath, npull, dh, sz, height, degen, more;
	Path *path, *rp;
	Blk *b, *rb;
	Kvp sep;
	Bptr bp;

	assert(!canqlock(&fs->mutlk));
	sz = 0;
	stablesort(msg, nmsg);
	for(i = 0; i < nmsg; i++){
		assert(msg[i].kv.key.nk <= Keymax);
		assert(msg[i].kv.val.nv <= Inlmax);
		sz += msgsz(&msg[i]);
	}
	npull = 0;
	more = 0;
Again:
	b = getroot(t, &height);
	if(waserror()){
		if(!agetl(&fs->rdonly))	/* Plan2001: why, on the console */
			print("gefs: an update failed: %s: read-only\n", errmsg());
		aincl(&fs->rdonly, 1);
		dropblk(b);
		nexterror();
	}
	if(deep != nil && b->type == Tleaf){
		/* no pivots: no buffers */
		dropblk(b);
		poperror();
		return 0;
	}
	if(deep == nil && npull == 0 && b->type == Tpivot && b->nval > 1 && !filledbuf(b, nmsg, sz)){
		fastupsert(t, b, msg, nmsg);
		poperror();
		return 0;
	}
	/*
	 * The tree can grow in height by 1 when we
	 * split, so we allocate room for one extra
	 * node in the path.
	 */
	if((path = calloc((height + 2), sizeof(Path))) == nil)
		error(Enomem);
	poperror();
	if(waserror()){
		freepath(t, path, height+2, 0);	/* npath not volatile */
		nexterror();
	}
	degen = 0;
	npath = 0;
	path[npath].b = nil;
	path[npath].idx = -1;
	path[npath].midx = -1;
	npath++;

	path[0].sz = sz;
	path[0].ins = msg;
	path[0].lo = npull;
	path[0].hi = nmsg;
	while(b->type == Tpivot){
		if(deep != nil)
			keyvictim(b, &path[npath], deep, next, nbuf, nnbuf, &more);
		else if(b->nval > 1 && !filledbuf(b, nmsg, path[npath - 1].sz))
			break;
		else
			victim(b, &path[npath]);
		getval(b, path[npath].idx, &sep);
		bp = unpackbp(sep.val.v, sep.val.nv);
		b = getblk(fs, bp, 0);
		npath++;
		assert(npath < height+2);
	}
	path[npath].b = b;
	path[npath].idx = -1;
	path[npath].midx = -1;
	path[npath].lo = -1;
	path[npath].hi = -1;
	path[npath].npull = 0;
	path[npath].pullsz = 0;
	npath++;

	rp = flush(t, path, npath);
	rb = rp->nl;

	if(path[0].nl != nil)
		dh = 1;
	else if(path[1].nl != nil)
		dh = 0;
	else if(npath >2 && path[2].nl != nil)
		dh = -1;
	else
		fatal("broken path change");

	/*
	 * if we merged the root block, but there
	 * was still data stuck in the buffer, we
	 * should try again to clear out the data
	 * in the degenerate root.
	 */
	if(rb->type == Tpivot && rb->nval == 1)
		degen = 1;

	bassert(rb, rb->bp.addr != 0);

	lock(&t->lk);
	traceb("setroot", rb->bp);
	t->ht += dh;
	t->bp = rb->bp;
	t->dirty = 1;
	unlock(&t->lk);

	npull += rp->npull;
	freepath(t, path, npath, 1);
	poperror();

	if(npull != nmsg || degen){
		tracem("short pull");
		goto Again;
	}
	return more;
}

void
btupsert(Tree *t, Msg *msg, int nmsg)
{
	btupsert1(t, msg, nmsg, nil, nil, nil, 0);
}

/*
 * Plan2001: the messages for k's leaf, in the pivots' buffers on the
 * way, down into it.  Clobbers wait in the buffers until a buffer
 * fills, and the leaves keep what they clobber: a removed object
 * store's range, never written again, kept its values (T5.4).  The
 * next leaf's first key into *next: 0 at the tree's end.
 */
int
btdrain(Tree *t, Key *k, Kvp *next, char *buf, int nbuf)
{
	return btupsert1(t, nil, 0, k, next, buf, nbuf);
}

Blk*
getroot(Tree *t, int *h)
{
	Gefs *fs = t->limbo.fs;
	Bptr bp;

	lock(&t->lk);
	bp = t->bp;
	if(h != nil)
		*h = t->ht;
	unlock(&t->lk);

	return getblk(fs, bp, 0);
}

int
btlookup(Tree *t, Key *k, Kvp *r, char *buf, int nbuf)
{
	Gefs *fs = t->limbo.fs;
	int i, j, h, ok, same;
	Blk *b, **p;
	Bptr bp;
	Msg m;

	b = getroot(t, &h);
	if((p = calloc(h, sizeof(Blk*))) == nil){
		dropblk(b);
		error(Enomem);
	}
	if(waserror()){
		for(i = 0; i < h; i++)
			dropblk(p[i]);
		dropblk(b);
		free(p);
		nexterror();
	}
	ok = 0;
	p[0] = holdblk(b);
	for(i = 1; i < h; i++){
		if(blksearch(p[i-1], k, r, &same) == -1)
			break;
		bp = unpackbp(r->val.v, r->val.nv);
		p[i] = getblk(fs, bp, 0);
	}
	if(p[h-1] != nil)
		blksearch(p[h-1], k, r, &ok);
	if(ok)
		cpkvp(r, r, buf, nbuf);
	for(i = h-2; i >= 0; i--){
		if(p[i] == nil)
			continue;
		j = bufsearch(p[i], k, &m, &same);
		if(j < 0 || !same)
			continue;
		if(ok || m.op == Oinsert)
			ok = apply(fs, r, &m, buf, nbuf);
		else if(m.op != Oclearb && m.op != Oclobber)
			fatal("lookup %K << %N missing insert\n", k, &m);
		for(j++; j < p[i]->nbuf; j++){
			getmsg(p[i], j, &m);
			if(keycmp(k, &m.kv.key) != 0)
				break;
			ok = apply(fs, r, &m, buf, nbuf);
		}
	}
	for(i = 0; i < h; i++)
		dropblk(p[i]);
	dropblk(b);
	free(p);
	poperror();
	return ok;
}

void
btnewscan(Gefs *fs, Scan *s, char *pfx, int npfx)
{
	memset(s, 0, sizeof(*s));
	s->fs = fs;
	s->first = 1;
	s->donescan = 0;
	s->offset = 0;
	s->pfx.k = s->pfxbuf;
	s->pfx.nk = npfx;
	memmove(s->pfxbuf, pfx, npfx);

	s->kv.val.v = s->kvbuf+npfx;
	s->kv.val.nv = 0;
	cpkey(&s->kv.key, &s->pfx, s->kvbuf, sizeof(s->kvbuf));
}

void
btenter(Tree *t, Scan *s)
{
	Gefs *fs = t->limbo.fs;
	int i, same;
	Scanp *p;
	Msg m, c;
	Bptr bp;
	Blk *b;
	Kvp v;

	s->fs = fs;
	if(s->donescan)
		return;
	b = getroot(t, &s->ht);
	if((s->path = calloc(s->ht, sizeof(Scanp))) == nil){
		dropblk(b);
		error(Enomem);
	}
	if(waserror()){
		btexit(s);
		nexterror();
	}
	p = s->path;
	p[0].b = b;
	for(i = 0; i < s->ht; i++){
		p[i].vi = blksearch(b, &s->kv.key, &v, &same);
		if(b->type == Tpivot){
			if(p[i].vi == -1)
				getval(b, ++p[i].vi, &v);
			p[i].bi = bufsearch(b, &s->kv.key, &m, &same);
			if(p[i].bi == -1){
				p[i].bi++;
			}else if(!same || !s->first){
				/* scan past repeated messages */
				while(p[i].bi < p[i].b->nbuf){
					getmsg(p[i].b, p[i].bi, &c);
					if(keycmp(&m.kv.key, &c.kv.key) != 0)
						break;
					p[i].bi++;
				}
			}
			bp = unpackbp(v.val.v, v.val.nv);
			b = getblk(fs, bp, 0);
			p[i+1].b = b;
		}else if(p[i].vi == -1 || !same || !s->first)
			p[i].vi++;
	}
	s->first = 0;
	poperror();
}

int
btnext(Scan *s, Kvp *r)
{
	Gefs *fs = s->fs;
	int i, j, h, ok, start, bufsrc;
	Scanp *p;
	Msg m, n;
	Bptr bp;
	Kvp kv;

Again:
	p = s->path;
	h = s->ht;
	start = h;
	bufsrc = -1;
	if(p == nil || s->donescan)
		return 0;
	if(waserror()){
		btexit(s);
		nexterror();
	}
	/* load up the correct blocks for the scan */
	for(i = h-1; i >= 0; i--){
		if(p[i].b != nil
		&&(p[i].vi < p[i].b->nval || p[i].bi < p[i].b->nbuf))
			break;
		if(i == 0){
			s->donescan = 1;
			poperror();
			return 0;
		}
		if(p[i].b != nil)
			dropblk(p[i].b);
		p[i].b = nil;
		p[i].vi = 0;
		p[i].bi = 0;
		p[i-1].vi++;
		start = i;
	}

	if(p[start-1].vi < p[start-1].b->nval){
		for(i = start; i < h; i++){
			getval(p[i-1].b, p[i-1].vi, &kv);
			bp = unpackbp(kv.val.v, kv.val.nv);
			p[i].b = getblk(fs, bp, 0);
		}
		ok = 1;
		m.op = Oinsert;
		getval(p[h-1].b, p[h-1].vi, &m.kv);
	}else{
		getmsg(p[start-1].b, p[start-1].bi, &m);
		if(m.op == Oinsert)
			ok = 1;
		else if(m.op == Oclobber || m.op == Oclearb)
			ok = 0;
		else
			broke(fs, "%s: broken entry: %N\n", Efs, &m);
		bufsrc = start-1;
	}

	for(i = h-2; i >= 0; i--){
		if(p[i].b == nil || p[i].bi == p[i].b->nbuf)
			continue;
		getmsg(p[i].b, p[i].bi, &n);
		if(keycmp(&n.kv.key, &m.kv.key) < 0){
			if(n.op == Oinsert)
				ok = 1;
			else if(n.op == Oclobber || n.op == Oclearb)
				ok = 0;
			else
				broke(fs, "%s: broken entry: %N\n", Efs, &n);
			bufsrc = i;
			m = n;
		}
	}
	if(m.kv.key.nk < s->pfx.nk || memcmp(m.kv.key.k, s->pfx.k, s->pfx.nk) != 0){
		s->donescan = 1;
		poperror();
		return 0;
	}

	/* scan all messages applying to the message */
	cpkvp(r, &m.kv, s->kvbuf, sizeof(s->kvbuf));
	if(bufsrc == -1)
		p[h-1].vi++;
	else
		p[bufsrc].bi++;
	for(i = h-2; i >= 0; i--){
		for(j = p[i].bi; p[i].b != nil && j < p[i].b->nbuf; j++){
			getmsg(p[i].b, j, &m);
			if(keycmp(&r->key, &m.kv.key) != 0)
				break;
			ok = apply(fs, r, &m, s->kvbuf, sizeof(s->kvbuf));
			p[i].bi++;
		}
	}
	poperror();
	if(!ok)
		goto Again;
	return 1;
}

void
btexit(Scan *s)
{
	int i;

	for(i = 0; i < s->ht; i++)
		dropblk(s->path[i].b);
	free(s->path);
	s->path = nil;
	s->ht = 0;
}
