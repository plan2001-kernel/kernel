/* Minimal POSIX utsname ABI for native Plan2001 AMD64 Newlib programs.
 * Newlib 4.6.0 installs sys/times.h but not sys/utsname.h.
 * The values describe this userland environment, not kernel capabilities.
 */
#ifndef P2001_ASH_SYS_UTSNAME_H
#define P2001_ASH_SYS_UTSNAME_H
#ifdef __cplusplus
extern "C" {
#endif
#define P2001_UTS_FIELD 65
struct utsname {
    char sysname[P2001_UTS_FIELD];
    char nodename[P2001_UTS_FIELD];
    char release[P2001_UTS_FIELD];
    char version[P2001_UTS_FIELD];
    char machine[P2001_UTS_FIELD];
};
int uname(struct utsname *name);
#ifdef __cplusplus
}
#endif
#endif
