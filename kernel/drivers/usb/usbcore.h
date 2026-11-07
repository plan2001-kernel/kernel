/*
 * USB kernel core.
 *
 * The core owns the live endpoint/device state and the HCI probe/root-hub
 * service.  It knows nothing about Chans, Qids, directory walks or the
 * removed legacy #u files; native syscalls use it directly.
 */
#ifndef _USBCORE_H_
#define _USBCORE_H_

#include "../drivers/usb/usb.h"
#include "usbctl.h"			/* via -I sys/include */

typedef struct Usbentry Usbentry;

Ep*	usbcore_getep(int devno, int epno);	/* referenced on success */
Ep*	usbcore_getepname(char *name);		/* referenced on success */
void	usbcore_putep(Ep*);
int	usbcore_dataopen(Ep*, int mode);	/* 0 ok; errors via error() */
void	usbcore_dataclose(Ep*);
long	usbcore_read(Ep*, void*, long);
long	usbcore_write(Ep*, void*, long);
long	usbcore_ioctl(Ep*, int req, void *arg, long narg);
long	usbcore_globalioctl(int req, void *arg, long narg);
int	usbcore_list(Usbentry*, int);
void	usbcore_init(void);
void	usbcore_shutdown(void);
void	usbcore_reset(void);

#endif
