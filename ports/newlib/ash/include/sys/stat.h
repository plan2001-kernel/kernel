/* Add the missing POSIX lstat declaration to upstream Newlib's stat.h.
 * x86_64-unknown-elf Newlib 4.6.0 only exposes lstat for SPU/RTEMS/Cygwin.
 * The real Plan2001 implementation is in plan2001_syscalls.c and handles
 * UXFS file/directory metadata (no symbolic links).
 */
#ifndef PLAN2001_ASH_NEWLIB_STAT_H
#define PLAN2001_ASH_NEWLIB_STAT_H
#include_next <sys/stat.h>
#ifdef __cplusplus
extern "C" {
#endif
int lstat(const char *path, struct stat *st);
#ifdef __cplusplus
}
#endif
#endif
