/*
 * Native GE/O volume profile (docs/O-FORMAT.md).  The initial PoC is
 * only the profile, the flat catalog and plain GE records: no object
 * transactions, FS mounts, 9P or path resolution.  O records are Ko
 * keys in the volume's snapshot tree; opening an existing volume only
 * reads it and never formats.
 */
#include "gefs.h"

/*
 * Open an existing O volume: GE's blocks/tree/recovery, the O profile
 * and version, and the init Space's catalog entry.  An FS volume is
 * rejected here, before any write.
 */
static void*
oopen0(char *dev, void *oblk, vlong off, vlong size, int rw,
	long (*oblkrw)(void*, void*, long, uvlong, int),
	int (*oblkflush)(void*))
{
	Gefs *fs;
	uvlong v;

	fs = geopenblk(dev, oblk, off, size, rw, oblkrw, oblkflush);
	if(!(fs->flag & Ovolflag))
		error("O: not an O volume (legacy FS profile)");
	v = fs->flag & Ovolvermask;
	if(v != Ovolvers)
		error("O: unsupported volume version %llud", v);
	/* A flat O volume has one header and no name-to-SpaceID catalog. */
	{
		char b = Kobj, buf[Kvmax];
		Key key;
		Kvp kv;
		key.k = &b;
		key.nk = 1;
		if(!btlookup(&fs->snap, &key, &kv, buf, sizeof buf) ||
		   kv.val.nv < 24)
			error("O: missing flat volume header");
	}
	return fs;
}

void*
oopenblk(char *dev, void *oblk, vlong off, vlong size, int rw,
	long (*oblkrw)(void*, void*, long, uvlong, int),
	int (*oblkflush)(void*))
{
	return oopen0(dev, oblk, off, size, rw, oblkrw, oblkflush);
}

/* Plan2001 Stage 4: the port's syscall boundary.  These keep Ospace and
 * Gefs opaque to sysgefs.c, whose 6c signatures for an incomplete
 * struct would otherwise differ from this package's. */
void*
onativeopen(void *v, char *name)
{
	return ospaceopen(v, name);
}

uvlong
onativesid(void *v)
{
 USED(v);
	return 0; /* flat volume: no SpaceID */
}

long
onativeget(void *sp, uvlong oid, void *buf, long n, uvlong *ver)
{
	return ospaceget(sp, oid, buf, n, ver);
}

void
onativeput(void *sp, uvlong oid, void *buf, long n, uvlong owner)
{
	ospaceput(sp, oid, buf, n, owner);
}

long
onativecreate(void *sp, void *buf, long n, uvlong *oid, uvlong owner)
{
	return ospacecreate(sp, buf, n, oid, owner);
}

uvlong
onativecommit(void *sp)
{
	return ospacecommit(sp);
}

void
onativeclose(void *sp)
{
	ospaceclose(sp);
}
