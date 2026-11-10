#ifndef PLAN2001_CORE_VOBJINT_H
#define PLAN2001_CORE_VOBJINT_H

#include "../../runtime/abi/vobj.h"
#include "../../runtime/abi/obj.h"

typedef struct Ohand Ohand;
typedef struct Vhand Vhand;

/* Shared, bounded object/ABI limits. */
enum {
	Vobjpaymax	= 1*1024*1024,
	Vsubcapmax	= 1024,
	/* One table covers bytes/FIFO/GE plus Pipe, UART, KBD and devices.
	 * The low Handle byte still addresses 255 slots; keep the table small
	 * enough for the kernel stack arrays in vobjlist/vobjprocexit. */
	Vhandmax	= 96,
};

typedef struct Vobj Vobj;
typedef struct Veth Veth;

struct Vobj {
	Ref	ref;
	uvlong	incarnation;
	uvlong	space;
	uvlong	oid;
	int	kind;
	int	flags;
	int	provider;
	ulong	size;
	uchar	*data;		/* immutable snapshot, may be nil */
	void	*priv;		/* provider-private state */
	void	*fifo;		/* VkindFifo state, owned by sysvobj.c */
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
int	vobjcreatege(int spacehandle, void *buf, long n, uvlong *oid,
		uvlong *spaceid);
int	vobjidentity(int handle, VobjId *id);
int	vobjcommit(int handle, uvlong *ver);
int	vobjputoid(int handle, uvlong oid, void *buf, long n);
int	vobjconsoleopen(void *req, long len, uvlong rights, VobjId *id);
int	vobjnamespaceopen(uvlong rights, VobjId *id);
int	vobjnamespacecreate(int control, char (*roles)[32], int nr);
uvlong	vobjnamespaceattach(int control, int view, int mode);
void	vobjnamespacedetach(int control, uvlong token);
int	vobjnamespacevisible(int control, void *out, long bytes);
void	oscheckrole(void *context, char *name, int geonly);
void	osrevokehand(Ohand*);
void	osviewgrantvalid(Ohand*);
int	osvisibility(Ohand **grants, int count, int *allowed);

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

/* Process-lifecycle providers.  SPAWN resolves an explicit Space Handle to
 * the shared spawn core; the direct OEXECUTE/OTERMINATE/PROCWAIT/EXITS calls
 * use the same kernel semantics.  Keeping one core preserves forkchild,
 * spawn layout, parent/child checks and pexit semantics. */
ulong	kernrforkex(ulong flag, Vinhand*, int, uvlong*);
long	kernospawn(Ohand *soh, uvlong oid, char **uargv, int argc,
		struct Ospawn *uopt);
int	kernoterminate(int pid);
long	vobjprocwait(ProcWait *out);

int	vobjopenstream(uvlong provider, void *req, long len, uvlong rights,
		VobjId *id);
int	vobjopenhandle(int src, uvlong rights, VobjId *id);
int	vobjopenbinding(char *role, int rolelen, uvlong rights, VobjId *id);
int	vobjbind(int handle, char *role, int rolelen, uvlong rights, uint mode);
int	vobjinfo(int handle, VobjInfo *out);
int	vobjfifostat(int handle, VobjFifostat *out);
int	vobjfifosub(int fifoh, uvlong rights, long capacity);
int	vobjfifosend(int fifoh, int objh, void *env, long envlen);
int	vobjfiforecv(int subh, void *env, long envlen, int timeout, long *got);
int	vobjrevoke(int handle);
int	vobjfifoshutdown(int handle);
long	vobjio(int handle, uvlong op, void *buf, long n, long timeout);
long	vobjreadver(int handle, void *buf, long n, long timeout, uvlong *ver);
int	vobjlist(void *buf, long n);

/*
 * Native Pipe, UART and KBD providers.  They live in the same Vobj/Vhand
 * core as bytes, FIFO and GE and are reached only through the OBJ* five-call
 * ABI; syspipe.c, sysuart.c and syskbd.c keep only the RFORK/exec/exit,
 * OSPAWN and bootstrap-output hooks.  All errors use the normal kernel error
 * mechanism.
 */
int	vobjpipecreate(VobjId *id);
int	vobjpipepeer(int handle);
int	vobjregioncreate(uvlong size, VobjId *id);
uintptr	vobjregionmap(int handle, uintptr va);
int	vobjregionunmap(uintptr va);
void	vobjpipeoutputset(Proc *p, int handle);
long	vobjpipeoutputwrite(int handle, void *data, long n);
int	vobjpipekernelinstall(Proc *proc, void *pipe, int end);

int	vobjuartctl(int handle, void *cfg);
long	vobjuartstatus(int handle, void *buf, long n);

int	vobjkbdleds(int handle, int leds);

/* Common device ABI.  UART, KBD, Ethernet and USB are discovered and opened
 * only through this catalog; provider-specific control/I/O stays distinct. */
int vobjdevicesopen(uvlong rights, VobjId *id);
long vobjdevicelist(int handle, void *out, long bytes);
int vobjdeviceopen(void *req, long len, uvlong rights, VobjId *id);
long vobjdevicesend(int handle, void *in, long len, unsigned int *kind);
int vobjdevicerecv(int handle, void *out);
int vobjdevicerx(int handle);
long vobjioctl(int handle, uvlong op, void *data, long len, uvlong arg0);
/* Kernel-owned references and envelope, process context only.  This is the
 * same ObjectFIFO publisher used by user Handle sends, without ambient
 * process Handle lookup.  Caller pins fifo/object throughout the call. */
int vobjfifopublish(Vobj *fifo, Vobj *object, int rights, void *env, long len);
void vobjfifoclose(Vobj *fifo);
int vetherlist(ObjDeviceEntry *out, int capacity);
Veth *vetheropen(int controller);
void vetherclose(Veth*);
void vetherdetach(Veth*);
Vobj *vetherfifo(Veth*);
long vethersend(Veth*, void*, long);
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
Ohand*	osopenspace(char *name, int n);
void	oshandput(Ohand*);
long	osreadhand(Ohand*, uvlong oid, void *buf, long n, uvlong *ver);
long	osgetobjecthand(Ohand*, uvlong oid, uchar **bp, long max, uvlong *ver);
void	oswritehand(Ohand*, uvlong oid, void *buf, long n);
long	oscreatehand(Ohand*, void *buf, long n, uvlong *oid);
long	oscommithand(Ohand*, uvlong *ver);
Ohand*	oscopycap(Ohand*, uvlong oid);
Ohand*	oscopycapdelegate(Ohand*, int rights);
uvlong	ossid(Ohand*);
void	osvolumeid(Ohand*, uchar *out);
int	oshandrights(Ohand*);
void	oscheckhand(Ohand*);

#endif
