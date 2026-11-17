/* POSIX mmap source declarations, NOT an implemented mapping syscall.
 * Region sharing must use explicit native capability mappings.
 */
#ifndef PLAN2001_ASH_NEWLIB_SYS_MMAN_H
#define PLAN2001_ASH_NEWLIB_SYS_MMAN_H
#include <sys/types.h>
#include <stddef.h>
#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_SHARED 0x1
#define MAP_PRIVATE 0x2
/* Source-only flag: the native mmap adapter always reports ENOSYS. */
#define MAP_ANONYMOUS 0x100
#define MAP_FAILED ((void *)-1)
void *mmap(void *,size_t,int,int,int,off_t);
int munmap(void *,size_t);
int mprotect(void *,size_t,int);
#endif
