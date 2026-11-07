#include "gefs.h"

static void
wrbarrier(Gefs *fs)
{
	tracev("barrier", agetv(&fs->qgen));
	aincv(&fs->qgen, 1);
}

static void
wrwait(Gefs *fs)
{
	Qent qe;
	int i;

	tracev("wrwait", agetv(&fs->qgen));
	aincv(&fs->qgen, 1);
	fs->syncing = fs->nsyncers;
	for(i = 0; i < fs->nsyncers; i++){
		qe.op = Qfence;
		qe.bp.addr = 0;
		qe.bp.hash = -1;
		qe.bp.gen = -1;
		qe.b = nil;
		qput(&fs->syncq[i], qe);
	}
	aincv(&fs->qgen, 1);
	while(fs->syncing != 0)
		rsleep(&fs->syncrz);
	gflushdev(fs);	/* Plan2001: the disk's write cache too */
	tracev("flushed", agetv(&fs->qgen));
}

static void
dosync(Gefs *fs, int id)
{
	Arena *a;
	Dlist *sdl, ddl;
	int i;

	qlock(&fs->synclk);
	fs->syncproc = up;	/* Plan2001: a full disk's sync takes the reserve */
	if(waserror()){
		fprint(2, "failed to sync: %s\n", errmsg());
		aincl(&fs->rdonly, 1);
		fs->syncproc = nil;
		qunlock(&fs->synclk);
		nexterror();
	}

	/*
	 * Wait for data that we're syncing to hit disk
	 */
	tracem("flush1");
	wrbarrier(fs);
	/*
	 * pass 0: Update all open snapshots, and
	 *  pack the blocks we want to sync. Snap
	 *  while holding the write lock, and then
	 *  wait until all the blocks they point at
	 *  have hit disk; once they're on disk, we
	 *  can take a consistent snapshot.
         */
	sdl = emalloc(sizeof(Dlist), 1);
	sdl->limbo.fs = fs;
	qlock(&fs->mutlk);
	epochstart(fs, id);
	if(waserror()){
		free(sdl);
		epochend(fs, id);
		aincl(&fs->rdonly, 1);
		qunlock(&fs->mutlk);
		nexterror();
	}
	tracem("packb");

	/*
	 * Now that we've updated the snaps, we can sync the
	 * dlist; the snap tree will not change from here.
	 */
	dlsync(fs);
	*sdl = fs->snapdl;
	ddl = fs->dropdl;
	fs->snapdl.hd = Zb;
	fs->snapdl.tl = Zb;
	fs->snapdl.ins = nil;
	fs->dropdl.hd = Zb;
	fs->dropdl.tl = Zb;
	fs->dropdl.ins = nil;
	traceb("syncdl.dl", sdl->hd);
	traceb("syncdl.rb", fs->snap.bp);
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(&a->qlk);
		/*
		 * because the log uses preallocated
		 * blocks, we need to write the log
		 * block out synchronously, or it may
		 * get reused.
		 */
		logbarrier(a, agetv(&fs->qgen));
		flushlog(a);

		packarena(a->h0->data, Blksz, a);
		packarena(a->h1->data, Blksz, a);
		finalize(a->h0);
		finalize(a->h1);
		fs->arenabp[i] = a->h0->bp;
		qunlock(&a->qlk);
	}
	assert(fs->snapdl.hd.addr == -1);
	traceb("packsb.rb", fs->snap.bp);
	packsb(fs->sb0->buf, Blksz, fs);
	packsb(fs->sb1->buf, Blksz, fs);
	finalize(fs->sb0);
	finalize(fs->sb1);
	fs->snap.dirty = 0;
	epochend(fs, id);
	qunlock(&fs->mutlk);
	poperror();

	/*
	 * pass 1: sync block headers; if we crash here,
	 *  the block footers are consistent, and we can
	 *  use them.
	 */
	tracem("arenas0");
	for(i = 0; i < fs->narena; i++)
		enqueue(fs->arenas[i].h0);
	wrbarrier(fs);
	wrwait(fs);

	/*
	 * pass 2: sync superblock; we have a consistent
	 * set of block headers, so if we crash, we can
	 * use the loaded block headers; the footers will
	 * get synced after so that we can use them next
	 * time around.
         */
	tracem("supers");
	enqueue(fs->sb0);
	enqueue(fs->sb1);
	wrbarrier(fs);
	wrwait(fs);

	/*
	 * pass 3: sync block footers; if we crash here,
	 *  the block headers are consistent, and we can
	 *  use them.
         */
	tracem("arenas1");
	for(i = 0; i < fs->narena; i++)
		enqueue(fs->arenas[i].h1);

	/*
	 * Wait for commit: all of the previous changes
	 * must hit disk before we start cleaing up the
	 * state.
	 */
	wrwait(fs);

	/*
	 * Pass 4: clean up the old snap tree's deadlist.
	 * we need to wait for all the new data to hit disk
	 * before we can free anything, otherwise it gets
	 * clobbered.
	 */
	tracem("snapdl");
	freedl(&ddl, 0);
	limbo(DFdlist, &sdl->limbo);
	fs->syncproc = nil;
	qunlock(&fs->synclk);
	tracem("synced");
	poperror();
}

/* synclk is also syncrz's condition lock and rsleep releases it.
 * A separate admission lock prevents two full syncs from packing and
 * writing the same mutable arena/superblock buffers concurrently. */
static void
sync(Gefs *fs, int id)
{
	qlock(&fs->syncserial);
	if(waserror()){
		qunlock(&fs->syncserial);
		nexterror();
	}
	dosync(fs, id);
	poperror();
	qunlock(&fs->syncserial);
}

/* Plan2001: sync from a GE caller that is not the sweeper (stage 1's probe) */
void
gesync(Gefs *fs, int id)
{
	sync(fs, id);
}

/* Plan2001: the queue with a qlock, as the kernel has no semacquire for it */
Chan*
mkchan(int size)
{
	Chan *c;

	if((c = mallocz(sizeof(Chan) + size*sizeof(void*), 1)) == nil)
		sysfatal("create channel");
	c->fullrz.l = &c->lk;
	c->emptyrz.l = &c->lk;
	c->size = size;
	c->rp = c->args;
	c->wp = c->args;
	return c;

}

void*
chrecv(Chan *c)
{
	void *a;

	qlock(&c->lk);
	while(c->n == 0)
		rsleep(&c->emptyrz);
	a = *c->rp;
	if(++c->rp >= &c->args[c->size])
		c->rp = c->args;
	c->n--;
	rwakeup(&c->fullrz);
	qunlock(&c->lk);
	return a;
}

void
chsend(Chan *c, void *m)
{
	qlock(&c->lk);
	while(c->n == c->size)
		rsleep(&c->fullrz);
	*c->wp = m;
	if(++c->wp >= &c->args[c->size])
		c->wp = c->args;
	c->n++;
	rwakeup(&c->emptyrz);
	qunlock(&c->lk);
}

void
runsweep(Gefs *fs, int id, void*)
{
	Bptr bp, nb, *oldhd;
	int i, op;
	Arena *a;
	Blk *b;
	Osweepmsg *om;

	if((oldhd = calloc(fs->narena, sizeof(Bptr))) == nil)
		sysfatal("malloc log heads");
	fs->sweepproc = up;	/* Plan2001: the reserve (blkalloc_lk) */
	while(1){
		om = chrecv(fs->osweepchan);
		op = om->op;
		free(om);
		if(waserror()){
			fprint(2, "sweep error: %s\n", errmsg());
			continue;
		}
		switch(op){
		case OsweepHalt:
			if(!agetl(&fs->rdonly)){
				aincl(&fs->rdonly, 1);
				/* cycle through all epochs to clear them.  */
				for(i = 0; i < 3; i++)
					while(!epochclean(fs))
						sleep(1);
				if(waserror()){
					fprint(2, "halt failed: %s\n", errmsg());
					break;
				}
				sync(fs, id);
				poperror();	/* Plan2001: upstream exits here */
			}
			gefshalted(fs);	/* Plan2001: the kprocs stay, read-only */
			break;
		case OsweepSync:
			qlock(&fs->syncserial);
			tracem("syncreq");
			if(waserror()){
				fprint(2, "sync error: %s\n", errmsg());
				qunlock(&fs->syncserial);
				aincl(&fs->rdonly, 1);
				nexterror();
			}
			if(!fs->snap.dirty || agetl(&fs->rdonly))
				goto Syncout;
			for(i = 0; i < fs->narena; i++){
				a = &fs->arenas[i];
				oldhd[i].addr = -1;
				oldhd[i].hash = -1;
				oldhd[i].gen = -1;
				qlock(&a->qlk);
				/*
				 * arbitrary heuristic -- try compressing
				 * when the log doubles in size.
				 */
				if(a->nlog >= 2*a->lastlogsz){
					oldhd[i] = a->loghd;
					epochstart(fs, id);
					fs->syncproc = up;	/* Plan2001: the reserve */
					if(waserror()){
						fs->syncproc = nil;
						epochend(fs, id);
						qunlock(&a->qlk);
						nexterror();
					}
					compresslog(a);
					fs->syncproc = nil;
					epochend(fs, id);
					poperror();
				}
				qunlock(&a->qlk);
				epochclean(fs);
			}

			dosync(fs, id);	/* admission covers compression and sync */

			for(i = 0; i < fs->narena; i++){
				for(bp = oldhd[i]; bp.addr != -1; bp = nb){
					qlock(&fs->mutlk);
					epochstart(fs, id);
					if(waserror()){
						epochend(fs, id);
						qunlock(&fs->mutlk);
						nexterror();
					}
					b = getblk(fs, bp, 0);
					nb = b->logp;
					freeblk(fs, nil, b);
					dropblk(b);
					epochend(fs, id);
					qunlock(&fs->mutlk);
					poperror();
					epochclean(fs);
				}
			}
Syncout:
			qunlock(&fs->syncserial);
			poperror();
			break;

		default:
			fprint(2, "gefs%d: unexpected sweep op %d\n", fs->id, op);
			break;
		}
		poperror();
		assert(estacksz() == 0);
	}
}

/* Plan2001: a sync every 5 seconds; no automatic snapshots (loadautos) */
void
runtasks(Gefs *fs, int, void *)
{
	Osweepmsg *om;

	while(1){
		sleep(5000);
		if(agetl(&fs->rdonly))
			continue;
		if(waserror()){
			fprint(2, "task error: %s\n", errmsg());
			continue;
		}
		om = emalloc(sizeof(Osweepmsg), 1);
		om->op = OsweepSync;
		chsend(fs->osweepchan, om);
		poperror();
	}
}
