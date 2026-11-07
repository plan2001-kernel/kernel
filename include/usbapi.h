#ifndef _USB_H_
#define _USB_H_

#include <usbctl.h>

typedef struct Usbentry Usbentry;

typedef struct Usb { int h; } Usb;
typedef struct UsbEp { Usb *usb; int data; int ctl; } UsbEp;

int	usb_open(Usb*);
int	usb_list(Usb*, Usbentry*, int);
int	usb_epopen(Usb*, int, int, int, int);
int	usb_epname(Usb*, char*, int, int);
int	usb_endpoint(Usb*, int, int, UsbEp*);
long	usb_read(UsbEp*, void*, long);
long	usb_write(UsbEp*, void*, long);
long	usb_ioctl(UsbEp*, int, void*, long);
int	usb_close(Usb*);

long	usb_readh(int, void*, long);
long	usb_writeh(int, void*, long);
long	usb_ioctlh(int, int, void*, long);
int	usb_closeh(int);

#endif
