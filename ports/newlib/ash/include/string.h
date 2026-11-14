/* Provide prototypes for GNU string extensions consumed by original ash.
 * Implementations are in ports/newlib/ash/ash_posix.c; Newlib supplies the
 * standard libc string routines. Do not rely on implicit int prototypes.
 */
#ifndef PLAN2001_ASH_NEWLIB_STRING_H
#define PLAN2001_ASH_NEWLIB_STRING_H
#include_next <string.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *mempcpy(void *restrict dest,const void *restrict src,size_t n);
char *strchrnul(const char *s,int character);
#ifdef __cplusplus
}
#endif
#endif
