#ifndef _USBCTL_H_
#define _USBCTL_H_
enum { USBDATA = 0, USBCTL = 1, USBSVC = 2 };
enum {
    USBdebug = 1, USBnew, USBnewdev, USBdetach, USBreset, USBdebugep,
    USBclrhalt, USBaddress, USBhub, USBmaxpkt, USBntds, USBpollival,
    USBtimeout, USBhz, USBsamplesz, USBsampledelay, USBuframes,
    USBname, USBinfo,
};
enum { USBTnone = 0, USBTctl, USBTiso, USBTbulk, USBTintr };
enum { USBOrdwr = 2, USBOWRITE = 1, USBOREAD = 0 };
enum { USBfull = 0, USBlow = 1, USBhigh = 2, USBsuper = 4 };
struct Usbnew { int nb; int type; int mode; };
struct Usbnewdev { int speed; int port; char reply[16]; };
struct Usbhub { int nports; int ttt; int mtt; };
struct Usbentry {
    int idx; int devno; int epno; int ttype; int mode;
    int speed; int state; int inuse; int maxpkt; char name[128];
};
#endif
