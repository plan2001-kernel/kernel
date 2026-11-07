#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

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
