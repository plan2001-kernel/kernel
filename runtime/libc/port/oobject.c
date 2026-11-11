/*
 * libc/libo-style adapters over the universal object ABI.
 *
 * The kernel boundary is exactly CREATE, OPEN, CLOSE, BIND and INVOKE.
 * Everything here is argument packing plus the familiar READ/WRITE/
 * PUBLISH/SUBSCRIBE/RECEIVE/ACCEPT ergonomics on top of INVOKE.  A READ of
 * a byte-stream object returns a byte count; a RECEIVE from an ObjectFIFO
 * returns one authorized Handle and an optional envelope.
 */
#include	<u.h>
#include	<libc.h>
#include	<uartapi.h>
#include	<usbctl.h>
#include	"../../abi/vobj.h"
#include	"../../abi/obj.h"
#include	"../../abi/sysctl.h"
#include	"../../abi/ioctl.h"

extern long objcreate(unsigned long long, unsigned long long, void *,
		unsigned long long, void *);
extern long objopen(unsigned long long, void *, long, unsigned long long,
		void *);
extern long objclose(int);
extern long objbind(int, void *, long, unsigned long long,
		unsigned long long);
extern long objinvoke(int, void *, long);
extern long sysctl(unsigned int, unsigned int, SysctlReq *, long);
extern long ioctl(int, unsigned int, IoctlReq *, long);
extern long spawn(int, unsigned long long, char **, int, struct Ospawn *);
extern long rforkex(unsigned long, void *, unsigned long long *);
extern long regionwait(unsigned long long, unsigned long long,
		unsigned long long, long long);
extern long regionwake(unsigned long long, unsigned long long,
		unsigned long long);

long
obj_create(unsigned long long space, unsigned int kind, void *data,
	unsigned long long len, ObjId *out)
{
	return objcreate(space, kind, data, len, out);
}

long
obj_open(unsigned long long provider, void *req, long reqlen,
		unsigned long long rights, ObjId *out)
{
	return objopen(provider, req, reqlen, rights, out);
}

long
obj_close(int handle)
{
	return objclose(handle);
}

long
obj_bind(int handle, const char *role, unsigned long long len,
		unsigned long long rights, unsigned int mode)
{
	return objbind(handle, (void*)role, (long)len, rights, mode);
}

long
obj_invoke(int handle, ObjCall *call, long calllen)
{
	return objinvoke(handle, call, calllen);
}

static void
callinit(ObjCall *c, unsigned int op)
{
	memset(c, 0, sizeof *c);
	c->version = OBJ_ABI_VERSION;
	c->op = op;
}

/* Space-scoped persistent operations over INVOKE.  Creation is atomic and
 * synchronizing; PutOid only stages one named OID and never commits. */
long
obj_create_persist(int space, void *data, unsigned long long len,
                   unsigned long long *oid)
{
    ObjId id;
    long h;

    if(oid == nil){werrstr("bad arg in system call");return -1;}
    h = obj_create(space, ObjKindGe, data, len, &id);
    if(h < 0) return h;
    obj_close(h);
    *oid = id.oid;
    return 0;
}

long
obj_put_oid(int space, unsigned long long oid, void *buf,
		unsigned long long len)
{
	ObjCall c;

	if(len > OBJ_PUT_OID_MAX || (len != 0 && buf == nil))
		return -1;
	callinit(&c, ObjOpPutOid);
	c.arg0 = oid;
	c.in = buf;
	c.inlen = len;
	return obj_invoke(space, &c, sizeof c);
}

long
obj_commit(int handle, unsigned long long *version)
{
	ObjCall c;

	callinit(&c, ObjOpCommit);
	if(version != nil){
		c.out = version;
		c.outlen = sizeof *version;
	}
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_read(int handle, void *buf, unsigned long long len,
		unsigned long long timeout)
{
	ObjCall c;

	callinit(&c, ObjOpRead);
	c.out = buf;
	c.outlen = len;
	c.arg0 = timeout;
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_read_version(int handle, void *buf, unsigned long long len,
		unsigned long long timeout, unsigned long long *version)
{
	ObjCall c;
	long r;

	callinit(&c, ObjOpRead);
	c.out = buf;
	c.outlen = len;
	c.arg0 = timeout;
	r = obj_invoke(handle, &c, sizeof c);
	if(version != nil)
		*version = c.aux;
	return r;
}

long
obj_write(int handle, void *buf, unsigned long long len,
		unsigned long long timeout)
{
	ObjCall c;

	callinit(&c, ObjOpWrite);
	c.in = buf;
	c.inlen = len;
	c.arg0 = timeout;
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_info(int handle, void *info)
{
	ObjCall c;

	callinit(&c, ObjOpInfo);
	c.out = info;
	c.outlen = sizeof(VobjInfo);
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_stat(int handle, void *stat)
{
	ObjCall c;

	callinit(&c, ObjOpStat);
	c.out = stat;
	c.outlen = sizeof(VobjFifostat);
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_subscribe(int fifo, unsigned long long rights,
		unsigned long long capacity)
{
	ObjCall c;

	callinit(&c, ObjOpSubscribe);
	c.arg0 = rights;
	c.arg1 = capacity;
	return obj_invoke(fifo, &c, sizeof c);
}

long
obj_publish(int fifo, int object, void *env, unsigned long long envlen)
{
	ObjCall c;

	callinit(&c, ObjOpSend);
	c.arg0 = (unsigned long long)object;
	c.in = env;
	c.inlen = envlen;
	return obj_invoke(fifo, &c, sizeof c);
}

long
obj_receive(int subscription, void *env, unsigned long long envcap,
		unsigned long long timeout, unsigned long long *got)
{
	ObjCall c;
	long r;

	callinit(&c, ObjOpRecv);
	c.out = env;
	c.outlen = envcap;
	c.arg0 = timeout;
	r = obj_invoke(subscription, &c, sizeof c);
	if(got != nil)
		*got = c.aux;
	return r;
}

long
obj_grant(int handle, unsigned long long rights)
{
    ObjOpenHandleReq req = {handle, 0};

    if(rights == 0 || rights > Vall){werrstr("bad arg in system call");return -1;}
    return obj_open(ObjOpenHandle, &req, sizeof req, rights, nil);
}

/* GE/O grant or volatile-region map revocation. */
long
obj_revoke(int handle)
{
	ObjCall c;

	callinit(&c, ObjOpRevoke);
	return obj_invoke(handle, &c, sizeof c);
}

/* Close the FIFO transport for all subscribers.  This is not revocation. */
long
obj_fifo_shutdown(int handle)
{
	ObjCall c;

	callinit(&c, ObjOpFifoShutdown);
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_list_count(void *records, unsigned long long count,
		unsigned long long *copied)
{
	SysctlReq r;
	long ret;

	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.data = records;
	r.datalen = count * sizeof(VobjListRec);
	ret = sysctl(SysctlScopeOwnProcess, SysctlHandleList, &r, sizeof r);
	if(ret < 0)
		return ret;
	if(copied != nil)
		*copied = r.aux;
	return (long)r.result;
}

/* The return value is the total live Handle count; an undersized buffer is
 * safe and *copied (in obj_list_count) reports how many records were copied. */
long
obj_list(void *records, unsigned long long count)
{
	return obj_list_count(records, count, nil);
}

/* Native Pipe, UART and KBD userland adapters.  They add no kernel entry
 * point: Pipe creation is CREATE(kind=Pipe), the peer is INVOKE(PipePeer),
 * byte I/O is READ/WRITE, and UART/KBD controls are typed INVOKE calls. */
long
obj_pipe_create(ObjId *out)
{
	return obj_create(0, ObjKindPipe, nil, 0, out);
}

long
obj_pipe_peer(int handle)
{
	ObjCall c;

	callinit(&c, ObjOpPipePeer);
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_region_create(unsigned long long size, ObjId *out)
{
	return obj_create(0, ObjKindRegion, nil, size, out);
}

long
obj_region_map(int handle, unsigned long long addr)
{
	ObjCall c;

	callinit(&c, ObjOpRegionMap);
	c.arg0 = addr;
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_region_unmap(unsigned long long addr)
{
	ObjCall c;

	callinit(&c, ObjOpRegionUnmap);
	c.arg0 = addr;
	return obj_invoke(0, &c, sizeof c);
}

long
region_wait(unsigned long long base, unsigned long long offset,
		unsigned long long expected, long long timeout)
{
	return regionwait(base, offset, expected, timeout);
}

long
region_wake(unsigned long long base, unsigned long long offset,
		unsigned long long count)
{
	return regionwake(base, offset, count);
}

/* Device control is IOCTL(145); payload stays in OBJINVOKE. */
long
obj_device_ioctl(int device, unsigned int operation, void *data,
		unsigned long long len, unsigned long long arg0)
{
	IoctlReq r;

	memset(&r, 0, sizeof r);
	r.version = IOCTL_ABI_VERSION;
	r.data = data;
	r.datalen = len;
	r.arg0 = arg0;
	return ioctl(device, operation, &r, sizeof r);
}

long
obj_uart_ctl(int handle, void *cfg)
{
	if(cfg == nil)
		return -1;
	return obj_device_ioctl(handle, IoctlUartConfig, cfg,
		sizeof(Uartcfg), 0);
}

long
obj_uart_status(int handle, void *buf, unsigned long long len)
{
	if(buf == nil || len == 0)
		return -1;
	return obj_device_ioctl(handle, IoctlUartStatus, buf, len, 0);
}

long
obj_kbd_leds(int handle, unsigned int leds)
{
	return obj_device_ioctl(handle, IoctlKbdLeds, nil, 0, leds);
}

long
obj_device_usb_global_ctl(int catalog, unsigned int request, void *data,
		unsigned long long len)
{
	return obj_device_ioctl(catalog, IoctlUsbGlobal, data, len, request);
}

long
obj_device_usb_ep_ctl(int endpoint, unsigned int request, void *data,
		unsigned long long len)
{
	return obj_device_ioctl(endpoint, IoctlUsbEndpoint, data, len, request);
}

long
obj_device_usb_clear_halt(int device)
{
	return obj_device_ioctl(device, IoctlUsbClearHalt, nil, 0, 0);
}

long
obj_device_ether_detach(int device)
{
	return obj_device_ioctl(device, IoctlEtherDetach, nil, 0, 0);
}

/* Typed process-capability operations over OBJINVOKE. */
long
obj_process_info(int handle, ObjProcessInfo *out)
{
	ObjCall c;

	memset(&c, 0, sizeof c);
	c.version = OBJ_ABI_VERSION;
	c.op = ObjOpProcessInfo;
	c.out = out;
	c.outlen = sizeof *out;
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_process_wait(int handle, ObjProcessInfo *out, long long timeout)
{
	ObjCall c;

	memset(&c, 0, sizeof c);
	c.version = OBJ_ABI_VERSION;
	c.op = ObjOpProcessWait;
	c.arg0 = (unsigned long long)timeout;
	c.out = out;
	c.outlen = sizeof *out;
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_process_terminate(int handle)
{
	ObjCall c;

	memset(&c, 0, sizeof c);
	c.version = OBJ_ABI_VERSION;
	c.op = ObjOpProcessTerminate;
	return obj_invoke(handle, &c, sizeof c);
}

/* Ordinary fork is the canonical extended fork with an empty Handle plan. */
int
rfork(int flags)
{
	return (int)rforkex((unsigned long)flags, nil, nil);
}

/* Direct SPAWN: an authorized Space Handle plus OID, never an INVOKE route. */
long
obj_ospawn(int space, unsigned long long oid, char **argv, int argc,
		struct Ospawn *options)
{
	if(argc < 0)
		return -1;
	return spawn(space, oid, argv, argc, options);
}

/* Common device adapters; concrete wire messages remain provider-specific. */
long
obj_devices_open(unsigned long long rights)
{
	return obj_open(ObjOpenDevices, nil, 0, rights, nil);
}

long
obj_device_list(int catalog, ObjDeviceEntry *entries, unsigned int count)
{
	ObjCall c;

	callinit(&c, ObjOpDeviceList);
	c.out = entries;
	c.outlen = (unsigned long long)count*sizeof *entries;
	return obj_invoke(catalog, &c, sizeof c);
}

long
obj_device_open(int catalog, unsigned int family, int selector0, int selector1,
		unsigned long long rights, void *options, unsigned long long optlen)
{
	ObjOpenDeviceReq r;

	if(optlen > OBJ_DEVICE_OPTMAX || (optlen != 0 && options == nil)){
		werrstr("bad arg in system call");
		return -1;
	}
	memset(&r, 0, sizeof r);
	r.version = OBJ_DEVICE_ABI_VERSION;
	r.family = family;
	r.catalog = catalog;
	r.selector0 = selector0;
	r.selector1 = selector1;
	r.optlen = (unsigned int)optlen;
	if(optlen != 0)
		memmove(r.options, options, optlen);
	return obj_open(ObjOpenDevice, &r, sizeof r, rights, nil);
}

long
obj_catalog_device_open(unsigned int family, int selector0, int selector1,
		unsigned long long rights, void *options, unsigned long long optlen)
{
	char err[ERRMAX];
	long cat, h;

	cat = obj_devices_open(Vmeta|Vdelegate|Vcreate);
	if(cat < 0)
		return -1;
	h = obj_device_open((int)cat, family, selector0, selector1, rights,
		options, optlen);
	if(h < 0)
		rerrstr(err, sizeof err);
	obj_close((int)cat);
	if(h < 0)
		werrstr("%s", err);
	return h;
}

long
obj_device_send_kind(int device, void *message, unsigned long long length,
		unsigned int *kind)
{
	ObjCall c;
	long r;

	callinit(&c, ObjOpDeviceSend);
	c.in = message;
	c.inlen = length;
	r = obj_invoke(device, &c, sizeof c);
	if(kind != nil)
		*kind = (unsigned int)c.aux;
	return r;
}

long
obj_device_send(int device, void *message, unsigned long long length)
{
	return obj_device_send_kind(device, message, length, nil);
}

/* One completed USB control message. */
long
obj_device_recv(int device, ObjDeviceResult *completion)
{
	ObjCall c;

	callinit(&c, ObjOpDeviceRecv);
	c.out = completion;
	c.outlen = sizeof *completion;
	return obj_invoke(device, &c, sizeof c);
}

/* Acquire the Ethernet receive FIFO channel (not a completed message). */
long
obj_device_rx(int device)
{
	ObjCall c;

	callinit(&c, ObjOpDeviceRx);
	return obj_invoke(device, &c, sizeof c);
}

long
obj_device_ether_status(int device, ObjEtherStatus *status)
{
    return obj_device_ioctl(device, IoctlEtherStatus, status,
        sizeof *status, 0);
}
