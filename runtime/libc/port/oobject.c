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
#include	"../../abi/vobj.h"
#include	"../../abi/obj.h"

extern long objcreate(unsigned long long, unsigned long long, void *,
		unsigned long long, void *);
extern long objopen(unsigned long long, void *, long, unsigned long long,
		void *);
extern long objclose(int);
extern long objbind(int, void *, long, unsigned long long,
		unsigned long long);
extern long objinvoke(int, void *, long);
extern long objlist(void *, long);
extern long objwait(void *);

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

long
obj_revoke(int handle)
{
	ObjCall c;

	callinit(&c, ObjOpRevoke);
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_list(void *records, unsigned long long count)
{
	return objlist(records, (long)count);
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

/* One-shot, attenuated RFORK(RFPROC) selection.  rights uses the legacy
 * Pipe bits 1 (read) and 2 (write); the next successful RFPROC consumes it. */
long
obj_pipe_inherit(int handle, unsigned int rights)
{
	ObjCall c;

	callinit(&c, ObjOpPipeInherit);
	c.arg0 = rights;
	return obj_invoke(handle, &c, sizeof c);
}

/* Discovery helper: count ObjDeviceUart records in the authorized common
 * device catalog.  There is no handle-less INVOKE operation and no UART
 * object invented just to report a count. */
long
obj_uart_count(void)
{
	ObjDeviceEntry *e;
	int cat, n, i, count;

	cat = (int)obj_devices_open(0);
	if(cat < 0)
		return -1;
	n = (int)obj_device_list(cat, nil, 0);
	if(n < 0 || n > OBJ_DEVICE_LISTMAX){
		obj_close(cat);
		return n < 0 ? n : -1;
	}
	if(n == 0){
		obj_close(cat);
		return 0;
	}
	e = malloc(n * sizeof *e);
	if(e == nil){
		obj_close(cat);
		return -1;
	}
	n = (int)obj_device_list(cat, e, (unsigned int)n);
	if(n < 0){
		free(e);
		obj_close(cat);
		return -1;
	}
	count = 0;
	for(i = 0; i < n; i++)
		if(e[i].family == ObjDeviceUart)
			count++;
	free(e);
	obj_close(cat);
	return count;
}

long
obj_uart_open(int port, int mode)
{
	ObjOpenUartReq req;

	if(mode != OREAD && mode != OWRITE && mode != ORDWR)
		return -1;
	memset(&req, 0, sizeof req);
	req.port = port;
	req.mode = mode;
	return obj_open(ObjOpenUart, &req, sizeof req, 0, nil);
}

long
obj_uart_ctl(int handle, void *cfg)
{
	ObjCall c;

	if(cfg == nil)
		return -1;
	callinit(&c, ObjOpUartCtl);
	c.in = cfg;
	c.inlen = sizeof(Uartcfg);
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_uart_status(int handle, void *buf, unsigned long long len)
{
	ObjCall c;

	if(buf == nil || len == 0)
		return -1;
	callinit(&c, ObjOpUartStatus);
	c.out = buf;
	c.outlen = len;
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_kbd_open(void)
{
	return obj_open(ObjOpenKbd, nil, 0, 0, nil);
}

long
obj_kbd_leds(int handle, unsigned int leds)
{
	ObjCall c;

	callinit(&c, ObjOpKbdLeds);
	c.arg0 = leds;
	return obj_invoke(handle, &c, sizeof c);
}

/* USB service.  The count is a record count; ents==nil or count==0 performs
 * the legacy USBLIST total-count query.  obj_usb_epopen needs a service
 * Handle carrying Vcreate; the returned endpoint Handle has rights derived
 * from kind and mode.  Neither call provides a data or control transfer. */
long
obj_usb_open(unsigned long long rights)
{
	return obj_open(ObjOpenUsb, nil, 0, rights, nil);
}

long
obj_usb_list(int handle, void *ents, unsigned long long count)
{
	ObjCall c;

	if(count > OBJ_USB_LISTMAX)
		count = OBJ_USB_LISTMAX;
	callinit(&c, ObjOpUsbList);
	c.out = ents;
	c.outlen = ents == nil ? 0 : count * sizeof(ObjUsbEntry);
	return obj_invoke(handle, &c, sizeof c);
}

long
obj_usb_epopen(int handle, int devno, int epno, int kind, int mode)
{
	ObjCall c;
	ObjUsbEpReq r;

	r.devno = devno;
	r.epno = epno;
	r.kind = kind;
	r.mode = mode;
	r.flags = 0;
	r.reserved = 0;
	callinit(&c, ObjOpUsbEpOpen);
	c.in = &r;
	c.inlen = sizeof r;
	return obj_invoke(handle, &c, sizeof c);
}

/* USB enumeration/administration shares the existing DeviceCtl operation;
 * its bounded buffer is IN/OUT, and the Handle must carry Vcontrol. */
long
obj_usb_ctl(int handle, unsigned int operation, void *data, unsigned long long len)
{
    ObjCall c;

    callinit(&c, ObjOpDeviceCtl);
    c.arg0 = operation;
    c.out = data;
    c.outlen = len;
    return obj_invoke(handle, &c, sizeof c);
}

long
obj_wait(ObjWait *w)
{
	if(w == nil)
		return -1;
	return objwait(w);
}

long
obj_ospawn(int space, unsigned long long oid, char **argv, int argc,
		struct Ospawn *options)
{
	ObjCall c;
	ObjOspawnReq r;

	if(argc < 0)
		return -1;
	r.oid = oid;
	r.argv = argv;
	r.argc = argc;
	r.flags = 0;
	r.reserved = 0;
	r.options = options;
	callinit(&c, ObjOpOspawn);
	c.in = &r;
	c.inlen = sizeof r;
	return obj_invoke(space, &c, sizeof c);
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
	unsigned long long rights)
{
	ObjOpenDeviceReq r;

	memset(&r, 0, sizeof r);
	r.version = OBJ_DEVICE_ABI_VERSION;
	r.family = family;
	r.catalog = catalog;
	r.selector0 = selector0;
	r.selector1 = selector1;
	return obj_open(ObjOpenDevice, &r, sizeof r, rights, nil);
}

long
obj_device_send(int device, void *message, unsigned long long length)
{
	ObjCall c;

	callinit(&c, ObjOpDeviceSend);
	c.in = message;
	c.inlen = length;
	return obj_invoke(device, &c, sizeof c);
}

long
obj_device_recv(int device, ObjDeviceResult *completion)
{
	ObjCall c;

	callinit(&c, ObjOpDeviceRecv);
	c.out = completion;
	c.outlen = sizeof *completion;
	return obj_invoke(device, &c, sizeof c);
}

long
obj_device_ctl(int device, unsigned int operation)
{
	ObjCall c;

	callinit(&c, ObjOpDeviceCtl);
	c.arg0 = operation;
	return obj_invoke(device, &c, sizeof c);
}

long
obj_device_ether_status(int device, ObjEtherStatus *status)
{
    ObjCall c;
    callinit(&c, ObjOpDeviceCtl);
    c.arg0 = ObjDeviceEtherStatus;
    c.out = status;
    c.outlen = sizeof *status;
    return obj_invoke(device, &c, sizeof c);
}
