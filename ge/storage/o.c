/*
 * Native GE/O v2 volume profile (docs/OBJECT-STORAGE.md).
 * The GE snapshot tree holds one volume header and flat Kobj/Kowner OID
 * records. There is no persistent SpaceID, Ko catalog, FS mount or path.
 * Opening an existing volume never formats it.
 */
#include "gefs.h"
#include "objint.h"

/* Reject non-O profiles and unknown versions before accessing flat OIDs. */
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

