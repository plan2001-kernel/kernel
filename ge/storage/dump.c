#include "gefs.h"

char	spc[128];

static int
showkey(Fmt *fmt, Key *k)
{
	int n;

	/*
	 * dent: pqid[8] qid[8] -- a directory entry key.
	 * ptr:  off[8] hash[8] -- a key for an Dir block.
	 * dir:  fixed statbuf header, user ids
	 */
	if(k->nk == 0)
		return fmtprint(fmt, "\"\"");
	switch(k->k[0]){
	case Kdat:	/* qid[8] off[8] => ptr[16]:	pointer to data page */
		n = fmtprint(fmt, "dat qid:%llx off:%llx",
			UNPACK64(k->k+1), UNPACK64(k->k+9));
		break;
	case Kent:	/* pqid[8] name[n] => dir[n]:	serialized Dir */
		n = fmtprint(fmt, "ent dir:%llx, name:\"%.*s\"",
			UNPACK64(k->k+1), k->nk-11, k->k+11);
		break;
	case Klabel:	/* name[n] => tree[24]:	snapshot ref */
		n = fmtprint(fmt, "label name:\"%.*s\"", k->nk-1, k->k+1);
		break;
	case Ksnap:	/* name[n] => tree[24]:	snapshot root */
		n = fmtprint(fmt, "snap id:%lld", UNPACK64(k->k+1));
		break;
	case Kup:	/* qid[8] => pqid[8]:		parent dir */
		n = fmtprint(fmt, "up dir:%llx", UNPACK64(k->k+1));
		break;
	case Kdlist:
		n = fmtprint(fmt, "dlist gen:%lld, bgen:%lld",
			UNPACK64(k->k+1), UNPACK64(k->k+9));
		break;
	case Kconf:
		n = fmtprint(fmt, "Conf %.*s", k->nk-1, k->k+1);
		break;
	case Kobj:	/* v2: header or oid[8] ~ver[8] idx[4] */
		if(k->nk == 1)
			n = fmtprint(fmt, "obj volume header");
		else if(k->nk == 1+8+8+4)
			n = fmtprint(fmt, "obj oid:%llx ver:%llud idx:%ud",
				UNPACK64(k->k+1), ~UNPACK64(k->k+9),
				UNPACK32(k->k+17));
		else
			n = fmtprint(fmt, "obj invalid key %.*H", k->nk, k->k);
		break;
	case Kowner:	/* v2: oid[8] ~ver[8] */
		if(k->nk == 1+8+8)
			n = fmtprint(fmt, "owner oid:%llx ver:%llud",
				UNPACK64(k->k+1), ~UNPACK64(k->k+9));
		else
			n = fmtprint(fmt, "owner invalid key %.*H", k->nk, k->k);
		break;
	default:
		n = fmtprint(fmt, "??? %.*H", k->nk, k->k);
		break;
	}
	return n;
}

static int
showval(Fmt *fmt, Kvp *v, int op, int flg)
{
	int n;
	Tree t;

	n = 0;
	if(flg){
		assert(	v->val.nv == Ptrsz+2);
		n = fmtprint(fmt, "(%B,%d)", unpackbp(v->val.v, 	v->val.nv), UNPACK16(v->val.v+Ptrsz));
		return n;
	}
	if(op == Odelete || op == Oclearb){
		n = fmtprint(fmt, "delete");
		return n;
	}
	switch(v->key.k[0]){
	case Kdat:	/* qid[8] off[8] => ptr[16]:	pointer to data page */
		switch(op){
		case Odelete:
		case Oclearb:
			n = 0;
			break;
		case Onop:
		case Oinsert:
			if(	v->val.nv == Ptrsz)
				n = fmtprint(fmt, "ptr:%B", unpackbp(v->val.v, 	v->val.nv));
			else
				n = fmtprint(fmt, "BROKEN ptr %.*H", v->key.nk, v->key.k);
			break;
		}
		break;
	case Kent:	/* pqid[8] name[n] => dir[n]: legacy serialized dirent */
		n = fmtprint(fmt, "legacy dir key=%.*H val=%.*H",
			v->key.nk, v->key.k, 	v->val.nv, v->val.v);
		break;
	case Ksnap:	/* name[n] => dent[16] ptr[16]:	snapshot root */
		switch(op){
		case Orelink:
		case Oreprev:
		case Oincref:
			n = fmtprint(fmt, "gen: %lld, dlbl: %d, dref: %d",
				UNPACK64(v->val.v), v->val.v[8], v->val.v[9]);
			break;
		case Onop:
		case Oinsert:
			if(unpacktree(nil, &t, v->val.v, 	v->val.nv) == nil)
				n = fmtprint(fmt, "corrupt tree");
			else
				n = fmtprint(fmt, "<tree %B [pred=%lld, succ=%lld, base=%lld, nref=%d, nlbl=%d]>",
					t.bp, t.pred, t.succ, t.base, t.nref, t.nlbl);
			break;
		default:
			n = fmtprint(fmt, "?? unknown op %d", op);
		}
		break;
	case Klabel:
		n = fmtprint(fmt, "snap id:%lld", UNPACK64(v->val.v+1));
		break;
	case Kup:	/* qid[8] => pqid[8]:		parent dir */
		n = fmtprint(fmt, "super dir:%llx, name:\"%.*s\")",
			UNPACK64(v->val.v+1), 	v->val.nv-11, v->val.v+11);
		break;
	case Kdlist:
		n = fmtprint(fmt, "hd:%B, tl:%B",
			unpackbp(v->val.v, 	v->val.nv),
			unpackbp(v->val.v+Ptrsz, 	v->val.nv-Ptrsz));
		break;
	case Kconf:
		n = fmtprint(fmt, "%.*s", 	v->val.nv, v->val.v);
		break;
	case Kobj:
		n = fmtprint(fmt, "%d bytes", 	v->val.nv);
		break;
	case Kowner:
		if(v->val.nv == 16 &&
		   memcmp(v->val.v, "OOWNER01", 8) == 0)
			n = fmtprint(fmt, "owner:%llx", UNPACK64(v->val.v+8));
		else
			n = fmtprint(fmt, "invalid owner value %.*H",
				v->val.nv, v->val.v);
		break;
	default:
		n = fmtprint(fmt, "??? %.*H", 	v->val.nv, v->val.v);
		break;
	}
	return n;

}

int
Bconv(Fmt *fmt)
{
	Bptr bp;

	bp = va_arg(fmt->args, Bptr);
	return fmtprint(fmt, "(%llx,%.16llux,%llx)", bp.addr, bp.hash, bp.gen);
}

int
Mconv(Fmt *fmt)
{
	char *opname[Nmsgtype] = {
	[Oinsert]	"Oinsert",
	[Odelete]	"Odelete",
	[Oclearb]	"Oclearb",
	[Oclobber]	"Oclobber",
	[Owstat]	"Owstat",
	[Orelink]	"Orelink",
	[Oreprev]	"Oreprev",
	[Oincref]	"Oincref",
	};
	Msg *m;
	int f, n;

	f = (fmt->flags & FmtSharp) != 0;
	m = va_arg(fmt->args, Msg*);
	if(m == nil)
		return fmtprint(fmt, "Msg{nil}");
	n = fmtprint(fmt, "Msg(%s, ", opname[m->op]);
	n += showkey(fmt, &m->kv.key);
	n += fmtprint(fmt, ") => (");
	n += showval(fmt, &m->kv, m->op, f);
	n += fmtprint(fmt, ")");
	return n;
}

int
Pconv(Fmt *fmt)
{
	Kvp *kv;
	int f, n;

	f = (fmt->flags & FmtSharp) != 0;
	kv = va_arg(fmt->args, Kvp*);
	if(kv == nil)
		return fmtprint(fmt, "Kvp{nil}");
	n = fmtprint(fmt, "Kvp(");
	n += showkey(fmt, &kv->key);
	n += fmtprint(fmt, ") => (");
	n += showval(fmt, kv, Onop, f);
	n += fmtprint(fmt, ")");
	return n;
}

int
Kconv(Fmt *fmt)
{
	Key *k;
	int n;

	k = va_arg(fmt->args, Key*);
	if(k == nil)
		return fmtprint(fmt, "Key{nil}");
	n = fmtprint(fmt, "Key(");
	n += showkey(fmt, k);
	n += fmtprint(fmt, ")");
	return n;
}

int
Rconv(Fmt *fmt)
{
	Arange *r;

	r = va_arg(fmt->args, Arange*);
	if(r == nil)
		return fmtprint(fmt, "<Arange:nil>");
	else
		return fmtprint(fmt, "Arange(%lld+%lld)", r->off, r->len);
}

int
Qconv(Fmt *fmt)
{
	Qid q;

	q = va_arg(fmt->args, Qid);
	return fmtprint(fmt, "(%llx %ld %d)", q.path, q.vers, q.type);
}

static void
rshowblk(Gefs *fs, int fd, Blk *b, int indent, int recurse)
{
	Blk *c;
	int i;
	Bptr bp;
	Kvp kv;
	Msg m;

	if(indent > sizeof(spc)/4)
		indent = sizeof(spc)/4;
	if(b == nil){
		fprint(fd, "NIL\n");
		return;
	}
	fprint(fd, "%.*s[BLK]|{%B}\n", 4*indent, spc, b->bp);
	switch(b->type){
	case Tpivot:
		for(i = 0; i < b->nbuf; i++){
			getmsg(b, i, &m);
			fprint(fd, "%.*s[%03d]|%N\n", 4*indent, spc, i, &m);
		}
		/* wet floor */
	case Tleaf:
		for(i = 0; i < b->nval; i++){
			getval(b, i, &kv);
			if(b->type == Tpivot){
				fprint(fd, "%.*s[%03d]|%#P\n", 4*indent, spc, i, &kv);
				bp = unpackbp(kv.val.v, kv.val.nv);
				c = getblk(fs, bp, 0);
				if(recurse)
					rshowblk(fs, fd, c, indent + 1, 1);
				dropblk(c);
			}else{
				fprint(fd, "%.*s[%03d]|%P\n", 4*indent, spc, i, &kv);
			}
		}
		break;
	case Tarena:
		fprint(fd, "arena -- ");
		goto Show;
	case Tlog:
		fprint(fd, "log -- ");
		goto Show;
	case Tdlist:
		fprint(fd, "dlist -- ");
		goto Show;
	case Tdat:
		fprint(fd, "dat -- ");
	Show:
		for(i = 0; i < 32; i++){
			fprint(fd, "%x", b->buf[i] & 0xff);
			if(i % 4 == 3)
				fprint(fd, " ");
		}
		fprint(fd, "\n");
		break;
	}
}

void
showblk(Gefs *fs, int fd, Blk *b, char *m, int recurse)
{
	fprint(fd, "=== %s\n", m);
	rshowblk(fs, fd, b, 0, recurse);
}

void
initshow(void)
{
	int i;

	memset(spc, ' ', sizeof(spc));
	for(i = 0; i < sizeof(spc); i += 4)
		spc[i] = '|';
}
