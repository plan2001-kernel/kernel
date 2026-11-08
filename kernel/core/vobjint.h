#ifndef PLAN2001_CORE_VOBJINT_H
#define PLAN2001_CORE_VOBJINT_H

#include "../../runtime/abi/vobj.h"
#include "../../runtime/abi/obj.h"

typedef struct Ohand Ohand;

/* Shared, bounded object/ABI limits. */
enum {
	Vobjpaymax	= 1*1024*1024,
	Vsubcapmax	= 1024,
	/* One table now covers bytes/FIFO/TCP/GE/UDP6 plus Pipe, UART and KBD.
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
int	vobjprocessselfopen(uvlong rights, VobjId *id);

/* Process-lifecycle providers.  The OBJINVOKE operations validate the
 * caller's own ProcessSelf or Space capability, then call the shared kernel
 * cores that the retired direct syscalls used.  Keeping one core preserves
 * forkchild, OSPAWN layout, parent/child checks and pexit semantics. */
ulong	kernrfork(ulong flag);
long	kernospawn(Ohand *soh, uvlong oid, char **uargv, int argc,
		struct Ospawn *uopt);
int	kernoterminate(int pid);
long	vobjospawn(int handle, void *req);
long	vobjwait(int handle, ObjWait *out);

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
long	vobjio(int handle, uvlong op, void *buf, long n, long timeout);
long	vobjreadver(int handle, void *buf, long n, long timeout, uvlong *ver);
int	vobjlist(void *buf, long n);
int	vobjaccept(int handle, int timeout);

/* UDP6 provider implemented in sysnet6.c over nativeudp6.c. */
long	vudp6addr(void *addr, long len);
int	vobjopenudp6(void *req, long len, uvlong rights, VobjId *id);
long	vobjudp6send(int handle, void *req, long len);
long	vobjudp6recv(int handle, void *out, long outlen, uvlong timeout);
void	vudp6_release(Vobj*);

/*
 * Native Pipe, UART and KBD providers.  They live in the same Vobj/Vhand
 * core as bytes, FIFO, TCP, GE and UDP6 and are reached only through the
 * OBJ* five-call ABI; syspipe.c, sysuart.c and syskbd.c keep only the
 * RFORK/exec/exit, OSPAWN and bootstrap-output hooks.  All errors use the
 * normal kernel error mechanism.
 */
int	vobjpipecreate(VobjId *id);
int	vobjpipepeer(int handle);
int	vobjpipeinherit(int handle, int rights);
void	vobjpipeprocfork(Proc *child);
void	vobjpipespawn(Proc *child, int (*entries)[2], int n);
void	vobjpipeoutputset(Proc *p, int handle);
long	vobjpipeoutputwrite(int handle, void *data, long n);
int	vobjpipekernelinstall(Proc *proc, void *pipe, int end);

int	vobjuartopenrights(int port, int mode, uvlong rights, VobjId *id);
int	vobjuartctl(int handle, void *cfg);
long	vobjuartstatus(int handle, void *buf, long n);

int	vobjkbdopen(VobjId *id);
int	vobjkbdleds(int handle, int leds);

/* USB service provider over the existing usbcore endpoint table. */
int	vobjusbopen(uvlong rights, VobjId *id);
long	vobjusblist(int handle, void *ents, long nbytes);
int	vobjusbepopen(int handle, void *req);

/* Common message-device ABI; T12 USB control integration. */
int vobjdevicesopen(uvlong rights, VobjId *id);
long vobjdevicelist(int handle, void *out, long bytes);
int vobjdeviceopen(void *req, long len, uvlong rights, VobjId *id);
long vobjdevicesend(int handle, void *in, long len);
int vobjdevicerecv(int handle, void *out);
int vobjdevicectl(int handle, uvlong op, void *out, long len);
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

/* TCP6 provider implemented in systcp6.c. */
Vobj*	vtcp_open(int listener, uchar *addr, int port);
int	vtcp_accept(Vobj*, int timeout, Vobj**);
long	vtcp_read(Vobj*, void*, long, long);
long	vtcp_write(Vobj*, void*, long);
void	vtcp_release(Vobj*);

/* GE/O provider helpers implemented in sysgefs.c. */
Ohand*	osopenh(int handle);
Ohand*	osopenspace(char *name, int n);
void	oshandput(Ohand*);
long	osreadhand(Ohand*, uvlong oid, void *buf, long n, uvlong *ver);
long	osgetobjecthand(Ohand*, uvlong oid, uchar **bp, long max, uvlong *ver);
void	oswritehand(Ohand*, uvlong oid, void *buf, long n);
long	oscreatehand(Ohand*, void *buf, long n, uvlong *oid);
long	oscommithand(Ohand*, uvlong *ver);
Ohand*	oscopycap(Ohand*, uvlong oid);
uvlong	ossid(Ohand*);
void	osvolumeid(Ohand*, uchar *out);
int	oshandrights(Ohand*);
void	oscheckhand(Ohand*);

#endif
