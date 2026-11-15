/* Compatibility for BusyBox's conditional sys/sysmacros.h include.
 * These are *userspace dev_t number packing helpers*, not GE/O rights.
 * No device can be opened on Plan2001 from a packed dev_t.
 */
#ifndef P2001_ASH_SYS_SYSMACROS_H
#define P2001_ASH_SYS_SYSMACROS_H
#include <sys/types.h>
#define major(x) ((unsigned int)(((unsigned long long)(x) >> 8) & 0xfffULL))
#define minor(x) ((unsigned int)(((unsigned long long)(x) & 0xffULL) | (((unsigned long long)(x) >> 12) & ~0xffULL)))
#define makedev(ma,mi) ((dev_t)((((unsigned long long)(ma) & 0xfffULL) << 8) | ((unsigned long long)(mi) & 0xffULL) | (((unsigned long long)(mi) & ~0xffULL) << 12)))
#endif
