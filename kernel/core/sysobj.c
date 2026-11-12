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
 * TCP6/UDP6 object providers and the legacy FIFO/Pipe providers are gone: the
 * permitted network boundary is raw device frames plus userspace protocol
 * handling.  Process handle introspection is SYSCTL SysctlHandleList; process
 * completion observation and control are VkindProcess INIT operations;
 * device control is IOCTL.
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
#include	"../../runtime/abi/objectspace.h"
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
	if(kind == ObjKindObjectSpace){
		VobjId vid;
		int h;

		if(space != 0 || buf != nil || n != 0)
			error(Ebadarg);
		h = vobjspacecreate(&vid);
		if(id != nil){
			id->incarnation = vid.incarnation;
			id->space = vid.space;
			id->oid = vid.oid;
		}
		return h;
	}
	if(kind == ObjKindGe){
  if(space > 0x7fffffffULL) error(Ebadarg);
		VobjId vid;
		int h;

		h = vobjcreatege((int)space, buf, n, nil);
		if(id != nil){
			validaddr((uintptr)id, sizeof(ObjId), 1);
			vobjidentity(h, &vid);
			((ObjId*)id)->incarnation = vid.incarnation;
			((ObjId*)id)->space = vid.space;
			((ObjId*)id)->oid = vid.oid;
		}
		return h;
	}
	if(kind != ObjKindBytes)
		error(Ebadarg);
	return vobjcreat(VkindBytes, space, buf, n, (VobjId*)id);
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
	case ObjOpenVolume:
		if(req != nil || len != 0)
			error(Ebadarg);
		return vobjopenstream(VopenGeVolume, req, len, rights, (VobjId*)id);
	case ObjOpenGeOid:
		/* The on-wire request is layout-compatible with VobjGeOidReq, so the
		 * provider can validate and copy the user pointer itself. */
		if(req == nil || len != sizeof(ObjOpenGeOidReq))
			error(Ebadarg);
		return vobjopenstream(VopenGeOid, req, (long)sizeof(VobjGeOidReq),
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
	case ObjOpCapRevoke:
		/* Revoke issued descendants, never the source grant or provider. */
		if(c.in != nil || c.inlen != 0 || c.out != nil ||
		   c.outlen != 0 || c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		c.result = vobjcaprevoke(handle);
		break;
	case ObjOpSpaceBind:
	{
		ObjSpaceBindReq b;
		if(c.in == nil || c.inlen != sizeof b || c.out != nil ||
		   c.outlen != 0 || c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		validaddr((uintptr)c.in, sizeof b, 0);
		memmove(&b, c.in, sizeof b);
		c.result = vobjspacebind(handle, &b);
		break;
	}
	case ObjOpSpaceUnbind:
		if(c.in != nil || c.inlen != 0 || c.out != nil ||
		   c.outlen != 0 || c.arg0 == 0 || c.arg1 != 0)
			error(Ebadarg);
		vobjspaceunbind(handle, c.arg0);
		c.result = 0;
		break;
	case ObjOpSpaceResolve:
		if(c.in != nil || c.inlen != 0 || c.out != nil ||
		   c.outlen != 0 || c.arg0 == 0 || c.arg1 > Vall)
			error(Ebadarg);
		c.result = vobjspaceresolve(handle, c.arg0, c.arg1);
		break;
	case ObjOpSpaceEnumerate:
	{
		uvlong epoch = 0;
		if(c.in != nil || c.inlen != 0 ||
		   c.out == nil || c.outlen < sizeof(ObjSpaceEntry) ||
		   c.outlen > ObjSpacePageMax*sizeof(ObjSpaceEntry) ||
		   c.outlen % sizeof(ObjSpaceEntry) != 0)
			error(Ebadarg);
		validaddr((uintptr)c.out, (long)c.outlen, 1);
		c.result = vobjspaceenumerate(handle, c.arg0, c.arg1,
			(ObjSpaceEntry*)c.out, c.outlen/sizeof(ObjSpaceEntry), &epoch);
		c.aux = epoch;
		break;
	}
	case ObjOpDeviceList:
        if(c.in != nil || c.inlen != 0 || c.arg0 != 0 || c.arg1 != 0
        || (c.out == nil && c.outlen != 0)
        || c.outlen % sizeof(ObjDeviceEntry) != 0)
            error(Ebadarg);
        c.result = vobjdevicelist(handle, c.out,
            bounded(c.outlen, OBJ_DEVICE_LISTMAX*sizeof(ObjDeviceEntry), Ebadarg));
        break;
    case ObjOpDeviceSend:
        if(c.out != nil || c.outlen != 0 || c.arg0 != 0 || c.arg1 != 0)
            error(Ebadarg);
        c.result = vobjdevicesend(handle, c.in,
            bounded(c.inlen, OBJ_DEVICE_MAXMESSAGE, Ebadarg));
        break;
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
    case ObjOpDeviceTx:
        if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
           c.arg0 != 0 || c.arg1 != 0)
            error(Ebadarg);
        c.result = vobjdevicetx(handle);
        break;
    case ObjOpInfo:
		if(c.out == nil || c.outlen != sizeof(VobjInfo))
			error(Ebadarg);
		vobjinfo(handle, (VobjInfo*)c.out);
		c.result = 0;
		break;
    case ObjOpOwnerInfo:
		if(c.in != nil || c.inlen != 0 || c.arg0 != 0 || c.arg1 != 0 ||
		   c.out == nil || c.outlen != sizeof(ObjOwnerInfo))
			error(Ebadarg);
		vobjownerinfo(handle, (ObjOwnerInfo*)c.out);
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
	case ObjOpRevoke:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		vobjrevoke(handle);
		c.result = 0;
		break;
	case ObjOpRegionMap:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   (c.arg1 & ~(uvlong)ObjRegionMapFlags) != 0)
			error(Ebadarg);
		c.result = (long long)vobjregionmap(handle, (uintptr)c.arg0,
			(int)c.arg1);
		break;
	case ObjOpRegionUnmap:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg1 != 0)
			error(Ebadarg);
		vobjregionunmap((uintptr)c.arg0);
		c.result = 0;
		break;
	case ObjOpProcessInfo:
		if(c.in != nil || c.inlen != 0 || c.arg0 != 0 || c.arg1 != 0 ||
		   c.out == nil || c.outlen != sizeof(ObjProcessInfo))
			error(Ebadarg);
		c.result = vobjprocinfo(handle, (ObjProcessInfo*)c.out);
		break;
	case ObjOpProcessWait:
		if(c.in != nil || c.inlen != 0 || c.arg1 != 0 ||
		   c.out == nil || c.outlen != sizeof(ObjProcessInfo))
			error(Ebadarg);
		if(c.arg0 == PROCESS_WAIT_INFINITE)
			tmo = -1;
		else if(c.arg0 > 0x7fffffffULL)
			error(Ebadarg);
		else
			tmo = (int)c.arg0;
		validaddr((uintptr)c.out, sizeof(ObjProcessInfo), 1);
		c.result = vobjprocesswait(handle, (ObjProcessInfo*)c.out, tmo);
		break;
	case ObjOpProcessTerminate:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0 ||
		   c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		c.result = vobjprocterminate(handle);
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
