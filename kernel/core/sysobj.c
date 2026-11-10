/*
 * Universal five-operation object ABI: CREATE, OPEN, CLOSE, BIND, INVOKE.
 *
 * These five entry points are the kernel boundary.  READ/WRITE and
 * PUBLISH/SUBSCRIBE/RECEIVE are userland adapters over INVOKE; the kernel
 * does not grow a syscall family per provider.  INVOKE is a request router:
 * it validates the authorized Handle, the operation and each buffer, then
 * calls the existing provider implementation in sysvobj.c.  It is not an
 * object class hierarchy and contains no network stack.
 *
 * Historical VOBJ* syscall numbers are reserved and have no handlers.  The
 * TCP6/UDP6 object providers are gone: the permitted network boundary is raw
 * device frames/ObjectFIFO plus userspace protocol handling.  Process handle
 * introspection is SYSCTL SysctlHandleList; child wait is the direct typed
 * PROCWAIT call; device control is IOCTL.  None is a synthetic object
 * operation.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"../drivers/uart/uartnative.h"
#include	"../../runtime/abi/sys.h"
#include	"../../runtime/abi/vobj.h"
#include	"../../runtime/abi/obj.h"
#include	"../../runtime/abi/namespace.h"
#include	"../../runtime/abi/ioctl.h"
#include	"../core/vobjint.h"

static long
bounded(uvlong v, long max, char *what)
{
	if(v > (uvlong)max)
		error(what);
	return (long)v;
}

uintptr
sysobjcreate(sysva_list list)
{
	uvlong space, flags;
	void *buf;
	long n;
	ObjId *id;
	int kind;

	space = sysva_arg(list, uvlong);
	flags = sysva_arg(list, uvlong);
	buf = sysva_arg(list, void*);
	n = sysva_arg(list, long);
	id = sysva_arg(list, ObjId*);
	if(flags & ~(uvlong)ObjCreateKindMask)
		error(Ebadarg);
	kind = (int)(flags & ObjCreateKindMask);
 if(id != nil) validaddr((uintptr)id, sizeof *id, 1);
	if(kind == ObjKindPipe){
		if(buf != nil || n != 0)
			error(Ebadarg);
		return vobjpipecreate((VobjId*)id);
	}
	if(kind == ObjKindRegion){
		VobjId vid;
		int h;

		if(space != 0 || buf != nil)
			error(Ebadarg);
		if(n <= 0)
			error(Ebadarg);
		h = vobjregioncreate((uvlong)n, &vid);
		if(id != nil){
			validaddr((uintptr)id, sizeof(ObjId), 1);
			((ObjId*)id)->incarnation = vid.incarnation;
			((ObjId*)id)->space = vid.space;
			((ObjId*)id)->oid = vid.oid;
		}
		return h;
	}
	if(kind == ObjKindGe){
  if(space > 0x7fffffffULL) error(Ebadarg);
		VobjId vid;
		int h;

		h = vobjcreatege((int)space, buf, n, nil, nil);
		if(id != nil){
			validaddr((uintptr)id, sizeof(ObjId), 1);
			vobjidentity(h, &vid);
			((ObjId*)id)->incarnation = vid.incarnation;
			((ObjId*)id)->space = vid.space;
			((ObjId*)id)->oid = vid.oid;
		}
		return h;
	}
	if(kind != ObjKindBytes && kind != ObjKindFifo)
		error(Ebadarg);
	return vobjcreat(kind == ObjKindFifo ? VkindFifo : VkindBytes,
		space, buf, n, (VobjId*)id);
}

uintptr
sysobjopen(sysva_list list)
{
	uvlong provider, rights;
	void *req;
	long len;
	ObjId *id;
	ObjOpenHandleReq hr;

	provider = sysva_arg(list, uvlong);
	req = sysva_arg(list, void*);
	len = sysva_arg(list, long);
	rights = sysva_arg(list, uvlong);
	id = sysva_arg(list, ObjId*);
	if(rights & ~(uvlong)Vall)
		error(Ebadarg);
	switch(provider){
	case ObjOpenHandle:
		if(req == nil || len != sizeof hr)
			error(Ebadarg);
		validaddr((uintptr)req, sizeof hr, 0);
		memmove(&hr, req, sizeof hr);
		if(hr.reserved != 0)
			error(Ebadarg);
		return vobjopenhandle(hr.handle, rights, (VobjId*)id);
	case ObjOpenBinding:
		if(req == nil || len <= 0 || len >= 32)
			error(Ebadarg);
		return vobjopenbinding((char*)req, (int)len, rights, (VobjId*)id);
	case ObjOpenSpace:
		if(req == nil || len <= 0)
			error(Ebadarg);
		return vobjopenstream(VopenSpace, req, len, rights, (VobjId*)id);
	case ObjOpenGeSpace:
		/* The on-wire request is layout-compatible with VobjGeReq, so the
		 * provider can validate and copy the user pointer itself. */
		if(req == nil || len != sizeof(ObjOpenGeSpaceReq))
			error(Ebadarg);
		return vobjopenstream(VopenGeSpace, req, (long)sizeof(VobjGeReq),
			rights, (VobjId*)id);
	case ObjOpenConsole:
		if(req == nil || len != sizeof(ObjOpenConsoleReq))
			error(Ebadarg);
		return vobjconsoleopen(req, len, rights, (VobjId*)id);
	case ObjOpenDevices:
        if(req != nil || len != 0)
            error(Ebadarg);
        return vobjdevicesopen(rights, (VobjId*)id);
    case ObjOpenDevice:
        return vobjdeviceopen(req, len, rights, (VobjId*)id);
	case ObjOpenNamespaceCurrent:
		if(req != nil || len != 0)
			error(Ebadarg);
		return vobjnamespaceopen(rights, (VobjId*)id);
	default:
		error("obj: provider not supported");
	}
	return -1;
}

uintptr
sysobjclose(sysva_list list)
{
	return vobjclosehandle(sysva_arg(list, int));
}

uintptr
sysobjbind(sysva_list list)
{
	int handle;
	char *role;
	long rolelen;
	uvlong rights, mode;

	handle = sysva_arg(list, int);
	role = sysva_arg(list, char*);
	rolelen = sysva_arg(list, long);
	rights = sysva_arg(list, uvlong);
	mode = sysva_arg(list, uvlong);
	if(rolelen <= 0 || rolelen >= 32 || mode > 0x1ff) error(Ebadarg);
	return vobjbind(handle, role, (int)rolelen, rights, (uint)mode);
}

/* Direct typed child wait.  The caller waits on its own children; no
 * synthetic ProcessSelf Handle is created or delegated. */
uintptr
sysprocwait(sysva_list list)
{
	ProcWait *w;

	w = sysva_arg(list, ProcWait*);
	if(w == nil)
		error(Ebadarg);
	validaddr((uintptr)w, sizeof *w, 1);
	return vobjprocwait(w);
}

/* IOCTL(145): device configuration, control and device-specific status.
 * Authority is the device Handle and its rights per operation.  Object
 * payload stays in OBJINVOKE. */
uintptr
sysioctl(sysva_list list)
{
	IoctlReq r;
	IoctlReq *ur;
	unsigned int op;
	long size;
	int handle;

	handle = sysva_arg(list, int);
	op = (unsigned int)sysva_arg(list, uvlong);
	ur = sysva_arg(list, IoctlReq*);
	size = sysva_arg(list, long);
	if(ur == nil || size != sizeof r)
		error(Ebadarg);
	validaddr((uintptr)ur, sizeof r, 1);
	memmove(&r, ur, sizeof r);
	if(r.version != IOCTL_ABI_VERSION || r.flags != 0 ||
	   r.reserved != 0 || r.reserved2 != 0)
		error(Ebadarg);
	r.result = vobjioctl(handle, op, r.data, (long)r.datalen, r.arg0);
	memmove(ur, &r, sizeof r);
	return (uintptr)r.result;
}

uintptr
sysobjinvoke(sysva_list list)
{
	int handle;
	ObjCall *ucall;
	long calllen;
	ObjCall c;
	long got;
	int tmo;

	handle = sysva_arg(list, int);
	ucall = sysva_arg(list, ObjCall*);
	calllen = sysva_arg(list, long);
	if(ucall == nil || calllen != sizeof c)
		error(Ebadarg);
	validaddr((uintptr)ucall, sizeof c, 1);
	memmove(&c, ucall, sizeof c);
	if(c.version != OBJ_ABI_VERSION)
		error("obj: unsupported ABI version");
	if(c.flags != 0 || c.rights != 0 || c.reserved != 0)
		error(Ebadarg);
	c.status = 0;
	c.aux = 0;
#ifdef PLAN2001_SYSCALL_TRACE
	objoptrace(c.op);
#endif
	switch(c.op){
	case ObjOpNamespaceCreate:
		if(c.in == nil || c.inlen == 0 || c.inlen > 32*32 ||
		   c.inlen % 32 != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		c.result = vobjnamespacecreate(handle, c.in, (int)(c.inlen/32));
		break;
	case ObjOpNamespaceAttach:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg0 > 0x7fffffffULL || c.arg1 > 2)
			error(Ebadarg);
		c.result = vobjnamespaceattach(handle, (int)c.arg0, (int)c.arg1);
		break;
	case ObjOpNamespaceDetach:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg1 != 0)
			error(Ebadarg);
		vobjnamespacedetach(handle, c.arg0);
		c.result = 0;
		break;
	case ObjOpNamespaceVisible:
		if(c.in != nil || c.inlen != 0 || c.arg0 != 0 || c.arg1 != 0 ||
		   (c.out == nil && c.outlen != 0) ||
		   c.outlen > 32*sizeof(NsVisible) ||
		   c.outlen % sizeof(NsVisible) != 0)
			error(Ebadarg);
		c.result = vobjnamespacevisible(handle, c.out, (long)c.outlen);
		break;
	case ObjOpDeviceList:
        if(c.in != nil || c.inlen != 0 || c.arg0 != 0 || c.arg1 != 0
        || (c.out == nil && c.outlen != 0)
        || c.outlen % sizeof(ObjDeviceEntry) != 0)
            error(Ebadarg);
        c.result = vobjdevicelist(handle, c.out,
            bounded(c.outlen, OBJ_DEVICE_LISTMAX*sizeof(ObjDeviceEntry), Ebadarg));
        break;
    case ObjOpDeviceSend:
    {
        unsigned int kind;

        if(c.out != nil || c.outlen != 0 || c.arg0 != 0 || c.arg1 != 0)
            error(Ebadarg);
        kind = 0;
        c.result = vobjdevicesend(handle, c.in,
            bounded(c.inlen, OBJ_DEVICE_MAXMESSAGE, Ebadarg), &kind);
        /* Accepted L2 bytes and a USB completion sequence are different
         * typed results even though both fit in the I/O return slot. */
        c.aux = kind;
        break;
    }
    case ObjOpDeviceRecv:
        if(c.in != nil || c.inlen != 0 || c.arg0 != 0 || c.arg1 != 0
        || c.out == nil || c.outlen != sizeof(ObjDeviceResult))
            error(Ebadarg);
        c.result = vobjdevicerecv(handle, c.out);
        break;
    case ObjOpDeviceRx:
        if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
           c.arg0 != 0 || c.arg1 != 0)
            error(Ebadarg);
        c.result = vobjdevicerx(handle);
        break;
    case ObjOpFifoShutdown:
        if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
           c.arg0 != 0 || c.arg1 != 0)
            error(Ebadarg);
        c.result = vobjfifoshutdown(handle);
        break;
    case ObjOpInfo:
		if(c.out == nil || c.outlen != sizeof(VobjInfo))
			error(Ebadarg);
		vobjinfo(handle, (VobjInfo*)c.out);
		c.result = 0;
		break;
	case ObjOpStat:
		if(c.out == nil || c.outlen != sizeof(VobjFifostat))
			error(Ebadarg);
		vobjfifostat(handle, (VobjFifostat*)c.out);
		c.result = 0;
		break;
	case ObjOpRead:
	{
		uvlong ver;

		/* READ/WRITE have no provider timeout.  Zero keeps the
		 * provider's existing blocking behavior; a finite timeout is
		 * rejected explicitly rather than silently ignored. */
		if(c.arg0 != 0)
			error("obj: read timeout not supported");
		ver = 0;
		c.result = vobjreadver(handle, c.out,
			bounded(c.outlen, Vobjpaymax, Ebadarg), 0, &ver);
		/* A GE read publishes the version it returned; every other
		 * provider leaves aux at its previous zero value. */
		if(c.result >= 0)
			c.aux = ver;
		break;
	}
	case ObjOpWrite:
		if(c.arg0 != 0)
			error("obj: write timeout not supported");
		c.result = vobjio(handle, VioWrite, c.in,
			bounded(c.inlen, Vobjpaymax, Ebadarg), 0);
		break;
	case ObjOpSend:
  if(c.arg0 > 0x7fffffffULL) error(Ebadarg);
		c.result = vobjfifosend(handle, (int)c.arg0, c.in,
			bounded(c.inlen, 0x7fffffff, Ebadarg));
		break;
	case ObjOpRecv:
		/* The historical FIFO timeout is <0 for an infinite wait; the
		 * OBJ adapter passes it as an unsigned -1.  Accept both signed
		 * and 32-bit unsigned infinite sentinels and keep finite waits
		 * bounded to the int-millisecond range. */
		if(c.arg0 == 0xffffffffffffffffULL || c.arg0 == 0xffffffffULL)
			tmo = -1;
		else
			tmo = (int)bounded(c.arg0, 0x7fffffff, Ebadarg);
		got = 0;
		c.result = vobjfiforecv(handle, c.out,
			bounded(c.outlen, 0x7fffffff, Ebadarg), tmo, &got);
		c.aux = (uvlong)got;
		break;
	case ObjOpSubscribe:
		c.result = vobjfifosub(handle, c.arg0,
			bounded(c.arg1, Vsubcapmax, Ebadarg));
		break;
	case ObjOpRevoke:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		vobjrevoke(handle);
		c.result = 0;
		break;
	case ObjOpPipePeer:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0)
			error(Ebadarg);
		c.result = vobjpipepeer(handle);
		break;
	case ObjOpRegionMap:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg1 != 0)
			error(Ebadarg);
		c.result = (long long)vobjregionmap(handle, (uintptr)c.arg0);
		break;
	case ObjOpRegionUnmap:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg1 != 0)
			error(Ebadarg);
		vobjregionunmap((uintptr)c.arg0);
		c.result = 0;
		break;
	case ObjOpPutOid:
		/* Replace one explicitly named OID through the Space's Owrite
		 * authority.  It never allocates, never commits and keeps the
		 * 64 MiB native object bound so a whole OID_GRAPH keeps its
		 * former OSPACEPUT size. */
		if(c.inlen > OBJ_PUT_OID_MAX)
			error(Ebadarg);
		if(c.in == nil && c.inlen != 0)
			error(Ebadarg);
		c.result = vobjputoid(handle, c.arg0, c.in, (long)c.inlen);
		break;
	case ObjOpCommit:
	{
		uvlong ver;

		if(c.out != nil){
			if(c.outlen != sizeof ver)
				error(Ebadarg);
			validaddr((uintptr)c.out, sizeof ver, 1);
			vobjcommit(handle, &ver);
			memmove(c.out, &ver, sizeof ver);
		}else{
   if(c.outlen != 0) error(Ebadarg);
			vobjcommit(handle, nil);
  }
		c.result = 0;
		break;
	}
	default:
		error("obj: operation not supported");
	}
	memmove(ucall, &c, sizeof c);
	return (uintptr)c.result;
}
