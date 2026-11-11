#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

Palloc palloc;

/*
 * Non-swapping memory-pressure thresholds.
 */
static struct
{
	ulong	highwater;
	ulong	headroom;
	Rendez	r;
} reclaimcfg;

static int	reclaim(void);
static void	kickreclaim(void);
static void	reclaimer(void*);

ulong
nkpages(Confmem *cm)
{
	return ((cm->klimit - cm->kbase) + BY2PG-1) / BY2PG;
}

void
pageinit(void)
{
	int color, i, j;
	Page *p, **t;
	Confmem *cm;
	vlong m, u;

	if(palloc.pages == nil){
		ulong np;

		np = 0;
		for(i=0; i<nelem(conf.mem); i++){
			cm = &conf.mem[i];
			np += cm->npage - nkpages(cm);
		}
		palloc.pages = xalloc(np*sizeof(Page));
		if(palloc.pages == nil)
			panic("pageinit");
	}

	color = 0;
	palloc.freecount = 0;
	palloc.head = nil;

	t = &palloc.head;
	p = palloc.pages;

	for(i=0; i<nelem(conf.mem); i++){
		cm = &conf.mem[i];
		for(j=nkpages(cm); j<cm->npage; j++){
			memset(p, 0, sizeof *p);
			p->pa = cm->base+j*BY2PG;
			if(cankaddr(p->pa) && (KADDR(p->pa) == nil || KADDR(p->pa) == (void*)-BY2PG))
				continue;
			p->color = color;
			color = (color+1)%NCOLOR;
			*t = p, t = &p->next;
			palloc.freecount++;
			p++;
		}
	}

	palloc.user = p - palloc.pages;
	u = palloc.user*BY2PG;

	/* Reclaim watermarks. */
	reclaimcfg.highwater = (palloc.user*5)/100;
	reclaimcfg.headroom = reclaimcfg.highwater + (reclaimcfg.highwater/4);

	m = 0;
	for(i=0; i<nelem(conf.mem); i++)
		if(conf.mem[i].npage)
			m += conf.mem[i].npage*BY2PG;
	m += PGROUND(end - (char*)KTZERO);

	print("%lldM memory: ", (m+1024*1024-1)/(1024*1024));
	print("%lldM kernel data, ", (m-u+1024*1024-1)/(1024*1024));
	print("%lldM user\n", u/(1024*1024));

	kproc("reclaimer", reclaimer, nil);
}

void
freepages(Page *head, Page *tail, ulong np)
{
	if(head == nil)
		return;
	if(tail == nil){
		tail = head;
		for(np = 1;; np++){
			tail->ref.ref = 0;
			if(tail->next == nil)
				break;
			tail = tail->next;
		}
	}
	lock(&palloc.lk);
	tail->next = palloc.head;
	palloc.head = head;
	if(palloc.freecount <= reclaimcfg.highwater){
		palloc.freecount += np;
		if(palloc.freecount > reclaimcfg.highwater)
			wakeup(&palloc.pwait[1].rz);
		wakeup(&palloc.pwait[0].rz);
	} else {
		palloc.freecount += np;
	}
	wakeup(&reclaimcfg.r);
	unlock(&palloc.lk);
}

int
needpages(void*)
{
	return palloc.freecount < reclaimcfg.headroom;
}

static int
havepages(void*)
{
	return palloc.freecount > reclaimcfg.highwater || up->noswap && palloc.freecount > 0;
}

/*
 * There is no backing page cache or swap image to reclaim.  Report only
 * whether the free-page headroom has been restored.
 */
static int
reclaim(void)
{
	return !needpages(nil);
}

static void
kickreclaim(void)
{
	wakeup(&reclaimcfg.r);
}

static void
reclaimer(void*)
{
	while(waserror())
		;

	for(;;){
		up->psstate = "Reclaim";
		reclaim();
		up->psstate = "Idle";
		sleep(&reclaimcfg.r, return0, nil);
	}
}

static Page*
pagepop(uintptr va)
{
	Page *p, **l;
	int color;

	if(palloc.head == nil)
		return nil;

	/* First try for our colour */
	color = getpgcolor(va);
	l = &palloc.head;
	for(p = *l; p != nil; p = p->next){
		if(p->color == color)
			break;
		l = &p->next;
	}

	if(p == nil) {
		l = &palloc.head;
		p = *l;
	}

	*l = p->next;
	p->next = nil;
	if(--palloc.freecount <= reclaimcfg.highwater)
		kickreclaim();

	p->ref.ref = 1;
	p->va = va;
	p->modref = 0;
	inittxtflush(p);

	return p;
}

Page*
newpage(uintptr va, QLock *locked)
{
	Page *p;

	lock(&palloc.lk);
	while(!havepages(nil)){
		unlock(&palloc.lk);
		if(locked)
			qunlock(locked);

		if(!waserror()){
			/*
			 * If the free-page headroom has not been restored,
			 * block for another process to release pages.
			 */
			if(reclaim() == 0){
				Rendezq *q;

				q = &palloc.pwait[!up->noswap];
				eqlock(&q->qlk);
				if(!waserror()){
					sleep(&q->rz, havepages, nil);
					poperror();
				}
				qunlock(&q->qlk);
			}
			poperror();
		}

		/*
		 * If called from fault and we lost the lock from
		 * underneath don't waste time allocating and freeing
		 * a page. Fault will call newpage again when it has
		 * reacquired the locks
		 */
		if(locked)
			return nil;

		lock(&palloc.lk);
	}

	p = pagepop(va);
	unlock(&palloc.lk);
	return p;
}

/*
 * Allocate one page without sleeping for reclaim.  An explicit allocation
 * that has a process budget and an error return (capability-region backing)
 * must fail with Enomem instead of blocking forever when memory is exhausted.
 * The ordinary fault path keeps using newpage().
 */
Page*
newpagenowait(uintptr va)
{
	Page *p;

	lock(&palloc.lk);
	p = pagepop(va);
	unlock(&palloc.lk);
	return p;
}

/*
 *  deadpage() decrements the page refcount
 *  and returns the page when it becomes freeable.
 */
Page*
deadpage(Page *p)
{
	if(decref(&p->ref) != 0)
		return nil;
	return p;
}

void
putpage(Page *p)
{
	p = deadpage(p);
	if(p != nil)
		freepages(p, p, 1);
}

void
copypage(Page *f, Page *t)
{
	KMap *ks, *kd;

	ks = kmap(f);
	kd = kmap(t);
	memmove((void*)VA(kd), (void*)VA(ks), BY2PG);
	kunmap(ks);
	kunmap(kd);
}

Page*
fillpage(Page *p, int c)
{
	KMap *k;

	if(p != nil){
		k = kmap(p);
		memset((void*)VA(k), c, BY2PG);
		kunmap(k);
	}
	return p;
}

void
zeroprivatepages(void)
{
	Page *p, *pe;

	/*
	 * in case of a panic, we might not have a process
	 * context to do the clearing of the private pages.
	 */
	if(up == nil){
		assert(panicking);
		return;
	}

	lock(&palloc.lk);
	pe = palloc.pages + palloc.user;
	for(p = palloc.pages; p != pe; p++) {
		if(p->modref & PG_PRIV){
			incref(&p->ref);
			fillpage(p, 0);
			decref(&p->ref);
		}
	}
	unlock(&palloc.lk);
}
