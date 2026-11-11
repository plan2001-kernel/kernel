#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

/*
 * Attachable segment types
 */
static Physseg physseg[10] = {
	{ SG_BSS,	"memory",	0,	SEGMAXSIZE	},
	{ 0,		0,		0,	0		},
};

static Lock physseglock;

void
initseg(void)
{
}

Segment *
newseg(int type, uintptr base, ulong size)
{
	Segment *s;
	int mapsize;

	if(size > (SEGMAPSIZE*PTEPERTAB))
		error(Enovmem);

	s = malloc(sizeof(Segment));
	if(s == nil)
		error(Enomem);
	s->ref.ref = 1;
	s->type = type;
	s->size = size;
	s->base = base;
	s->top = base+(size*BY2PG);
	s->used = 0;

	if((type & SG_TYPE) == SG_PHYSICAL){
		s->map = nil;
		s->mapsize = 0;
		return s;
	}

	mapsize = ROUND(size, PTEPERTAB)/PTEPERTAB;
	if(mapsize > nelem(s->ssegmap)){
		s->map = malloc(mapsize*sizeof(Pte*));
		if(s->map == nil){
			free(s);
			error(Enomem);
		}
		s->mapsize = mapsize;
	}
	else{
		s->map = s->ssegmap;
		s->mapsize = nelem(s->ssegmap);
	}

	return s;
}

void
putseg(Segment *s)
{
	if(s == nil)
		return;
	if(decref(&s->ref) != 0)
		return;

	if(s->mapsize > 0){
		Pte **pte, **emap;
		Page *fh, *ft;
		ulong np;

		np = 0;
		fh = ft = nil;

		emap = &s->map[s->mapsize];
		for(pte = s->map; pte < emap; pte++){
			Page **pg, **pe, *entry;

			if(*pte == nil)
				continue;
			pg = (*pte)->first;
			pe = (*pte)->last;
			while(pg <= pe){
				entry = *pg++;
				if(entry == nil)
					continue;
				entry = deadpage(entry);
				if(entry == nil)
					continue;
				if(fh != nil)
					ft->next = entry;
				else
					fh = entry;
				ft = entry;
				np++;
			}
			free(*pte);
		}

		freepages(fh, ft, np);

		if(s->map != s->ssegmap)
			free(s->map);
	}

	if(s->region != nil)
		regionsegput(s);

	if(s->profile != nil)
		free(s->profile);

	free(s);
}

Pte*
ptealloc(void)
{
	Pte *new;

	new = malloc(sizeof(Pte));
	if(new != nil){
		new->first = &new->pages[PTEPERTAB];
		new->last = new->pages;
	}
	return new;
}

static Pte*
ptecpy(Pte *new, Pte *old)
{
	Page **src, **dst, *entry;

	dst = &new->pages[old->first-old->pages];
	new->first = dst;
	for(src = old->first; src <= old->last; src++, dst++){
		if((entry = *src) == nil)
			continue;
		incref(&entry->ref);
		new->last = dst;
		*dst = entry;
	}
	return new;
}

Segment*
dupseg(Segment **seg, int segno, int share)
{
	int i;
	Pte *pte;
	Segment *n, *s;

	s = seg[segno];
	qlock(&s->qlk);
	if(waserror()){
		qunlock(&s->qlk);
		nexterror();
	}
	switch(s->type&SG_TYPE) {
	case SG_TEXT:		/* New segment shares pte set */
	case SG_PHYSICAL:
	case SG_FIXED:
	case SG_STICKY:
	default:
	sameseg:
		incref(&s->ref);
		qunlock(&s->qlk);
		poperror();
		return s;

	case SG_STACK:
		n = newseg(s->type, s->base, s->size);
		break;

	case SG_BSS:		/* Just copy on write */
		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->size);
		break;

	case SG_DATA:		/* Copy on write */
		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->size);
		break;

	case SG_REGION:
		/*
		 * A region mapping is process-local authority, never an
		 * ordinary inherited address-space range.  An RFMEM child shares
		 * the parent's address space, so it shares the exact Segment and
		 * its mapping reference; an ordinary fork skips region segments
		 * and must not reach here.
		 */
		if(share)
			goto sameseg;
		qunlock(&s->qlk);
		poperror();
		error(Ebadarg);
	}
	for(i = 0; i < s->mapsize; i++){
		if(s->map[i] != nil){
			pte = ptealloc();
			if(pte == nil){
				qunlock(&s->qlk);
				poperror();
				putseg(n);
				error(Enomem);
			}
			n->map[i] = ptecpy(pte, s->map[i]);
		}
	}
	n->used = s->used;
	n->flushme = s->flushme;
	if(s->ref.ref > 1)
		procflushseg(s);
	qunlock(&s->qlk);
	poperror();
	return n;
}

/*
 *  segpage inserts Page p into Segmnet s.
 *  on error, calls putpage() on p.
 */
void
segpage(Segment *s, Page *p)
{
	Pte **pte, *etp;
	uintptr soff;
	Page **pg;

	qlock(&s->qlk);
	if(p->va < s->base || p->va >= s->top || s->mapsize == 0)
		panic("segpage");
	soff = p->va - s->base;
	pte = &s->map[soff/PTEMAPMEM];
	if((etp = *pte) == nil){
		etp = ptealloc();
		if(etp == nil){
			qunlock(&s->qlk);
			putpage(p);
			error(Enomem);
		}
		*pte = etp;
	}
	pg = &etp->pages[(soff&(PTEMAPMEM-1))/BY2PG];
	assert(*pg == nil);
	settxtflush(p, s->flushme);
	*pg = p;
	s->used++;
	if(pg < etp->first)
		etp->first = pg;
	if(pg > etp->last)
		etp->last = pg;
	qunlock(&s->qlk);
}

void
relocateseg(Segment *s, uintptr offset)
{
	Pte **pte, **emap;
	Page **pg, **pe;

	emap = &s->map[s->mapsize];
	for(pte = s->map; pte < emap; pte++) {
		if(*pte == nil)
			continue;
		pe = (*pte)->last;
		for(pg = (*pte)->first; pg <= pe; pg++) {
			if(!pagedout(*pg))
				(*pg)->va += offset;
		}
	}
}

uintptr
ibrk(uintptr addr, int seg)
{
	Segment *s, *ns;
	uintptr newtop;
	ulong newsize, oldpages;
	int i, mapsize;
	Pte **map;

	s = up->seg[seg];
	if(s == nil)
		error(Ebadarg);

	if(addr == 0)
		return s->base;
	if(addr > USTKTOP - (BY2PG-1))
		error(Enovmem);

	qlock(&s->qlk);

	/* We may start with the bss overlapping the data */
	if(addr < s->base) {
		if(seg != BSEG || up->seg[DSEG] == nil || addr < up->seg[DSEG]->base) {
			qunlock(&s->qlk);
			error(Enovmem);
		}
		addr = s->base;
	}

	newtop = PGROUND(addr);
	newsize = (newtop-s->base)/BY2PG;
	if(newtop != s->top && s->ref.ref > 1){
		qunlock(&s->qlk);
		error(Einuse);
	}
	if(newtop < s->top) {
		/*
		 * do not shrink a segment shared with other procs, as the
		 * to-be-freed address space may have been passed to the kernel
		 * already by another proc and is past the validaddr stage.
		 */
		if(s->ref.ref > 1){
			qunlock(&s->qlk);
			error(Einuse);
		}
		oldpages = s->size;
		mfreeseg(s, newtop, (s->top-newtop)/BY2PG);
		s->top = newtop;
		s->size = newsize;
		qunlock(&s->qlk);
		flushmmu();
		if(oldpages > newsize)
			procmemdecommit(up, oldpages - newsize);
		return 0;
	}

	for(i = 0; i < NSEG; i++) {
		ns = up->seg[i];
		if(ns == nil || ns == s)
			continue;
		if(newtop > ns->base && s->base < ns->top) {
			qunlock(&s->qlk);
			error(Esoverlap);
		}
	}

	if(newsize > (SEGMAPSIZE*PTEPERTAB)) {
		qunlock(&s->qlk);
		error(Enovmem);
	}
	mapsize = ROUND(newsize, PTEPERTAB)/PTEPERTAB;
	if(mapsize > s->mapsize){
		map = malloc(mapsize*sizeof(Pte*));
		if(map == nil){
			qunlock(&s->qlk);
			error(Enomem);
		}
		memmove(map, s->map, s->mapsize*sizeof(Pte*));
		if(s->map != s->ssegmap)
			free(s->map);
		s->map = map;
		s->mapsize = mapsize;
	}

	if(newsize > s->size){
		if(procmemcommit(up, newsize - s->size) < 0){
			qunlock(&s->qlk);
			error(Enovmem);
		}
	}

	s->top = newtop;
	s->size = newsize;
	qunlock(&s->qlk);
	return 0;
}

/*
 *  Must be called with s locked.
 *  This relies on s->ref.ref > 1 indicating that
 *  the segment is shared with other processes
 *  different from the calling one.
 *  The calling process (up) is responsible for
 *  flushing its own TBL by calling flushmmu()
 *  afterwards.
 */
void
mfreeseg(Segment *s, uintptr start, ulong pages)
{
	uintptr off;
	Pte **pte, **emap;
	Page **pg, **pe, *entry;

	if(pages == 0)
		return;

	switch(s->type&SG_TYPE){
	case SG_PHYSICAL:
	case SG_FIXED:
	case SG_STICKY:
		return;
	case SG_REGION:
		/* Capability regions own a shared Page array; a partial free would
		 * decrement backing references behind other mappings.  Unmap the
		 * whole region instead. */
		return;
	}

	/*
	 * we have to make sure other processors flush the
	 * entry from their TLBs before the page is freed.
	 */
	if(s->ref.ref > 1)
		procflushseg(s);

	off = start-s->base;
	pte = &s->map[off/PTEMAPMEM];
	off = (off&(PTEMAPMEM-1))/BY2PG;
	for(emap = &s->map[s->mapsize]; pte < emap; pte++, off = 0) {
		if(*pte == nil) {
			off = PTEPERTAB - off;
			if(off >= pages)
				return;
			pages -= off;
			continue;
		}
		pg = &(*pte)->pages[off];
		for(pe = &(*pte)->pages[PTEPERTAB]; pg < pe; pg++) {
			if((entry = *pg) != nil){
				*pg = nil;
				putpage(entry);
				s->used--;
			}
			if(--pages == 0)
				return;
		}
	}
}

Segment*
isoverlap(uintptr va, uintptr len)
{
	int i;
	Segment *ns;
	uintptr newtop;

	newtop = va+len;
	for(i = 0; i < NSEG; i++) {
		ns = up->seg[i];
		if(ns == nil)
			continue;
		if(newtop > ns->base && va < ns->top)
			return ns;
	}
	return nil;
}

Physseg*
addphysseg(Physseg* new)
{
	Physseg *ps;

	/*
	 * Check not already entered and there is room
	 * for a new entry and the terminating null entry.
	 */
	lock(&physseglock);
	for(ps = physseg; ps->name; ps++){
		if(strcmp(ps->name, new->name) == 0){
			unlock(&physseglock);
			return nil;
		}
	}
	if(ps-physseg >= nelem(physseg)-2){
		unlock(&physseglock);
		return nil;
	}
	*ps = *new;
	unlock(&physseglock);

	return ps;
}

Physseg*
findphysseg(char *name)
{
	Physseg *ps;

	for(ps = physseg; ps->name; ps++)
		if(strcmp(ps->name, name) == 0)
			return ps;

	return nil;
}

uintptr
segattach(int attr, char *name, uintptr va, uintptr len)
{
	int sno, charged;
	ulong npages;
	Segment *s, *os;
	Physseg *ps;

	if(va != 0 && va >= USTKTOP)
		error(Ebadarg);

	charged = 0;
	npages = 0;
	qlock(&up->seglock);
	if(waserror()){
		if(charged)
			procmemdecommit(up, npages);
		qunlock(&up->seglock);
		nexterror();
	}
		
	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;

	if(sno == NSEG)
		error(Enovmem);

	/* round up va+len */
	len += va & (BY2PG-1);
	len = PGROUND(len);

	if(len == 0)
		error(Ebadarg);

	/*
	 * Find a hole in the address space.
	 * Starting at the lowest possible stack address - len,
	 * check for an overlapping segment, and repeat at the
	 * base of that segment - len until either a hole is found
	 * or the address space is exhausted.  Ensure that we don't
	 * map the zero page.
	 */
	if(va == 0) {
		for (os = up->seg[SSEG]; os != nil; os = isoverlap(va, len)) {
			va = os->base;
			if(len >= va)
				error(Enovmem);
			va -= len;
		}
	}

	va &= ~(BY2PG-1);
	if(va == 0 || (va+len) > USTKTOP || (va+len) < va)
		error(Ebadarg);

	if(isoverlap(va, len) != nil)
		error(Esoverlap);

	ps = findphysseg(name);
	if(ps == nil)
		error(Ebadarg);

	if(len > ps->size)
		error(Enovmem);

	/* Turn off what is not allowed */
	attr &= ~(SG_TYPE | SG_CACHED | SG_DEVICE);

	/* Copy in defaults */
	attr |= ps->attr;

	npages = len/BY2PG;
	if(procmemcommit(up, npages) < 0)
		error(Enovmem);
	charged = 1;
	s = newseg(attr, va, npages);
	s->pseg = ps;
	up->seg[sno] = s;
	charged = 0;
	qunlock(&up->seglock);
	poperror();

	return va;
}

static void
segflush(void *va, uintptr len)
{
	uintptr from, to, off;
	Segment *s;
	Pte *pte;
	Page **pg, **pe;

	from = (uintptr)va;
	to = from + len;
	to = PGROUND(to);
	from &= ~(BY2PG-1);
	if(to < from)
		error(Ebadarg);

	while(from < to) {
		s = seg(up, from, 1);
		if(s == nil)
			error(Ebadarg);

		s->flushme = 1;
		if(s->ref.ref > 1)
			procflushseg(s);
	more:
		len = (s->top < to ? s->top : to) - from;
		if(s->mapsize > 0){
			off = from-s->base;
			pte = s->map[off/PTEMAPMEM];
			off &= PTEMAPMEM-1;
			if(off+len > PTEMAPMEM)
				len = PTEMAPMEM-off;
			if(pte != nil) {
				pg = &pte->pages[off/BY2PG];
				pe = pg + len/BY2PG;
				while(pg < pe) {
					settxtflush(*pg, !pagedout(*pg));
					pg++;
				}
			}
		}
		from += len;
		if(from < to && from < s->top)
			goto more;
		qunlock(&s->qlk);
	}
}

uintptr
syssegflush(sysva_list list)
{
	void *va;
	ulong len;

	va = sysva_arg(list, void*);
	len = sysva_arg(list, ulong);
	segflush(va, len);
	flushmmu();
	return 0;
}

void
segclock(uintptr pc)
{
	Segment *s;

	s = up->seg[TSEG];
	if(s == nil || s->profile == nil)
		return;
	s->profile[0] += TK2MS(1);
	if(pc >= s->base && pc < s->top) {
		pc -= s->base;
		s->profile[pc>>LRESPROF] += TK2MS(1);
	}
}

enum {
	/* commands to segmentioproc */
	Cnone=0,
	Cread,
	Cwrite,
	Cdie,
};

static int
cmddone(void *arg)
{
	Segio *sio = arg;

	return sio->cmd == Cnone;
}

static void
docmd(Segio *sio, int cmd)
{
	sio->err = nil;
	sio->cmd = cmd;
	while(waserror())
		;
	wakeup(&sio->cmdwait);
	sleep(&sio->replywait, cmddone, sio);
	poperror();
	if(sio->err != nil)
		error(sio->err);
}

static int
cmdready(void *arg)
{
	Segio *sio = arg;

	return sio->cmd != Cnone;
}

static void
segmentioproc(void *arg)
{
	Segio *sio = arg;
	int done;
	int sno;

	qlock(&up->seglock);
	for(sno = 0; sno < NSEG; sno++)
		if(up->seg[sno] == nil && sno != ESEG)
			break;
	if(sno == NSEG)
		panic("segmentkproc");
	sio->p = up;
	incref(&sio->s->ref);
	up->seg[sno] = sio->s;
	qunlock(&up->seglock);

	while(waserror())
		;
	for(done = 0; !done;){
		sleep(&sio->cmdwait, cmdready, sio);
		if(waserror())
			sio->err = up->errstr;
		else {
			if(sio->s != nil && up->seg[sno] != sio->s){
				Segment *tmp;
				qlock(&up->seglock);
				incref(&sio->s->ref);
				tmp = up->seg[sno];
				up->seg[sno] = sio->s;
				putseg(tmp);
				qunlock(&up->seglock);
				flushmmu();
			}
			switch(sio->cmd){
			case Cread:
				memmove(sio->data, sio->addr, sio->dlen);
				break;
			case Cwrite:
				memmove(sio->addr, sio->data, sio->dlen);
				if(sio->s->flushme)
					segflush(sio->addr, sio->dlen);
				break;
			case Cdie:
				done = 1;
				break;
			}
			poperror();
		}
		sio->cmd = Cnone;
		wakeup(&sio->replywait);
	}

	pexit("done");
}

long
segio(Segio *sio, Segment *s, void *a, long n, vlong off, int read)
{
	uintptr m;
	void *b;

	b = a;
	if(s != nil){
		m = s->top - s->base;
		if(off < 0 || off >= m){
			if(!read)
				error(Ebadarg);
			return 0;
		}
		if(off+n > m){
			if(!read)
				error(Ebadarg);	
			n = m - off;
		}

		if((uintptr)a < KZERO) {
			b = smalloc(n);
			if(waserror()){
				free(b);
				nexterror();
			}
			if(!read)
				memmove(b, a, n);
		}
	}

	eqlock(&sio->qlk);
	if(waserror()){
		qunlock(&sio->qlk);
		nexterror();
	}
	sio->s = s;
	if(s == nil){
		if(sio->p != nil){
			docmd(sio, Cdie);
			sio->p = nil;
		}
		qunlock(&sio->qlk);
		poperror();
		return 0;
	}
	if(sio->p == nil){
		sio->cmd = Cnone;
		kproc("segmentio", segmentioproc, sio);
	}
	sio->addr = (char*)s->base + off;
	sio->data = b;
	sio->dlen = n;
	docmd(sio, read ? Cread : Cwrite);
	qunlock(&sio->qlk);
	poperror();

	if(a != b){
		if(read)
			memmove(a, b, n);
		free(b);
		poperror();
	}
	return n;
}
