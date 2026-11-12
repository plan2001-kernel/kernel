#ifndef PLAN2001_CORE_VOBJINT_H
#define PLAN2001_CORE_VOBJINT_H

#include "../../runtime/abi/vobj.h"
#include "../../runtime/abi/obj.h"

typedef struct Ohand Ohand;
typedef struct Vhand Vhand;
typedef struct Procstat Procstat;

/* Shared, bounded object/ABI limits. */
enum {
	Vobjpaymax	= 1*1024*1024,
	Vsubcapmax	= 1024,
	/* One table covers volatile objects, GE/O and devices.  The low
	 * Handle byte still addresses 255 slots; keep the table small enough
	 * for the kernel stack arrays in vobjlist/vobjprocexit. */
	Vhandmax	= 96,
};

typedef struct Vobj Vobj;
typedef struct Veth Veth;

struct Vobj {
	Ref	ref;
	uvlong	incarnation;
	uvlong	space;
	uvlong	oid;
	uvlong	owner;		/* immutable OwnerID of the object */
	int	kind;
	int	flags;
	int	provider;
	ulong	size;
	uchar	*data;		/* immutable snapshot, may be nil */
	void	*priv;		/* provider-private state */
};

/* Core operations exported to concrete providers. */
Vobj*	vobjnewkind(int kind, ulong size);
void	vobjput(Vobj*);
void	vobjsetpriv(Vobj*, void*);
int	vobjinstallobj(Vobj*, int rights);
Vobj*	vobjholdobj(int handle, int rights);
int	vobjhandlesbykind(Proc*, int kind);
int	vobjclosehandle(int handle);
int	vobjkindof(Vobj*);
ulong	vobjsize(Vobj*);

/*
 * Shared core used by the legacy VOBJ* shims and the universal
 * CREATE/OPEN/CLOSE/BIND/INVOKE family.  Each helper takes explicit
 * arguments; it validates rights and returns a Handle or a provider result.
 * Errors are raised through the normal kernel error mechanism.
 */
int	vobjcreat(int kind, uvlong space, void *buf, long n, VobjId *id);
int	vobjcreatege(int volumehandle, void *buf, long n, uvlong *oid);
int	vobjidentity(int handle, VobjId *id);
int	vobjcommit(int handle, uvlong *ver);
int	vobjputoid(int handle, uvlong oid, void *buf, long n);
int	vobjconsoleopen(void *req, long len, uvlong rights, VobjId *id);
/* ObjectSpace graph is independent of Pgrp, GE SpaceID, and process ancestry. */
int	vobjspacecreate(VobjId *id);
void	vobjspacesave(int spacehandle, ObjSpaceDiskReq*, uvlong *version);
int	vobjspaceload(ObjSpaceDiskReq*, VobjId *id);
uvlong	vobjspacebind(int spacehandle, ObjSpaceBindReq *req);
void	vobjspaceunbind(int spacehandle, uvlong bindingid);
int	vobjspaceresolve(int spacehandle, uvlong bindingid, uvlong rights);
int	vobjspaceenumerate(int spacehandle, uvlong cursor, uvlong generation,
	ObjSpaceEntry *out, int max, uvlong *epoch);
void	osrevokehand(Ohand*);

/*
 * One explicit process-local Handle delegation request.  Both RFORKEX and
 * OSPAWN translate their public option records into this kernel form and use
 * the same validation/commit code.  dstslot is 0 (allocate the lowest free
 * child slot) or a 1-based child slot.
 */
typedef struct Vinhand {
	int	handle;
	uvlong	rights;
	int	dstslot;
} Vinhand;

/*
 * A fully validated and pinned inheritance plan.  prepare() looks up every
 * source Handle and verifies rights, kind, generation and slot conflicts
 * before a child is created; commit() installs the copies; release() drops
 * the pinned source references.  The plan never mutates the parent.
 */
typedef struct Vinherit Vinherit;
struct Vinherit {
	int	n;
	struct {
		Vhand	*src;
		int	srchandle;
		int	rights;
		int	slot;
		ulong	gen;
	} e[Vhandmax];
};

int	vobjinheritprepare(Vinherit*, Vinhand*, int);
void	vobjinheritcommit(Proc*, Vinherit*);
int	vobjinheritchildhandle(Vinherit*, int);
int	vobjinheritsrchandle(Vinherit*, int);
void	vobjinheritrelease(Vinherit*);

/* Process-lifecycle providers.  SPAWN and RFORKEX resolve an explicit
 * Space/Handle plan and return a VkindProcess capability.  Observation,
 * termination and pexit completion all use the one Procstat resource. */
ulong	kernrforkex(ulong flag, Vinhand*, int, uvlong*);
long	kernospawn(Ohand *soh, uvlong oid, char **uargv, int argc,
		struct Ospawn *uopt, uvlong owner, int internal);
int	vobjprocinstall(Procstat*);
int	vobjprocinfo(int, ObjProcessInfo*);
long	vobjprocesswait(int, ObjProcessInfo*, long);
int	vobjprocterminate(int);

int	vobjopenstream(uvlong provider, void *req, long len, uvlong rights,
		VobjId *id);
int	vobjopenhandle(int src, uvlong rights, VobjId *id);
int	vobjinfo(int handle, VobjInfo *out);
int	vobjownerinfo(int handle, ObjOwnerInfo *out);
int	vobjrevoke(int handle);
int	vobjcaprevoke(int handle);
long	vobjio(int handle, uvlong op, void *buf, long n, long timeout);
long	vobjreadver(int handle, void *buf, long n, long timeout, uvlong *ver);
int	vobjlist(void *buf, long n);

/*
 * Native UART and KBD providers and capability regions.  They live in the
 * same Vobj/Vhand core as bytes and GE and are reached only through the OBJ*
 * five-call ABI; sysuart.c and syskbd.c keep only the exec/exit hooks.  All
 * errors use the normal kernel error mechanism.
 */
int	vobjregioncreate(uvlong size, VobjId *id);
Vobj*	vobjregionalloc(uvlong size);
uintptr	vobjregionmap(int handle, uintptr va, int flags);
int	vobjregionunmap(uintptr va);
int	vobjregionwriterhold(Vobj *o);
int	vobjregionringinit(Vobj *o);
void	vobjregionwriterrelease(Vobj *o);
int	vobjregionringpublish(Vobj *o, uvlong *seq, void *data, ulong len);
uvlong	vobjregionringload(Vobj *o, uvlong off);
int	vobjregionringconsume(Vobj *o, uvlong seq, void *buf, ulong max,
		ulong *len);
int	vobjregionringack(Vobj *o, uvlong seq);
int	vobjregionwaitkern(Vobj *o, uvlong off, uvlong expected, int timeout);

int	vobjuartctl(int handle, void *cfg);
long	vobjuartstatus(int handle, void *buf, long n);

int	vobjkbdleds(int handle, int leds);

/* Common device ABI.  UART, KBD, Ethernet and USB are discovered and opened
 * only through this catalog; provider-specific control/I/O stays distinct. */
int vobjdevicesopen(uvlong rights, VobjId *id);
long vobjdevicelist(int handle, void *out, long bytes);
int vobjdeviceopen(void *req, long len, uvlong rights, VobjId *id);
long vobjdevicesend(int handle, void *in, long len);
int vobjdevicerecv(int handle, void *out);
int vobjdevicerx(int handle);
int vobjdevicetx(int handle);
long vobjioctl(int handle, uvlong op, void *data, long len, uvlong arg0);
int vetherlist(ObjDeviceEntry *out, int capacity);
Veth *vetheropen(int controller);
void vetherclose(Veth*);
void vetherdetach(Veth*);
Vobj *vetherrxregion(Veth*);
Vobj *vethertxregion(Veth*);
void vetherstatus(Veth*, ObjEtherStatus*);

/* CLOSE only when the Handle still names the expected provider kind. */
int	vobjclosekind(int handle, int kind);

/* SYSCTL provider helpers. */
long	sysctlsnapshot(void *user, uvlong n);
long	sysctlenvget(char *name, int namelen, void *buf, long n, long *outlen);
long	sysctlenvset(char *name, int namelen, void *val, long n);
long	sysctlenvdelete(char *name, int namelen);
long	sysctlenvlist(void *buf, long n, long *need);
long	osvolumeopen(char *spec, int n);

/* GE/O provider helpers implemented in sysgefs.c. */
int	vobjrightsfromo(int rights);
int	vobjinstallohand(Ohand*, int kind);
Ohand*	vobjholdohand(int handle, int kind, int rights);
Ohand*	osopenvolume(void);
void	oshandput(Ohand*);
long	osreadhand(Ohand*, uvlong oid, void *buf, long n, uvlong *ver);
long	osgetobjecthand(Ohand*, uvlong oid, uchar **bp, long max, uvlong *ver);
long	osgetobjecthandk(Ohand*, uvlong oid, uchar **bp, long max, uvlong *ver);
void	oswritehand(Ohand*, uvlong oid, void *buf, long n);
long	osreadhandk(Ohand*, uvlong oid, void *buf, long n, uvlong *ver);
void	oswritehandk(Ohand*, uvlong oid, void *buf, long n);
long	oscreatehand(Ohand*, void *buf, long n, uvlong *oid);
long	oscreatehandowner(Ohand*, void *buf, long n, uvlong *oid, uvlong owner);
void	osownercheck(Ohand*, uvlong oid);
uvlong	osownerof(Ohand*, uvlong oid);
int	osownerfault(int point);
long	oscommithand(Ohand*, uvlong *ver);
Ohand*	oscopycap(Ohand*, uvlong oid);
Ohand*	oscopycapdelegate(Ohand*, int rights);
void	osvolumeid(Ohand*, uchar *out);
int	oshandrights(Ohand*);
void	oscheckhand(Ohand*);

#endif
