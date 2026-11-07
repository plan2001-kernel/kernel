/*
 * Storage Device core.
 *
 * The concrete SDev/SDunit/SDifc registry, controller initialization and
 * the native block operations used by the O storage backend.  This file
 * has no Dev/Chan/Qid/Dirtab dependency.  The canonical amd64 build has no
 * #S file adapter: controllers are reached only through SDifc and the
 * native block/flush entry points below.
 */
#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../core/error.h"

#include "../core/sd.h"

extern SDifc* sdifc[];

static	char	Enoata[]	= "raw ata commands not supported";
static	char	Enoscsi[]	= "raw scsi commands not supported";

char sddevletters[SDndev+1] = "0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";

SDev *sddevs[SDndev];
QLock sddevslock;

enum {
	Maxpart = 1<<8,		/* upper bound for the native partition array */
};

static QLock sdinits;
static int sdinitialized;

static void
freeperm(SDperm *perm)
{
	free(perm->name);
	perm->name = nil;
	free(perm->user);
	perm->user = nil;
}

static void
freeunit(SDunit *unit)
{
	int i;

	if(unit == nil)
		return;

	for(i = 0; i < unit->npart; i++)
		freeperm(&unit->part[i].perm);
	free(unit->part);

	freeperm(&unit->ctlperm);
	freeperm(&unit->rawperm);
	freeperm(&unit->perm);

	free(unit);
}

void
sdfreedev(SDev *sdev)
{
	int i;

	/* free units and their files */
	for(i = 0; i < sdev->nunit; i++)
		freeunit(sdev->unit[i]);

	free(sdev->unitflg);
	free(sdev->unit);
	free(sdev);
}

void
sdaddpart(SDunit* unit, char* name, uvlong start, uvlong end)
{
	SDpart *pp;
	int i, partno;

	/*
	 * Check name not already used
	 * and look for a free slot.
	 */
	if(unit->part != nil){
		partno = -1;
		for(i = 0; i < unit->npart; i++){
			pp = &unit->part[i];
			if(!pp->valid){
				if(partno == -1)
					partno = i;
				break;
			}
			if(strcmp(name, pp->perm.name) == 0){
				if(pp->start == start && pp->end == end)
					return;
				error(Ebadctl);
			}
		}
	}
	else{
		if((unit->part = malloc(sizeof(SDpart)*SDnpart)) == nil)
			error(Enomem);
		unit->npart = SDnpart;
		partno = 0;
	}

	/*
	 * If no free slot found then increase the
	 * array size (can't get here with unit->part == nil).
	 */
	if(partno == -1){
		if(unit->npart >= Maxpart)
			error(Enomem);
		if((pp = malloc(sizeof(SDpart)*(unit->npart+SDnpart))) == nil)
			error(Enomem);
		memmove(pp, unit->part, sizeof(SDpart)*unit->npart);
		free(unit->part);
		unit->part = pp;
		partno = unit->npart;
		unit->npart += SDnpart;
	}

	/*
	 * Check size and extent are valid.
	 */
	if(start > end || end > unit->sectors)
		error(Eio);
	pp = &unit->part[partno];
	pp->start = start;
	pp->end = end;
	kstrdup(&pp->perm.name, name);
	kstrdup(&pp->perm.user, eve);
	pp->perm.perm = 0640;
	pp->valid = 1;
}

void
sddelpart(SDunit* unit, char* name)
{
	int i;
	SDpart *pp;

	/*
	 * Look for the partition to delete.
	 * Can't delete if someone still has it open.
	 */
	pp = unit->part;
	for(i = 0; i < unit->npart; i++){
		if(pp->valid && strcmp(name, pp->perm.name) == 0)
			break;
		pp++;
	}
	if(i >= unit->npart)
		error(Ebadctl);
	if(strcmp(up->user, pp->perm.user) && !iseve())
		error(Eperm);
	pp->valid = 0;
	pp->vers++;
}

static void
sdincvers(SDunit *unit)
{
	int i;

	unit->vers++;
	if(unit->part){
		for(i = 0; i < unit->npart; i++){
			unit->part[i].valid = 0;
			unit->part[i].vers++;
		}
	}
}

int
sdinitpart(SDunit* unit)
{
	int nf;
	uvlong start, end;
	char *f[4], *p, *q, buf[10];

	if(unit->sectors > 0){
		unit->sectors = unit->secsize = 0;
		sdincvers(unit);
	}

	if(unit->inquiry[0] & 0xC0)
		return 0;
	switch(unit->inquiry[0] & 0x1F){
	case 0x00:			/* DA */
	case 0x04:			/* WORM */
	case 0x05:			/* CD-ROM */
	case 0x07:			/* MO */
		break;
	default:
		return 0;
	}

	if(unit->dev->ifc->online != nil)
		(*unit->dev->ifc->online)(unit);

	if(unit->sectors){
		sdincvers(unit);
		sdaddpart(unit, "data", 0, unit->sectors);

		/*
		 * Use partitions passed from boot program,
		 * e.g.
		 *	sdC0part=dos 63 123123/plan9 123123 456456
		 * This happens before /boot sets hostname so the
		 * partitions will have the null-string for user.
		 * The gen functions patch it up.
		 */
		snprint(buf, sizeof buf, "%spart", unit->perm.name);
		for(p = getconf(buf); p != nil; p = q){
			if((q = strchr(p, '/')) != nil)
				*q++ = '\0';
			nf = tokenize(p, f, nelem(f));
			if(nf < 3)
				continue;

			start = strtoull(f[1], 0, 0);
			end = strtoull(f[2], 0, 0);
			if(!waserror()){
				sdaddpart(unit, f[0], start, end);
				poperror();
			}
		}
	}

	return 1;
}

int
sdindex(int idno)
{
	char *p;

	p = strchr(sddevletters, idno);
	if(p == nil)
		return -1;
	return p-sddevletters;
}

SDev*
sdgetdev(int idno)
{
	SDev *sdev;
	int i;

	if((i = sdindex(idno)) < 0)
		return nil;

	qlock(&sddevslock);
	if((sdev = sddevs[i]) != nil)
		incref(&sdev->r);
	qunlock(&sddevslock);
	return sdev;
}

SDunit*
sdgetunit(SDev* sdev, int subno)
{
	SDunit *unit;
	char buf[32];

	if(subno < 0 || subno >= sdev->nunit)
		return nil;

	/*
	 * Associate a unit with a given device and sub-unit
	 * number on that device.
	 * The device will be probed if it has not already been
	 * successfully accessed.
	 */
	qlock(&sdev->unitlock);
	unit = sdev->unit[subno];
	if(unit == nil){
		/*
		 * Probe the unit only once. This decision
		 * may be a little severe and reviewed later.
		 */
		if(sdev->unitflg[subno]){
			qunlock(&sdev->unitlock);
			return nil;
		}
		sdev->unitflg[subno] = 1;

		unit = smalloc(sizeof(SDunit));
		snprint(buf, sizeof buf, "%s%x", sdev->name, subno);
		kstrdup(&unit->perm.name, buf);
		kstrdup(&unit->perm.user, eve);
		unit->perm.perm = 0555;
		unit->subno = subno;
		unit->dev = sdev;

		if(waserror())
			goto Error;
		if(!sdev->enabled)
			sdev->enabled = sdev->ifc->enable == nil || (*sdev->ifc->enable)(sdev);

		/*
		 * No need to lock anything here as this is only
		 * called before the unit is made available in the
		 * sdunit[] array.
		 */
		if(!sdev->enabled || (*sdev->ifc->verify)(unit) == 0){
			poperror();
		Error:
			qunlock(&sdev->unitlock);
			freeunit(unit);
			return nil;
		}
		poperror();
		sdev->unit[subno] = unit;
	}
	qunlock(&sdev->unitlock);
	return unit;
}

/*
 * Iterate the enumerated units without a Chan or namec, for the native
 * O block backend.  The returned unit keeps its SDev reference;
 * sdunitput drops it.
 */
SDunit*
sdunititer(Sditer *it)
{
	SDev *sdev;
	SDunit *unit;

	if(it->idno < 0)
		it->idno = 0;
	for(; it->idno < nelem(sddevs); it->idno++, it->subno = 0){
		if((sdev = sdgetdev(sddevletters[it->idno])) == nil)
			continue;
		for(; it->subno < sdev->nunit; it->subno++){
			if((unit = sdgetunit(sdev, it->subno)) != nil){
				it->subno++;
				return unit;
			}
		}
		decref(&sdev->r);
	}
	return nil;
}

void
sdunitput(SDunit *unit)
{
	if(unit != nil)
		decref(&unit->dev->r);
}

/*
 * Make a probed unit's geometry available to the native block backend.
 * No Chan or virtual subfile is created.
 */
int
sdunitready(SDunit *unit)
{
	int ok;

	qlock(&unit->ctl);
	if(waserror()){
		qunlock(&unit->ctl);
		return 0;
	}
	if(unit->sectors == 0)
		sdinitpart(unit);
	ok = unit->sectors != 0 && unit->secsize != 0;
	poperror();
	qunlock(&unit->ctl);
	return ok;
}

/*
 * Absolute block I/O for the native O backend.  This is sdbio's bounds,
 * alignment, bounce-buffer and media-change behavior without a Chan or
 * partition qid; off is a byte offset in the unit.
 */
long
sdblockrw(SDunit *unit, int write, void *a, long len, uvlong off)
{
	int allocd, locked, nchange;
	long l;
	uchar *b;
	ulong max, nb, offset;
	uvlong bno, end;

	if(unit == nil || len < 0)
		error(Ebadarg);
	qlock(&unit->ctl);
	nchange = 0;
	while(waserror()){
		if(strcmp(up->errstr, Eio) == 0 && unit->sectors == 0 && nchange++ == 0){
			sdinitpart(unit);
			continue;
		}
		qunlock(&unit->ctl);
		nexterror();
	}
	if(unit->sectors == 0 && !sdinitpart(unit))
		error(Eio);
	if(unit->secsize == 0)
		error(Eio);
	end = (uvlong)unit->sectors * unit->secsize;
	if(off > end || (uvlong)len > end - off)
		error(Eio);
	if(len == 0){
		qunlock(&unit->ctl);
		poperror();
		return 0;
	}
	bno = off/unit->secsize;
	nb = ((off + len + unit->secsize - 1)/unit->secsize) - bno;
	max = SDmaxio/unit->secsize;
	if(nb > max)
		nb = max;
	if(bno + nb > unit->sectors)
		nb = unit->sectors - bno;
	if(nb == 0)
		error(Eio);
	locked = (unit->inquiry[1] & 0x80) != 0;
	if(!locked){
		qunlock(&unit->ctl);
		poperror();
	}

	offset = off%unit->secsize;
	if(offset+len > nb*unit->secsize)
		len = nb*unit->secsize - offset;
	if(offset || write && len%unit->secsize)
		allocd = 2;
	else
		allocd = (uintptr)a > KZERO && ((uintptr)a & (BY2PG-1)) == 0 ? 0 : 1;
	if(allocd == 0)
		b = a;
	else{
		while((b = sdmalloc(nb*unit->secsize)) == nil)
			resrcwait("no memory for sdblockrw");
		if(waserror()){
			sdfree(b);
			nexterror();
		}
	}

	if(write){
		if(offset || len%unit->secsize){
			l = (*unit->dev->ifc->bio)(unit, 0, 0, b, nb, bno);
			if(l < 0)
				error(Eio);
			if(l < nb*unit->secsize){
				nb = l/unit->secsize;
				if(nb == 0)
					error(Eio);
				l = nb*unit->secsize - offset;
				if(len > l)
					len = l;
			}
		}
		if(allocd != 0)
			memmove(b+offset, a, len);
		l = (*unit->dev->ifc->bio)(unit, 0, 1, b, nb, bno);
		if(l < 0)
			error(Eio);
		if(l < offset)
			len = 0;
		else if(len > l - offset)
			len = l - offset;
	}else{
		l = (*unit->dev->ifc->bio)(unit, 0, 0, b, nb, bno);
		if(l < 0)
			error(Eio);
		if(l < offset)
			len = 0;
		else if(len > l - offset)
			len = l - offset;
		if(allocd != 0)
			memmove(a, b+offset, len);
	}

	if(allocd != 0){
		sdfree(b);
		poperror();
	}
	if(locked){
		qunlock(&unit->ctl);
		poperror();
	}
	return len;
}

/*
 * Synchronize-cache through the driver's raw request path, without
 * opening #S/.../raw.  This is the same 0x35 SCSI command that
 * gflushdev used to write through a Chan.
 */
int
sdblockflush(SDunit *unit)
{
	SDreq *r;
	uchar cmd[10];
	int rv;

	if(unit == nil)
		error(Ebadarg);
	qlock(&unit->raw);
	if(waserror()){
		qunlock(&unit->raw);
		nexterror();
	}
	r = smalloc(sizeof *r);
	if(waserror()){
		free(r);
		nexterror();
	}
	memset(r, 0, sizeof *r);
	memset(cmd, 0, sizeof cmd);
	cmd[0] = 0x35;
	r->unit = unit;
	memmove(r->cmd, cmd, sizeof cmd);
	r->clen = sizeof cmd;
	r->proto = SDcdb;
	r->status = ~0;
	rv = sdrio(r, nil, 0);
	if(rv < 0 || r->status != SDok)
		error(Eio);
	free(r);
	poperror();
	poperror();
	qunlock(&unit->raw);
	return 0;
}

/*
 * The one native controller-initialization owner.  The native boot calls
 * it from pc64/main.c; the guard makes enumeration happen exactly once.
 * There is no Dev reset adapter in the canonical O-only build.
 */
void
sdinitcontrollers(void)
{
	SDev *sdev, *s;
	int i, n;

	qlock(&sdinits);
	if(sdinitialized){
		qunlock(&sdinits);
		return;
	}
	sdinitialized = 1;

	/*
	 * Probe all known controller types and register any devices found.
	 */
	n = 0;
	for(i = 0; sdifc[i] != nil; i++){
		if(sdifc[i]->pnp == nil)
			continue;
		sdev = sdifc[i]->pnp();
		for(s = sdev; s != nil; s = s->next)
			n++;
		sdadddevs(sdev);
	}
	qunlock(&sdinits);
	print("sd: controllers initialized (%d) native-only\n", n);
}

/*
 * Kept for the native boot flow: sdinitcontrollers() owns the probe and the
 * exactly-once guard, so a second call is a no-op.
 */
void
sdreset(void)
{
	sdinitcontrollers();
}

void
sdadddevs(SDev *sdev)
{
	int i, j, id;
	SDev *next;

	for(; sdev; sdev=next){
		next = sdev->next;

		sdev->unit = malloc(sdev->nunit * sizeof(sdev->unit[0]));
		sdev->unitflg = malloc(sdev->nunit * sizeof(sdev->unitflg[0]));
		if(sdev->unit == nil || sdev->unitflg == nil){
			print("sdadddevs: out of memory\n");
		giveup:
			if(sdev->ifc->clear != nil)
				(*sdev->ifc->clear)(sdev);
			free(sdev->unitflg);
			free(sdev->unit);
			free(sdev);
			continue;
		}
		id = sdindex(sdev->idno);
		if(id < 0){
			print("sdadddevs: bad id number %d (%C)\n", id, id);
			goto giveup;
		}
		qlock(&sddevslock);
		for(i=0; i<nelem(sddevs); i++){
			if(sddevs[j = (id+i)%nelem(sddevs)] == nil){
				sdev->idno = sddevletters[j];
				sddevs[j] = sdev;
				snprint(sdev->name, sizeof sdev->name, "sd%c", sddevletters[j]);
				break;
			}
		}
		qunlock(&sddevslock);
		if(i == nelem(sddevs)){
			print("sdadddevs: out of device letters\n");
			goto giveup;
		}
	}
}

long
sdrio(SDreq* r, void* a, long n)
{
	char *errstr;
	int rv;
	void *data;
	SDunit *u;
	int (*f)(SDreq*);

	if(n >= SDmaxio || n < 0)
		error(Etoobig);
	u = r->unit;
	if(u->haversense && r->cmd[0] == 0x03){
		u->haversense = 0;
		r->rlen = sizeof u->rsense;
		if(r->rlen > n)
			r->rlen = n;
		memmove(a, u->rsense, r->rlen);
		r->status = SDok;
		return r->rlen;
	}

	data = nil;
	if(n > 0){
		while((data = sdmalloc(n)) == nil)
			resrcwait("no memory for sdrio");
		if(waserror()){
			r->data = nil;
			sdfree(data);
			nexterror();
		}
		if(r->write)
			memmove(data, a, n);
	}
	r->data = data;
	r->dlen = n;

	if(r->proto == SData){
		f = u->dev->ifc->ataio;
		errstr = Enoata;
	}else{
		f = u->dev->ifc->rio;
		errstr = Enoscsi;
	}
	if(f == nil)
		error(errstr);
	rv = (*f)(r);
	if(r->flags & SDvalidsense){
		memmove(u->rsense, r->sense, sizeof u->rsense);
		u->haversense = 1;
	}
	if(rv != SDok)
		error(Eio);

	r->data = nil;
	if(n > 0){
		if(n < r->rlen)
			n = r->rlen;
		if(!r->write && n > 0)
			memmove(a, data, n);
		sdfree(data);
		poperror();
	}
	return n;
}

/*
 * SCSI simulation for non-SCSI devices
 *
 * see /sys/src/cmd/scuzz/sense.c for information on key.
 * see /sys/lib/scsicodes for asc:ascq codes
 */
int
sdsetsense(SDreq *r, int status, int key, int asc, int ascq)
{
	int len;
	SDunit *unit;

	unit = r->unit;
	unit->sense[0] = 0x80 | 0x70;	/* valid; fixed-format */
	unit->sense[2] = key;
	unit->sense[12] = asc;
	unit->sense[13] = ascq;

	r->status = status;
	if(status == SDcheck && !(r->flags & SDnosense)){
		/* request sense case from sdfakescsi */
		len = sizeof unit->sense;
		if(len > sizeof r->sense-1)
			len = sizeof r->sense-1;
		memmove(r->sense, unit->sense, len);
		unit->sense[2] = 0;
		unit->sense[12] = 0;
		unit->sense[13] = 0;
		r->flags |= SDvalidsense;
		return SDok;
	}
	return status;
}

int
sdfakescsi(SDreq *r)
{
	uchar *cmd, *p;
	uvlong len;
	SDunit *unit;

	cmd = r->cmd;
	r->rlen = 0;
	unit = r->unit;

	/*
	 * Map SCSI commands into ATA commands for discs.
	 * Fail any command with a LUN except INQUIRY which
	 * will return 'logical unit not supported'.
	 */
	if((cmd[1]>>5) && cmd[0] != 0x12)
		return sdsetsense(r, SDcheck, 0x05, 0x25, 0);

	switch(cmd[0]){
	default:
		return sdsetsense(r, SDcheck, 0x05, 0x20, 0);

	case 0x00:	/* test unit ready */
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x03:	/* request sense */
		if(cmd[4] < sizeof unit->sense)
			len = cmd[4];
		else
			len = sizeof unit->sense;
		if(r->data && r->dlen >= len){
			memmove(r->data, unit->sense, len);
			r->rlen = len;
		}
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x12:	/* inquiry */
		if(cmd[4] < sizeof unit->inquiry)
			len = cmd[4];
		else
			len = sizeof unit->inquiry;
		if(r->data && r->dlen >= len){
			memmove(r->data, unit->inquiry, len);
			r->rlen = len;
		}
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x1B:	/* start/stop unit */
		/*
		 * nop for now, can use power management later.
		 */
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x25:	/* read capacity */
		if((cmd[1] & 0x01) || cmd[2] || cmd[3])
			return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
		if(r->data == nil || r->dlen < 8)
			return sdsetsense(r, SDcheck, 0x05, 0x20, 1);

		/*
		 * Read capacity returns the LBA of the last sector.
		 */
		len = unit->sectors;
		if(len >= 0xffffffff)
			len = 0xffffffff;
		else if(len > 0)
			len--;
		p = r->data;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		len = unit->secsize;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		r->rlen = p - (uchar*)r->data;
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x9E:	/* long read capacity */
		if((cmd[1] & 0x01) || cmd[2] || cmd[3])
			return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
		if(r->data == nil || r->dlen < 8)
			return sdsetsense(r, SDcheck, 0x05, 0x20, 1);
		/*
		 * Read capcity returns the LBA of the last sector.
		 */
		len = unit->sectors;
		if(len > 0)
			len--;
		p = r->data;
		*p++ = len>>56;
		*p++ = len>>48;
		*p++ = len>>40;
		*p++ = len>>32;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		len = unit->secsize;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		r->rlen = p - (uchar*)r->data;
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x35:	/* synchronize cache */
	case 0x91:
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x08:	/* read6 */
	case 0x0a:	/* write6 */
	case 0x28:	/* read10 */
	case 0x2a:	/* write10 */
	case 0xa8:	/* read12 */
	case 0xaa:	/* write12 */
	case 0x88:	/* read16 */
	case 0x8a:	/* write16 */
		return SDnostatus;
	}
}

int
sdfakescsirw(SDreq *r, uvlong *llba, int *nsec, int *rwp)
{
	uchar *c;
	int rw, count;
	uvlong lba;

	c = r->cmd;
	rw = SDread;
	if((c[0] & 0xf) == 0xa)
		rw = SDwrite;
	switch(c[0]){
	case 0x08:	/* read6 */
	case 0x0a:
		lba = (c[1] & 0xf)<<16 | c[2]<<8 | c[3];
		count = c[4];
		break;
	case 0x28:	/* read10 */
	case 0x2a:
		lba = c[2]<<24 | c[3]<<16 | c[4]<<8 | c[5];
		count = c[7]<<8 | c[8];
		break;
	case 0xa8:	/* read12 */
	case 0xaa:
		lba = c[2]<<24 | c[3]<<16 | c[4]<<8 | c[5];
		count = c[6]<<24 | c[7]<<16 | c[8]<<8 | c[9];
		break;
	case 0x88:	/* read16 */
	case 0x8a:
		/* ata commands only go to 48-bit lba */
		if(c[2] || c[3])
			return sdsetsense(r, SDcheck, 3, 0xc, 2);
		lba = (uvlong)c[4]<<40 | (uvlong)c[5]<<32;
		lba |= c[6]<<24 | c[7]<<16 | c[8]<<8 | c[9];
		count = c[10]<<24 | c[11]<<16 | c[12]<<8 | c[13];
		break;
	default:
		print("%s: bad cmd 0x%.2ux\n", r->unit->perm.name, c[0]);
		r->status  = sdsetsense(r, SDcheck, 0x05, 0x20, 0);
		return SDcheck;
	}
	if(r->data == nil)
		return SDok;
	if(r->dlen < count * r->unit->secsize)
		count = r->dlen/r->unit->secsize;
	if(rwp)
		*rwp = rw;
	*llba = lba;
	*nsec = count;
	return SDnostatus;
}
