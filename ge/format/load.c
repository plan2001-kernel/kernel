#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static int
rangecmp(Avl *a, Avl *b)
{
	if(((Arange*)a)->off < ((Arange*)b)->off)
		return -1;
	if(((Arange*)a)->off > ((Arange*)b)->off)
		return 1;
	return 0;
}

void
loadarena(Arena *a, Bptr hd)
{
	Blk *h0, *h1, *b;
	Bptr bp;

	/* try to load block pointers, we only need one to succeed */
	bp = hd;
	h0 = getblk(bp, GBtry);
	bp.addr += Blksz;
	h1 = getblk(bp, GBtry);
	/* if neither head nor tail is consistent, we're hosed */
	b = (h0 != nil) ? h0 : h1;
	if(b == nil)
		error(Efs);
	if(h0 == nil || h1 == nil)
		fprint(2, "using fallback arena header %B\n", b->bp);


	/* otherwise, we could have crashed mid-pass, just load the blocks */
	bp = hd;
	if(h0 == nil)
		h0 = getblk(bp, GBnochk);
	bp.addr += Blksz;
	if(h1 == nil)
		h1 = getblk(bp, GBnochk);

	unpackarena(a, b->data, Arenasz);
	if((a->free = avlcreate(rangecmp)) == nil)
		error(Enomem);
	a->logbuf[0] = cachepluck();
	a->logbuf[1] = cachepluck();
	a->logbuf[0]->bp = (Bptr){-1, -1, -1};
	a->logbuf[1]->bp = (Bptr){-1, -1, -1};
	setflag(a->logbuf[0], Bstatic, 0);
	setflag(a->logbuf[1], Bstatic, 0);
	a->h0 = h0;
	a->h1 = h1;
	a->used = a->size;
}
