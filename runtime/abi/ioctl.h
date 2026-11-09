/*
 * IOCTL: typed device configuration, control and device-specific status.
 *
 * The call is ioctl(device_handle, operation, IoctlReq *request, long size).
 * Authority is the authorized process-local device Handle and its rights;
 * the operation number, selector or a rights mask never creates authority.
 * Read-only status and mutating control use distinct required rights.
 *
 * Device payload transport stays in the object interface: UART bytes,
 * Ethernet frames and USB transfer requests/completions are not IOCTL.
 */
#ifndef PLAN2001_ABI_IOCTL_H
#define PLAN2001_ABI_IOCTL_H

#define IOCTL_ABI_VERSION 1

enum {
	IoctlUartConfig		= 1,	/* data: Uartcfg input, Vcontrol */
	IoctlUartStatus		= 2,	/* data: status bytes out, Vmeta */
	IoctlKbdLeds		= 3,	/* arg0: LED bits, Vcontrol */
	IoctlUsbGlobal		= 4,	/* arg0: USB request, data in/out, Vcontrol */
	IoctlUsbEndpoint	= 5,	/* arg0: USB request, data in/out, Vcontrol */
	IoctlEtherStatus	= 6,	/* data: ObjEtherStatus out, Vmeta */
	IoctlEtherDetach	= 7,	/* no data, Vcontrol */
	IoctlUsbClearHalt	= 8,	/* no data, Vcontrol */
};

typedef struct IoctlReq {
	unsigned int		version;	/* IOCTL_ABI_VERSION */
	unsigned int		flags;		/* must be zero */
	unsigned int		reserved;	/* must be zero */
	unsigned int		reserved2;	/* must be zero */
	void			*data;		/* in/out buffer */
	unsigned long long	datalen;	/* data length in bytes */
	unsigned long long	arg0;		/* operation-specific scalar */
	long long		result;		/* out: operation result */
} IoctlReq;

long	ioctl(int handle, unsigned int operation, IoctlReq *req, long size);

#endif /* PLAN2001_ABI_IOCTL_H */
