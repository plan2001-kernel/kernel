/*
 * Universal five-operation object ABI: CREATE, OPEN, CLOSE, BIND, INVOKE.
 *
 * These five entry points are the kernel boundary.  READ/WRITE,
 * PUBLISH/SUBSCRIBE/RECEIVE and ACCEPT/CONNECT are userland adapters over
 * INVOKE; the kernel does not grow a syscall family per provider.  INVOKE is
 * a request router: it validates the authorized Handle, the operation and
 * each buffer, then calls the existing provider implementation in
 * sysvobj.c/systcp6.c.  It is not an object class hierarchy and contains no
 * network stack.
 *
 * The legacy VOBJ* family remains a compatibility shim over the same core.
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
	case ObjOpenTcpListen:
	case ObjOpenTcpConnect:
        error("obj: kernel network protocols disabled; use raw device");
	case ObjOpenGe:
		return vobjopenstream(provider, req, len, rights, (VobjId*)id);
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
	case ObjOpenUdp6:
        error("obj: kernel network protocols disabled; use raw device");
	case ObjOpenUart:
	{
		ObjOpenUartReq ur;

		if(req == nil || len != sizeof ur)
			error(Ebadarg);
		validaddr((uintptr)req, sizeof ur, 0);
		memmove(&ur, req, sizeof ur);
		if(ur.flags != 0 || ur.reserved != 0)
			error(Ebadarg);
		/* OPENUART derives its rights from the native mode; an explicit
		 * attenuation is applied after the open. */
		if(rights & ~(uvlong)Vall)
			error(Ebadarg);
		if(ur.mode != OREAD && ur.mode != OWRITE && ur.mode != ORDWR)
			error(Ebadarg);
		return vobjuartopenrights(ur.port, ur.mode, rights, (VobjId*)id);
	}
	case ObjOpenKbd:
		if(req != nil && len != 0)
			error(Ebadarg);
		if(rights != 0)
			error(Eperm);
		return vobjkbdopen((VobjId*)id);
	case ObjOpenUsb:
		if(req != nil && len != 0)
			error(Ebadarg);
		return vobjusbopen(rights, (VobjId*)id);
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
    case ObjOpenProcessSelf:
		if(req != nil && len != 0)
			error(Ebadarg);
		return vobjprocessselfopen(rights, (VobjId*)id);
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
    case ObjOpDeviceCtl:
        if(c.in != nil || c.inlen != 0 || c.arg1 != 0)
            error(Ebadarg);
        c.result = vobjdevicectl(handle, c.arg0, c.out,
            bounded(c.outlen, sizeof(ObjEtherStatus), Ebadarg));
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

		ver = 0;
		c.result = vobjreadver(handle, c.out,
			bounded(c.outlen, Vobjpaymax, Ebadarg),
			bounded(c.arg0, 0x7fffffff, Ebadarg), &ver);
		/* A GE read publishes the version it returned; every other
		 * provider leaves aux at its previous zero value. */
		if(c.result >= 0)
			c.aux = ver;
		break;
	}
	case ObjOpWrite:
		c.result = vobjio(handle, VioWrite, c.in,
			bounded(c.inlen, Vobjpaymax, Ebadarg),
			bounded(c.arg0, 0x7fffffff, Ebadarg));
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
	case ObjOpAccept:
		c.result = vobjaccept(handle,
			(int)bounded(c.arg0, 0x7fffffff, Ebadarg));
		break;
	case ObjOpGrant:
		c.result = vobjgrant(handle, c.arg0);
		break;
	case ObjOpRevoke:
		vobjrevoke(handle);
		c.result = 0;
		break;
	case ObjOpList:
		c.result = vobjlist(c.out,
			bounded(c.outlen, Vhandmax, Ebadarg));
		break;
	case ObjOpCreatePersist:
	{
		uvlong oid;
  int created;

		if(c.out == nil || c.outlen != sizeof oid)
			error(Ebadarg);
		validaddr((uintptr)c.out, sizeof oid, 1);
		created = vobjcreatege(handle, c.in,
			bounded(c.inlen, Vobjpaymax, Ebadarg), &oid, nil);
  vobjclosehandle(created); /* INVOKE returns an OID, not a live Handle */
		memmove(c.out, &oid, sizeof oid);
		c.result = 0;
		break;
	}
	case ObjOpSendTo:
		if(c.in == nil || c.inlen < sizeof(ObjUdp6SendReq))
			error(Ebadarg);
		c.result = vobjudp6send(handle, c.in,
			bounded(c.inlen, Vobjpaymax, Ebadarg));
		break;
	case ObjOpRecvFrom:
		if(c.out == nil || c.outlen < sizeof(ObjUdp6RecvHdr))
			error(Ebadarg);
		if(c.arg0 > 0xffffffffULL)
			error(Ebadarg);
		c.result = vobjudp6recv(handle, c.out,
			bounded(c.outlen,
				sizeof(ObjUdp6RecvHdr) + OBJ_UDP6_MAXPAYLOAD,
				Ebadarg), c.arg0);
		break;
	case ObjOpPipePeer:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0)
			error(Ebadarg);
		c.result = vobjpipepeer(handle);
		break;
	case ObjOpPipeInherit:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0)
			error(Ebadarg);
		if(c.arg0 > 3)
			error(Ebadarg);
		vobjpipeinherit(handle, (int)c.arg0);
		c.result = 0;
		break;
	case ObjOpUartCount:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0)
			error(Ebadarg);
		c.result = uartnports();
		break;
	case ObjOpNet6Addr:
		if(c.in != nil || c.inlen != 0 || c.out == nil
		|| c.outlen != OBJ_NET6_ADDRLEN)
			error(Ebadarg);
		validaddr((uintptr)c.out, OBJ_NET6_ADDRLEN, 1);
		c.result = vudp6addr(c.out, OBJ_NET6_ADDRLEN);
		break;
	case ObjOpUartCtl:
		if(c.in == nil || c.inlen != sizeof(Uartcfg))
			error(Ebadarg);
		vobjuartctl(handle, c.in);
		c.result = 0;
		break;
	case ObjOpUartStatus:
		if(c.out == nil || c.outlen == 0 || c.outlen > OBJ_UART_STATMAX)
			error(Ebadarg);
		c.result = vobjuartstatus(handle, c.out, (long)c.outlen);
		break;
	case ObjOpKbdLeds:
		if(c.in != nil || c.inlen != 0 || c.out != nil || c.outlen != 0)
			error(Ebadarg);
		c.result = vobjkbdleds(handle,
			(int)bounded(c.arg0, 0x7fffffff, Ebadarg));
		break;
	case ObjOpUsbList:
	{
		uvlong cap;
		long outlen;

		if(c.in != nil || c.inlen != 0)
			error(Ebadarg);
		if(c.out == nil && c.outlen != 0)
			error(Ebadarg);
		if(c.outlen % sizeof(ObjUsbEntry) != 0)
			error(Ebadarg);
		/* A capacity beyond the record bound is accepted and clamped,
		 * exactly like the obj_usb_list() adapter.  The provider then
		 * validates only the bytes it actually copies. */
		cap = (uvlong)OBJ_USB_LISTMAX * sizeof(ObjUsbEntry);
		outlen = c.outlen > cap ? (long)cap : (long)c.outlen;
		c.result = vobjusblist(handle, c.out, outlen);
		break;
	}
	case ObjOpUsbEpOpen:
		if(c.in == nil || c.inlen != sizeof(ObjUsbEpReq) ||
		   c.out != nil || c.outlen != 0)
			error(Ebadarg);
		c.result = vobjusbepopen(handle, c.in);
		break;
	case ObjOpTimeNow:
		if(c.in != nil || c.inlen != 0 || c.out == nil
		|| c.outlen != sizeof(uvlong))
			error(Ebadarg);
		validaddr((uintptr)c.out, sizeof(uvlong), 1);
		c.result = vobjtimenow(handle, (uvlong*)c.out);
		break;
	case ObjOpErrstrExchange:
		/* No Handle is opened or consumed: the operation touches only the
		 * calling process's own error buffers, so a exhausted Handle table
		 * cannot be the reason its error text cannot be read.  The buffer is
		 * the single IN/OUT area and generrstr() performs the exact ERRSTR(41)
		 * swap after validating the user address. */
		if(handle != 0 || c.in != nil || c.inlen != 0
		|| c.out == nil || c.outlen == 0 || c.outlen > OBJ_ERRSTR_MAX
		|| c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		c.result = generrstr((char*)c.out, (uint)c.outlen);
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
	case ObjOpOspawn:
		if(c.in == nil || c.inlen != sizeof(ObjOspawnReq)
		|| c.out != nil || c.outlen != 0 || c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		c.result = vobjospawn(handle, c.in);
		break;
	case ObjOpWait:
		if(c.in != nil || c.inlen != 0 || c.out == nil
		|| c.outlen != sizeof(ObjWait) || c.arg0 != 0 || c.arg1 != 0)
			error(Ebadarg);
		validaddr((uintptr)c.out, sizeof(ObjWait), 1);
		c.result = vobjwait(handle, (ObjWait*)c.out);
		break;
	default:
		error("obj: operation not supported");
	}
	memmove(ucall, &c, sizeof c);
	return (uintptr)c.result;
}
