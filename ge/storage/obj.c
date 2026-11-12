#include "gefs.h"
#include "objint.h"

/*
 * GE versioned object engine for the native O profile (docs/O-FORMAT.md).
 * A volume holds all Kobj records in the
 * O volume's snapshot tree; there is no fid, qid or pathname:
 *
 *	Kobj (header)			=> N[8] K[8] W[8] user[]
 *	Kobj oid[8] ~ver[8] idx[4]	=> chunk (idx 0: len[4] first)
 *
 * N is the published version, K the oldest one kept, W the last begun.
 * What shows is, for each oid, its newest version <= N, unless that is
 * a tombstone (len ~0).  A transaction writes version W = N+1 as the
 * records come (unseen, as > N), and its commit publishes it with the
 * header: one message, so in one change of the tree's root.  A failed
 * or abandoned transaction's keys are taken out (versions > N); after a
 * crash, W > N says some may be left, and the next begin sweeps them.
 */

enum {
	Objkeep		= 3,		/* versions kept: K..N */
	Objroom		= 16,		/* blocks a batch of values needs, at most */
};

/*
 * Native OID allocation state. It is appended to the volume header's
 * existing user part, so it advances in the same Kobj transaction as
 * the object it names; no allocator record is itself an OID, and no
 * ordinary oobjput can reach it.  The first 72 bytes are the
 * pre-existing seed header (Environment digest and its next value),
 * kept for compatibility; the allocator starts below at offset 72.
 */
#define Objallocbase	64ULL
#define Objalloclimit	0xffffffff00000000ULL
#define Objexecfirst	0x4f45584543000001ULL
#define Objexeclast	0x4f45584543000003ULL
#define Objalloclegacy	72
#define Objallocstate	88	/* 72 + magic[8] + next[8] */
#define Objownerstate	104	/* 88 + magic[8] + mode[8] */
static char objallocmagic[8] = "OALLOC01";
static char objownermagic[8] = "OOWNER01";
static char objownermode[8] = "OOWNMODE";

/*
 * Immutable per-object owner metadata.  The key is versioned like Kobj, so
 * an owner record is published only with the transaction that created the
 * object; unpublished records are ignored and swept like unpublished
 * chunks.  The value keeps the payload format opaque.
 */
enum {
	Objownerksz	= 1+8+8,	/* Kowner store oid ~ver */
	Objownerpfx	= 1,		/* Kowner store */
	Objowneroidpfx	= 1+8,	/* Kowner store oid */
	Objownervallen	= 8+8,		/* magic owner */
	Objmigbatch	= 32,		/* legacy OIDs per scan/stage batch */
};

/*
 * Plan2001 Stage 3: the engine's tree.  A native O transaction writes
 * the O volume's snapshot tree directly, with no mount or file.
 */
Tree*
txroot(Otx *tx)
{
	return tx->tree;
}

static void
txupsert(Otx *tx, Msg *m, int nm)
{
	if(agetl(&tx->fs->rdonly))
		error(Erdonly);
	btupsert(tx->tree, m, nm);
}

/* Plan2001: a volume's stores with a transaction, under its mutlk (Gefs.otxs) */

static void	packownerkey(char*, uvlong, uvlong);

static void
packkey(char *k, uvlong oid, uvlong ver, u32int idx)
{
	k[0] = Kobj;
	PACK64(k+1, oid);
	PACK64(k+9, ~ver);
	PACK32(k+17, idx);
}

int
gethdr(Tree *t, Ohdr *h)
{
	char kb[Objhdrksz], buf[Kvmax];
	Key k;
	Kvp kv;

	kb[0] = Kobj;
	k.k = kb;
	k.nk = sizeof(kb);
	memset(h, 0, sizeof(*h));
	if(!btlookup(t, &k, &kv, buf, sizeof(buf)))
		return 0;
	if(kv.val.nv < 24 || kv.val.nv > 24+Objuser)
		error(Ecorrupt);
	h->n = UNPACK64(kv.val.v);
	h->k = UNPACK64(kv.val.v+8);
	h->w = UNPACK64(kv.val.v+16);
	/* All OIDs in this volume share one published version and writer
	 * epoch. Reject a damaged header before it can publish a new root. */
	if(h->k == 0 || h->n == 0 || h->k > h->n ||
	   h->n > h->w || h->w == ~0ULL)
		error(Ecorrupt);
	h->nuser = kv.val.nv - 24;
	memmove(h->user, kv.val.v+24, h->nuser);
	return 1;
}

static void
puthdr(Otx *tx, Ohdr *h)
{
	char kb[Objhdrksz], vb[24+Objuser];
	Msg m;

	kb[0] = Kobj;
	PACK64(vb, h->n);
	PACK64(vb+8, h->k);
	PACK64(vb+16, h->w);
	memmove(vb+24, h->user, h->nuser);
	m.op = Oinsert;
	m.kv.key.k = kb;
	m.kv.key.nk = sizeof(kb);
	m.kv.val.v = vb;
	m.kv.val.nv = 24 + h->nuser;
	txupsert(tx, &m, 1);
}

/*
 * Plan2001: a long run of upserts (a store removed, an abort, the
 * history swept) lets go of mutlk and its epoch every Objyield
 * batches, as the legacy admin batches did: the blocks it freed
 * leave limbo.
 * Held through it all, the Bfree pool and the cache ran out with
 * mutlk held, and gefs stopped.  No scan may be entered here.
 */
void
objtxflush(Otx *tx)
{
	Gefs *fs = tx->fs;
	int i;

	if(tx->nm == 0)
		return;
	/*
	 * Plan2001: values stop where data does (blkalloc's Tdat
	 * limit, twice the reserve): the chunks are in the leaves, and
	 * a full disk's commit took the tree's reserve (read-only, T5.9)
	 */
	for(i = 0; i < tx->nm; i++)
		if(tx->mb[i].kv.val.nv > 0){
			if(!dataroom(fs, Objroom*Blksz))
				error(Efull);
			break;
		}
	txupsert(tx, tx->mb, tx->nm);
	tx->nm = 0;
	if(tx->id >= 0 && ++tx->nflush >= Objyield){
		tx->nflush = 0;
		epochend(fs, tx->id);
		qunlock(&fs->mutlk);
		epochclean(fs);
		qlock(&fs->mutlk);
		epochstart(fs, tx->id);
	}
}

void
objtxaddmsg(Otx *tx, int op, char *k, int nk, char *v, int nv)
{
	Msg *m;

	/*
	 * Plan2001: clobbers go in batches over a pivot's buffer room,
	 * so they reach the leaves: in a pivot's buffer they kept the
	 * removed values in the leaves (T5.4: a removed 40 MiB store
	 * kept ~20 MiB of the tree)
	 */
	if(tx->nm == Objclob || (nv > 0 && tx->nm >= Objbatch))
		objtxflush(tx);
	m = &tx->mb[tx->nm];
	memmove(tx->kb[tx->nm], k, nk);
	if(nv > 0)
		memmove(tx->vb[tx->nm], v, nv);
	m->op = op;
	m->kv.key.k = tx->kb[tx->nm];
	m->kv.key.nk = nk;
	m->kv.val.v = nv > 0 ? tx->vb[tx->nm] : nil;	/* vb: Objbatch only */
	m->kv.val.nv = nv;
	tx->nm++;
}

/*
 * The keys of store under pfx for which drop() says so, out: in
 * batches, each from a scan of its own (as readdir's: entered and left
 * again, so the tree may change between them).  Called with mutlk.
 */
void
objtxsweep(Otx *tx, char *pfx, int npfx, int (*drop)(char*, int, void*), void *a)
{
	Gefs *fs = tx->fs;
	char last[Keymax];
	int nlast, more, n;
	Scan *s;

	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	nlast = 0;
	do{
		btnewscan(fs, s, pfx, npfx);
		if(nlast > 0){
			memmove(s->kvbuf, last, nlast);
			s->kv.key.k = s->kvbuf;
			s->kv.key.nk = nlast;
			s->first = 0;
		}
		btenter(txroot(tx), s);
		more = 0;
		n = 0;
		while(btnext(s, &s->kv)){
			if(drop(s->kv.key.k, s->kv.key.nk, a)){
				objtxaddmsg(tx, Oclobber, s->kv.key.k, s->kv.key.nk, nil, 0);
				n++;
			}
			if(n == Objclob-1 || tx->nm == Objclob){
				memmove(last, s->kv.key.k, s->kv.key.nk);
				nlast = s->kv.key.nk;
				more = 1;
				break;
			}
		}
		btexit(s);
		objtxflush(tx);
	}while(more);
	poperror();
	free(s);
}

uvlong
keyver(char *k)
{
	return ~UNPACK64(k+9);
}

static int
dropunpub(char *k, int nk, void *a)
{
	return (nk == Objksz || nk == Objownerksz) &&
		keyver(k) > *(uvlong*)a;
}

typedef struct Oold Oold;
struct Oold {
	uvlong	k;
	uvlong	keep;	/* the newest version <= k, 0: not yet seen */
};

/* an oid's versions older than its newest <= K: seen by none kept */
static int
dropold(char *k, int nk, void *a)
{
	Oold *o;
	uvlong v;

	if(nk != Objksz)
		return 0;
	o = a;
	v = keyver(k);
	if(o->keep == 0){
		if(v <= o->k)
			o->keep = v;
		return 0;
	}
	return v < o->keep;
}

int	objfail;	/* the next commit's history sweep fails */
int	ownerfault;	/* test-only create/publication fault point */
int	ownercrash;	/* test-only hard-stop checkpoint (101..104) */

/*
 * A deterministic test-only checkpoint.  The marker names exactly where
 * guest execution stopped; the host kills QEMU here without letting the
 * transaction finish or unwind.  Interrupts are disabled so this CPU does
 * not run any later guest code before the host's kill.
 */
static void
ownercrashstop(char *where, uvlong oid)
{
	print("OWNERID-CRASH %s oid=%llux\n", where, oid);
	splhi();
	for(;;)
		;
}

/*
 * Plan2001 V7 (the V7 store-header format (see objpin below)): the user part's recovery pin.
 * The header's user part is the file's, and only its writer knows it:
 * env[64] next[8] (V5, V6), or env[64] next[8] fmt[1] pad[7] pin[8]
 * (V7, fmt 2).  The pin is a published version that must stay readable
 * until the next upgrade publishes another: K never passes it.
 */
enum {
	Objuserfmt	= 72,	/* the format byte's offset */
	Objuserpin	= 80,	/* the pin's */
	Objuserfmtv7	= 2,
	Objuserlenv7	= 88,
};

static uvlong
objpin(Ohdr *h)
{
	if(h->nuser < Objuserlenv7 || h->user[Objuserfmt] != Objuserfmtv7)
		return 0;
	return UNPACK64(h->user+Objuserpin);
}

void
objtxbegin(Otx *tx)
{
	Gefs *fs = tx->fs;
	Ohdr h;
	if(tx->ver != 0)
		return;
	if(fs->otxs != nil)
		error("O: volume write transaction busy");
	gethdr(txroot(tx), &h);
	if(h.w > h.n){
		/* a transaction cut short: its keys out */
		char pfx[Objownerpfx];

		pfx[0] = Kobj;
			objtxsweep(tx, pfx, sizeof(pfx), dropunpub, &h.n);
		pfx[0] = Kowner;
		objtxsweep(tx, pfx, sizeof(pfx), dropunpub, &h.n);
	}
	h.w = h.n + 1;
	puthdr(tx, &h);
	tx->ver = h.w;
	tx->nent = 0;
	tx->bytes = 0;
	tx->newuser = 0;
	tx->next = fs->otxs;
	fs->otxs = tx;
}

void
objtxend(Otx *tx)
{
	Gefs *fs = tx->fs;
	Otx **p;

	for(p = (Otx**)&fs->otxs; *p != nil; p = &(*p)->next)
		if(*p == tx){
			*p = tx->next;
			break;
		}
	tx->ver = 0;
	tx->nent = 0;
	tx->nm = 0;
	free(tx->ent);
	tx->ent = nil;
	tx->aent = 0;
}

void
objtxaddent(Otx *tx, uvlong oid, u32int nidx)
{
	Oent *e;

	if(tx->nent == tx->aent){
		tx->aent = tx->aent ? 2*tx->aent : 256;
		e = realloc(tx->ent, tx->aent*sizeof(Oent));
		if(e == nil)
			error(Enomem);
		tx->ent = e;
	}
	tx->ent[tx->nent].oid = oid;
	tx->ent[tx->nent].nidx = nidx;
	tx->nent++;
}

/* the transaction's keys out; called with mutlk */
void
objtxabort(Otx *tx)
{
	char pfx[Objownerpfx];
	Ohdr h;

	if(tx->ver == 0)
		return;
	if(waserror()){
		/* left to the next begin: W > N */
		objtxend(tx);
		nexterror();
	}
	tx->nm = 0;
	gethdr(txroot(tx), &h);
	/*
	 * Remove every unpublished Kobj/Kowner record, not only the keys
	 * this Otx remembers: a failed owner migration stages records for
	 * many legacy OIDs that never become Oent entries.  Each batch is
	 * collected by a scan that is left before the next mutation.
	 */
	pfx[0] = Kobj;
	objtxsweep(tx, pfx, sizeof pfx, dropunpub, &h.n);
	pfx[0] = Kowner;
	objtxsweep(tx, pfx, sizeof pfx, dropunpub, &h.n);
	h.w = h.n;
	puthdr(tx, &h);
	poperror();
	objtxend(tx);
}

void
objtxputchunk(Otx *tx)
{
	char k[Objksz];

	packkey(k, tx->oid, tx->ver, tx->idx);
	objtxaddmsg(tx, Oinsert, k, Objksz, tx->chunk, tx->nchunk);
	tx->idx++;
	tx->nchunk = 0;
}

void
objtxcommit(Otx *tx)
{
	char pfx[Objoidksz];
	Ohdr h;
	Oent *e;
	Oold o;

	if(tx->ver == 0)
		return;
	uvlong k0, pin;

	objtxflush(tx);
	gethdr(txroot(tx), &h);
	if(h.w != tx->ver)
		error(Ecorrupt);
	h.n = tx->ver;
	/* the prospective user part first: its pin decides K */
	k0 = h.k;
	if(tx->newuser){
		h.nuser = tx->nuser;
		memmove(h.user, tx->user, tx->nuser);
	}
	if(h.n >= Objkeep && h.k < h.n - Objkeep + 1)
		h.k = h.n - Objkeep + 1;
	pin = objpin(&h);
	if(pin != 0 && pin < h.k && pin >= k0)
		h.k = pin;	/* the recovery point stays: K <= pin <= N */
	if(h.k < k0)
		h.k = k0;	/* and nothing already pruned comes back */
	if(h.k == 0)
		h.k = 1;
	/* published: one message */
	puthdr(tx, &h);

	/*
	 * then the versions none kept sees.  Published, the
	 * transaction is no more: a failure here aborts nothing
	 * (an abort clobbered the version just published) and is no
	 * failure of the commit; what is left is swept at the
	 * object's next commit
	 */
	tx->ver = 0;
	if(waserror()){
		print("gefs: volume history not swept: %s\n", errmsg());
		tx->nm = 0;
	}else{
		if(objfail){	/* the console's objfail (T5.12) */
			objfail = 0;
			error("objfail: history sweep");
		}
		pfx[0] = Kobj;
			for(e = tx->ent; e < tx->ent+tx->nent; e++){
			PACK64(pfx+1, e->oid);
			o.k = h.k;
			o.keep = 0;
			objtxsweep(tx, pfx, sizeof(pfx), dropold, &o);
		}
		poperror();
	}
	objtxend(tx);
}

/*
 * NATIVE GE/O object API (docs/O-FORMAT.md).
 * A volume holds all Kobj records
 * in the O volume's snapshot tree.  There is no Fid, qid, mount, file or
 * path: these calls enter the engine directly.  The handle's lock is the
 * PoC's single writer.
 */

static void	objputdata(Otx*, uvlong, void*, long);
static void	objunpubclear(Otx*, uvlong, uvlong, int);
static void	ownerensure(Otx*, Ohdr*);
static void	ownerassign(Otx*, Ohdr*, uvlong, uvlong);
static void	setuserstate(Otx*, Ohdr*, uvlong, int);
static void	packownerkey(char*, uvlong, uvlong);

/* serializes a whole create (or commit) with its sync across handles */

/*
 * A candidate is never handed out if it falls in a reserved range or if
 * any published record (including a tombstone) already names it.  The
 * latter is checked in the same serialized transaction as the write, so
 * an explicitly numbered oobjput in the candidate range is skipped
 * even though it did not advance the high-water mark.
 */
static int
allocreserved(uvlong oid)
{
	if(oid < Objallocbase || oid >= Objalloclimit)
		return 1;
	return oid >= Objexecfirst && oid <= Objexeclast;
}

static int
objectused(Gefs *fs, uvlong oid, uvlong upto)
{
	char pfx[Objoidksz];
	Scan *s;
	int used;

	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	pfx[0] = Kobj;
	PACK64(pfx+1, oid);
	btnewscan(fs, s, pfx, Objoidksz);
	btenter(&fs->snap, s);
	if(waserror()){
		btexit(s);
		nexterror();
	}
	used = 0;
	while(btnext(s, &s->kv)){
		if(s->kv.key.nk != Objksz)
			continue;
		if(keyver(s->kv.key.k) > upto)
			continue;
		used = 1;
		break;
	}
	btexit(s);
	poperror();
	free(s);
	poperror();
	return used;
}

static int
txhasoid(Otx *tx, uvlong oid)
{
	Oent *e;

	for(e = tx->ent; e < tx->ent+tx->nent; e++)
		if(e->oid == oid)
			return 1;
	return 0;
}

/*
 * Read the volume allocator's published state. A header with no user part, or
 * with the pre-allocator 72-byte seed header, upgrades on first use.
 * Anything else is rejected instead of being reset to zero.
 */
static void
getalloc(Ohdr *h, uvlong *next)
{
	if(h->nuser == 0 || h->nuser == Objalloclegacy){
		*next = Objallocbase;
		return;
	}
	if((h->nuser == Objallocstate || h->nuser == Objownerstate) &&
	   memcmp(h->user+Objalloclegacy, objallocmagic,
	    sizeof objallocmagic) == 0){
		*next = UNPACK64(h->user+Objalloclegacy+sizeof objallocmagic);
		if(*next < Objallocbase || *next > Objalloclimit)
			error("O: corrupt allocator state");
		return;
	}
	error("O: unrecognized volume allocator state");
}

/*
 * Discard records an abandoned attempt left at versions newer than the
 * published N.  A crash can persist payload chunks or owner records without
 * a durable W > N header, so begin()'s cut-short sweep is not guaranteed to
 * see them.  They are never authority: clear them before an OID is reused
 * or a new version is staged.
 */
static void
objunpubclear(Otx *tx, uvlong oid, uvlong upto, int owners)
{
	char pfx[Objowneroidpfx];

	pfx[0] = Kobj;
	PACK64(pfx+1, oid);
	objtxsweep(tx, pfx, Objoidksz, dropunpub, &upto);
	if(owners){
		pfx[0] = Kowner;
		objtxsweep(tx, pfx, sizeof pfx, dropunpub, &upto);
	}
}

/*
 * Allocate one never-before-committed OID and publish its initial
 * payload (including an empty one).  The allocator high-water mark and
 * the object are written in one volume transaction and become visible
 * with one commit; the caller sees the OID only after gesync succeeds.
 */
long
oobjcreate(void *handle, void *data, long n, uvlong *oid, uvlong owner)
{
	Otx *sp = handle;
	Gefs *fs;
	Otx *tx;
	Ohdr h;
	uvlong cand, next;

	if(oid == nil)
		error("O: no OID result pointer");
	if(owner == 0)
		error(Ebadarg);
	fs = sp->fs;
	qlock(&fs->oalloclk);
	if(waserror()){
		qunlock(&fs->oalloclk);
		nexterror();
	}
	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	qlock(&fs->mutlk);
	if(waserror()){
		if(!waserror()){
			if(sp->ver != 0)
				objtxabort(sp);
			poperror();
		}
		qunlock(&fs->mutlk);
		nexterror();
	}
	tx = sp;
	objtxbegin(tx);
	if(!gethdr(&fs->snap, &h))
		error("O: no volume header");
	getalloc(&h, &next);
	for(;;){
		if(next >= Objalloclimit)
			error("O: OID range exhausted");
		if(allocreserved(next) || txhasoid(tx, next) ||
		    objectused(fs, next, h.n)){
			next++;
			continue;
		}
		break;
	}
	cand = next;
	next++;
	if(next >= Objalloclimit)
		next = Objalloclimit;	/* the next call fails, never wraps */

	/* A reused OID must not inherit chunks or owner metadata from an
	 * abandoned attempt that never published. */
	objunpubclear(tx, cand, h.n, 1);

	/* Payload and immutable owner are staged in the same version and
	 * become visible with the one header commit. */
	objputdata(tx, cand, data, n);
	if(ownerfault == 1){
		ownerfault = 0;
		error("ownerfault: before owner staging");
	}
	if(ownercrash == 101)
		ownercrashstop("before-owner-staging", cand);
	ownerensure(tx, &h);
	objtxflush(tx);
	setuserstate(tx, &h, next, 1);
	ownerassign(tx, &h, cand, owner);
	if(ownerfault == 2){
		ownerfault = 0;
		error("ownerfault: after owner staging");
	}
	if(ownercrash == 102)
		ownercrashstop("after-owner-staging", cand);
	objtxflush(tx);
	objtxcommit(tx);
	if(ownerfault == 3){
		ownerfault = 0;
		error("ownerfault: after publication");
	}
	if(ownercrash == 103)
		ownercrashstop("after-publication", cand);
	qunlock(&fs->mutlk);
	poperror();
	gesync(fs, 0);
	qunlock(&sp->lk);
	poperror();
	*oid = cand;
	qunlock(&fs->oalloclk);
	poperror();
	return 0;
}

/* One opaque transaction per capability, no named storage subspaces. */
void*
oobjopen(void *volume)
{
	Gefs *fs = volume;
	Otx *tx;

	if(fs == nil || !(fs->flag & Ovolflag))
		error("O: not an O volume");
	tx = emalloc(sizeof(Otx), 1);
	tx->fs = fs;
	tx->tree = &fs->snap;
	tx->id = -1;
	return tx;
}

static uvlong objlegacyowner;

void
oobjownersetlegacy(uvlong owner)
{
	objlegacyowner = owner;
}

static void
packownerkey(char *k, uvlong oid, uvlong ver)
{
	k[0] = Kowner;
	PACK64(k+1, oid);
	PACK64(k+9, ~ver);
}

static int
headerownermode(Ohdr *h)
{
	if(h->nuser < Objownerstate)
		return 0;
	if(memcmp(h->user+Objallocstate, objownermode,
	    sizeof objownermode) != 0)
		error("O: corrupt owner-mode state");
	return 1;
}

static int
txownermode(Otx *tx)
{
	return tx->newuser && tx->nuser >= Objownerstate &&
		memcmp(tx->user+Objallocstate, objownermode,
		    sizeof objownermode) == 0;
}

/*
 * The newest owner record at or below the published version, or none.
 * Unpublished records are not authority: they appear only after a crashed
 * or aborted creation and are ignored until the next transaction sweeps
 * them.
 */
static int
ownerlookup(Otx *tx, uvlong oid, uvlong upto, uvlong *owner, uvlong *ownerver)
{
	char pfx[Objowneroidpfx];
	Scan *s;
	uvlong v, best;
	int found;

	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	pfx[0] = Kowner;
	PACK64(pfx+1, oid);
	btnewscan(tx->fs, s, pfx, sizeof pfx);
	btenter(tx->tree, s);
	if(waserror()){
		btexit(s);
		nexterror();
	}
	found = 0;
	best = 0;
	while(btnext(s, &s->kv)){
		if(s->kv.key.nk != Objownerksz)
			continue;
		v = keyver(s->kv.key.k);
		if(v > upto || (found && v <= best))
			continue;
		if(s->kv.val.nv != Objownervallen ||
		   memcmp(s->kv.val.v, objownermagic,
		    sizeof objownermagic) != 0 ||
		   UNPACK64(s->kv.val.v+8) == 0)
			error("O: corrupt object owner metadata: oid %llux", oid);
		best = v;
		*owner = UNPACK64(s->kv.val.v+8);
		found = 1;
	}
	btexit(s);
	poperror();
	free(s);
	poperror();
	if(found && ownerver != nil)
		*ownerver = best;
	return found;
}

static int
objectlive(Otx *tx, uvlong oid, uvlong upto)
{
	char pfx[Objoidksz];
	Scan *s;
	uvlong v;
	u32int idx, len;
	int live;

	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	pfx[0] = Kobj;
	PACK64(pfx+1, oid);
	btnewscan(tx->fs, s, pfx, sizeof pfx);
	btenter(tx->tree, s);
	if(waserror()){
		btexit(s);
		nexterror();
	}
	live = 0;
	while(btnext(s, &s->kv)){
		if(s->kv.key.nk != Objksz)
			continue;
		v = keyver(s->kv.key.k);
		if(v > upto)
			continue;
		idx = UNPACK32(s->kv.key.k+17);
		if(idx == 0 && s->kv.val.nv >= 4){
			len = UNPACK32(s->kv.val.v);
			live = len != Objtomb;
		}
		break;
	}
	btexit(s);
	poperror();
	free(s);
	poperror();
	return live;
}

static void
owneradd(Otx *tx, uvlong oid, uvlong ver, uvlong owner)
{
	char k[Objownerksz], v[Objownervallen];

	packownerkey(k, oid, ver);
	memmove(v, objownermagic, sizeof objownermagic);
	PACK64(v+8, owner);
	objtxaddmsg(tx, Oinsert, k, sizeof k, v, sizeof v);
}

/*
 * One-time upgrade of a Space that predates owner metadata.  Every object
 * published at the current version belonged to the seeded volume owner;
 * recording that explicitly means a later missing record is corruption,
 * never an implicit root grant.  New objects are assigned at creation.
 *
 * A scan is never mutated: collect a bounded batch of completed, distinct,
 * published, live OIDs, leave the scan, stage their Kowner records, then
 * resume after the last scanned key.  Batches are staged at tx->ver, an
 * unpublished version, so the migration becomes visible only with the
 * header commit that also records owner mode.
 */
static void
ownermigrate(Otx *tx, uvlong upto, uvlong owner)
{
	char pfx[Objhdrksz];
	char resume[Keymax], last[Keymax];
	uvlong batch[Objmigbatch];
	uvlong oid, prev, v;
	int nresume, nlast, nb, i, more, have, prevlive, seen;
	Scan *s;

	if(owner == 0)
		error("O: no legacy owner registered");
	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	pfx[0] = Kobj;
	nresume = 0;
	do{
		btnewscan(tx->fs, s, pfx, sizeof pfx);
		if(nresume > 0){
			memmove(s->kvbuf, resume, nresume);
			s->kv.key.k = s->kvbuf;
			s->kv.key.nk = nresume;
			s->first = 0;
		}
		btenter(tx->tree, s);
		if(waserror()){
			btexit(s);
			nexterror();
		}
		nb = 0;
		more = 0;
		nlast = 0;
		have = 0;
		prev = 0;
		prevlive = 0;
		seen = 0;
		while(btnext(s, &s->kv)){
			if(s->kv.key.nk != Objksz)
				continue;
			oid = UNPACK64(s->kv.key.k+1);
			if(have && oid != prev){
				if(prevlive){
					batch[nb++] = prev;
					if(nb == Objmigbatch){
						/* last is the previous OID's final
						 * record; resume after it. */
						more = 1;
						break;
					}
				}
				prev = oid;
				prevlive = 0;
				seen = 0;
			}else if(!have){
				prev = oid;
				have = 1;
			}
			v = keyver(s->kv.key.k);
			if(!seen && v <= upto){
				seen = 1;
				if(UNPACK32(s->kv.key.k+17) == 0 &&
				   s->kv.val.nv >= 4)
					prevlive =
					    UNPACK32(s->kv.val.v) != Objtomb;
			}
			memmove(last, s->kv.key.k, s->kv.key.nk);
			nlast = s->kv.key.nk;
		}
		if(!more && have && prevlive)
			batch[nb++] = prev;
		btexit(s);
		poperror();
		for(i = 0; i < nb; i++)
			owneradd(tx, batch[i], tx->ver, owner);
		if(ownerfault == 4 && nb > 0){
			ownerfault = 0;
			error("ownerfault: migration after staging");
		}
		if(ownercrash == 104 && nb > 0)
			ownercrashstop("migration-after-staging", nb);
		if(more){
			memmove(resume, last, nlast);
			nresume = nlast;
		}
	}while(more);
	poperror();
	free(s);
}

static void
setuserstate(Otx *tx, Ohdr *h, uvlong next, int ownermode)
{
	memset(tx->user, 0, sizeof tx->user);
	if(h->nuser >= Objalloclegacy)
		memmove(tx->user, h->user, Objalloclegacy);
	memmove(tx->user+Objalloclegacy, objallocmagic,
		sizeof objallocmagic);
	PACK64(tx->user+Objalloclegacy+sizeof objallocmagic, next);
	tx->nuser = Objallocstate;
	if(ownermode){
		memmove(tx->user+Objallocstate, objownermode,
			sizeof objownermode);
		PACK64(tx->user+Objallocstate+sizeof objownermode, tx->ver);
		tx->nuser = Objownerstate;
	}
	tx->newuser = 1;
}

static void
ownerensure(Otx *tx, Ohdr *h)
{
	uvlong next;

	if(headerownermode(h) || txownermode(tx))
		return;
	ownermigrate(tx, h->n, objlegacyowner);
	getalloc(h, &next);
	setuserstate(tx, h, next, 1);
}

static void
ownerassign(Otx *tx, Ohdr *h, uvlong oid, uvlong owner)
{
	uvlong stored, over;

	if(owner == 0 || oid == 0)
		error(Ebadarg);
	/*
	 * A published owner record is immutable: a second assignment, same
	 * or different OwnerID, never creates or overwrites it.  A live
	 * object without one is corruption, except for the migration record
	 * this transaction is about to publish.
	 */
	if(ownerlookup(tx, oid, h->n, &stored, &over)){
		/* Existing object's provenance is immutable. The caller's
		 * OwnerID describes the writer, not an authority predicate.
		 * oswritehand already checked its explicit Ohand grant. */
		return;
	}
	if(objectlive(tx, oid, h->n)){
		if(ownerlookup(tx, oid, tx->ver, &stored, &over) &&
		   over == tx->ver)
			return; /* retain newly staged immutable provenance */
		error("O: corrupt object owner metadata: live oid %llux", oid);
	}
	/* A reused OID with no published owner is a new publication.  Any
	 * older unpublished record was cleared before staging this version. */
	if(ownerlookup(tx, oid, tx->ver, &stored, &over) &&
	   over == tx->ver)
		return; /* previous writer in same transaction fixed provenance */
	owneradd(tx, oid, tx->ver, owner);
}

/*
 * Report the immutable owner record.  Returns 1 and the owner when one is
 * published, 0 when none is.  *required says this Space has owner metadata,
 * so a missing record is corruption, not a legacy root object.
 */
int
oobjownerget(void *handle, uvlong oid, uvlong *owner, int *required)
{
	Otx *sp = handle;
	Otx *tx;
	Ohdr h;

	if(sp == nil || owner == nil)
		error(Ebadarg);
	if(required != nil)
		*required = 0;
	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	tx = sp;
	if(!gethdr(tx->tree, &h)){
		poperror();
		qunlock(&sp->lk);
		return 0;
	}
	if(required != nil)
		*required = headerownermode(&h);
	if(ownerlookup(tx, oid, h.n, owner, nil)){
		poperror();
		qunlock(&sp->lk);
		return 1;
	}
	poperror();
	qunlock(&sp->lk);
	return 0;
}

static void
objputdata(Otx *tx, uvlong oid, void *data, long n)
{
	char *p;
	long k;

	objtxbegin(tx);
	if(n < 0 || n > Objtxmax || tx->bytes + n > Objtxmax)
		error("object transaction too big");
	if(!dataroom(tx->fs, 2*n + Blksz))
		error(Efull);
	tx->oid = oid;
	tx->bytes += n;
	tx->idx = 0;
	PACK32(tx->chunk, (u32int)n);
	tx->nchunk = 4;
	p = data;
	do{
		k = Objchunk - tx->nchunk;
		if(k > n)
			k = n;
		if(k > 0){
			memmove(tx->chunk+tx->nchunk, p, k);
			p += k;
			n -= k;
			tx->nchunk += k;
		}
		if(tx->nchunk == Objchunk || n == 0)
			objtxputchunk(tx);
	}while(n > 0);
	objtxaddent(tx, oid, tx->idx);
}

void
oobjput(void *handle, uvlong oid, void *data, long n, uvlong owner)
{
	Otx *sp = handle;
	Otx *tx;
	Ohdr h;

	if(oid == 0)
		error(Ebadarg);
	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	tx = sp;
	qlock(&sp->fs->mutlk);
	if(waserror()){
		if(!waserror()){
			objtxabort(tx);
			poperror();
		}
		qunlock(&sp->fs->mutlk);
		nexterror();
	}
	objtxbegin(tx);
	if(!gethdr(tx->tree, &h))
		error("O: no volume header");
	/* A write may be the first publication of an OID after a crash.
	 * Clear abandoned payload chunks; owner records of a live object are
	 * rewritten by the migration below, so do not remove them here. */
	objunpubclear(tx, oid, h.n, !objectlive(tx, oid, h.n));
	ownerensure(tx, &h);
	objtxflush(tx);
	ownerassign(tx, &h, oid, owner);
	objputdata(tx, oid, data, n);
	objtxflush(tx);
	qunlock(&sp->fs->mutlk);
	poperror();
	qunlock(&sp->lk);
	poperror();
}

uvlong
oobjcommit(void *handle)
{
	Otx *sp = handle;
	Gefs *fs = sp->fs;
	Otx *tx;
	Ohdr h;
	uvlong v;

	qlock(&fs->oalloclk);
	if(waserror()){
		qunlock(&fs->oalloclk);
		nexterror();
	}
	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	tx = sp;
	if(tx->ver == 0){
		gethdr(&fs->snap, &h);
		v = h.n;
	}else{
		qlock(&fs->mutlk);
		if(waserror()){
			if(!waserror()){
				objtxabort(tx);
				poperror();
			}
			qunlock(&fs->mutlk);
			nexterror();
		}
		v = tx->ver;
		objtxcommit(tx);
		qunlock(&fs->mutlk);
		poperror();
		gesync(fs, 0);
	}
	qunlock(&sp->lk);
	poperror();
	qunlock(&fs->oalloclk);
	poperror();
	return v;
}

/*
 * The newest published version of oid, or -1 if there is none.  A
 * tombstone is not an object.  The whole value must fit in buf: the PoC
 * has no partial-read object handle.
 */
long
oobjget(void *handle, uvlong oid, void *buf, long nbuf, uvlong *ver)
{
	Otx *sp = handle;
	Gefs *fs;
	Tree *t;
	Scan *s;
	Ohdr h;
	char pfx[Objoidksz];
	uvlong v, sel;
	u32int idx, len;
	long n, total;
	int found, tomb, want;

	if(ver != nil)
		*ver = 0;
	fs = sp->fs;
	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	t = &fs->snap;
	if(!gethdr(t, &h) || h.n == 0){
		qunlock(&sp->lk);
		poperror();
		return -1;
	}
	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	pfx[0] = Kobj;
	PACK64(pfx+1, oid);
	btnewscan(fs, s, pfx, Objoidksz);
	btenter(t, s);
	if(waserror()){
		btexit(s);
		nexterror();
	}
	found = tomb = 0;
	total = 0;
	sel = 0;
	len = 0;
	want = 0;
	while(btnext(s, &s->kv)){
		if(s->kv.key.nk != Objksz)
			continue;
		v = keyver(s->kv.key.k);
		if(v > h.n)
			continue;
		idx = UNPACK32(s->kv.key.k+17);
		if(!found){
			if(idx != 0 || s->kv.val.nv < 4)
				continue;
			len = UNPACK32(s->kv.val.v);
			if(len == Objtomb){
				tomb = 1;
				break;
			}
			/* Plan2001 Stage 4: a size query (buf nil, nbuf 0) */
			if(buf == nil && nbuf == 0){
				sel = v;
				found = 1;
				total = len;
				break;
			}
			if((long)len > nbuf)
				error("object larger than buffer");
			sel = v;
			found = 1;
			want = 1;
			n = s->kv.val.nv - 4;
			if(n > len)
				n = len;
			if(n > 0)
				memmove(buf, s->kv.val.v+4, n);
			total = n;
			if(total == len)
				break;
			continue;
		}
		if(v != sel)
			break;
		if(idx != want)
			error(Ecorrupt);
		n = s->kv.val.nv;
		if(n > len - total)
			n = len - total;
		if(n > 0)
			memmove((char*)buf+total, s->kv.val.v, n);
		total += n;
		want++;
		if(total == len)
			break;
	}
	btexit(s);
	poperror();
	free(s);
	poperror();
	qunlock(&sp->lk);
	poperror();
	if(tomb)
		return -1;
	if(!found)
		return -1;
	if(total != len)
		error(Ecorrupt);
	if(ver != nil)
		*ver = sel;
	return total;
}

void
oobjclose(void *handle)
{
	Otx *sp = handle;
	Gefs *fs;
	Otx *tx;

	if(sp == nil)
		return;
	fs = sp->fs;
	qlock(&sp->lk);
	tx = sp;
	if(tx != nil && tx->ver != 0){
		qlock(&fs->mutlk);
		if(waserror()){
			objtxend(tx);
			qunlock(&fs->mutlk);
			print("O: volume transaction aborted late\n");
		}else{
			objtxabort(tx);
			qunlock(&fs->mutlk);
			poperror();
		}
	}
	qunlock(&sp->lk);
	free(sp);
}
