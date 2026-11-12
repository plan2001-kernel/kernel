/*
 * Native GE/O v2 volume profile (docs/OBJECT-STORAGE.md).
 * The GE snapshot tree holds one volume header and flat Kobj/Kowner OID
 * records. There is no persistent SpaceID, Ko catalog, FS mount or path.
 * Opening an existing volume never formats it.
 */
#include "gefs.h"
#include "objint.h"

/* Reject FS profiles, unknown versions and damaged volume headers before
 * granting any O handle. The transitional "init" selector belongs only to
 * the phase-3 API wrapper, not to the GE/O on-disk format. */
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
	/* Validate the single volume-global publication epoch as well as
	 * the key's existence. A malformed header is never writable. */
	{
		Ohdr h;
		if(!gethdr(&fs->snap, &h))
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
