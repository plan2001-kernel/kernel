/*
 * Linux host entry for the real GE/O v1 reamer (ge/format/ream.c).
 *
 * This is the O-only call graph: the arena/log/B-tree/superblock code is
 * the repository's own GE implementation; only Plan 9 libc, process and
 * file-descriptor glue come from tools/host/ge-compat.  No 9P server,
 * FS root, adm tree or user table is started.
 */
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

extern int	fsync(int);
extern char	*seeddir;

Blk	*blkbuf;

void hostfmtinstall(void);

enum {
	Hostcache	= 32*MiB,
};

static int
hostinit(vlong cachesz)
{
	Blk *b;

	if((fs = mallocz(sizeof(Gefs), 1)) == nil)
		return -1;
	fs->lrurz.l = &fs->lrulk;
	fs->cmax = cachesz/Blksz;
	if(fs->cmax > (1<<20))
		return -1;
	if((fs->bcache = mallocz(fs->cmax*sizeof(Bucket), 1)) == nil)
		return -1;
	if((blkbuf = mallocz(fs->cmax*sizeof(Blk), 1)) == nil)
		return -1;
	for(b = blkbuf; b != blkbuf+fs->cmax; b++){
		b->bp = Zb;
		b->magic = Magic;
		lrutop(b);
	}
	return 0;
}

/*
 * Returns 0 on success.  The caller owns path and has already checked
 * that it is a caller-selected regular image; the reamer itself refuses
 * anything shorter than 128 MiB + one GE block.
 */
int
o_format_run(char *path, char *seed)
{
	seeddir = seed;
	if(hostinit(Hostcache) < 0){
		fprint(2, "format-o: initialization failed\n");
		return -1;
	}
	hostfmtinstall();
	errctx = (Errctx**)privalloc();
	if((*errctx = mallocz(sizeof(Errctx), 1)) == nil){
		fprint(2, "format-o: no error context\n");
		return -1;
	}
	if(waserror()){
		fprint(2, "format-o: %s\n", errmsg());
		return -1;
	}
	reamo(path);
	poperror();
	if(fsync(fs->fd) < 0){
		fprint(2, "format-o: fsync: %s\n", errmsg());
		return -1;
	}
	close(fs->fd);
	return 0;
}
