/*
 * Native GE/O volume profile (docs/O-FORMAT.md).  The initial PoC is
 * only the profile, the flat catalog and plain GE records: no object
 * transactions, FS mounts, 9P or path resolution.  O records are Ko
 * keys in the volume's snapshot tree; opening an existing volume only
 * reads it and never formats.
 */
#include "gefs.h"

static void
okey(char *kbuf, char *name, Key *k)
{
	if(strlen(name) > Keymax-1)
		error("O: record name too long");
	kbuf[0] = Ko;
	memmove(kbuf+1, name, strlen(name));
	k->k = kbuf;
	k->nk = 1+strlen(name);
}

uvlong
ospaceid(Gefs *fs, char *name)
{
	char kbuf[Keymax], xbuf[Kvmax];
	Key k;
	Kvp kv;

	okey(kbuf, name, &k);
	if(!btlookup(&fs->snap, &k, &kv, xbuf, sizeof(xbuf)))
		error("O: no space '%s'", name);
	if(kv.val.nv != 8)
		error("O: bad space id length for '%s'", name);
	return UNPACK64(kv.val.v);
}

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
	if(ospaceid(fs, "init") != OspaceInit)
		error("O: init Space id mismatch");
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
 return ((Ospace*)v)->sid;
}

long
onativeget(void *sp, uvlong oid, void *buf, long n, uvlong *ver)
{
	return ospaceget(sp, oid, buf, n, ver);
}

void
onativeput(void *sp, uvlong oid, void *buf, long n)
{
	ospaceput(sp, oid, buf, n);
}

long
onativecreate(void *sp, void *buf, long n, uvlong *oid)
{
	return ospacecreate(sp, buf, n, oid);
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
