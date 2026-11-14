/* No POSIX tty ioctl transport is implied by the Plan2001 object ABI. */
#ifndef PLAN2001_ASH_NEWLIB_SYS_IOCTL_H
#define PLAN2001_ASH_NEWLIB_SYS_IOCTL_H
int ioctl(int fd,unsigned long request,...);
#endif
