/* Newlib x86_64-unknown-elf includes only getrusage in sys/resource.h.
 * BusyBox shell_common.c uses standard rlimit types. This is userland
 * bookkeeping only: values cannot grant or alter kernel memory authority.
 */
#ifndef PLAN2001_ASH_RESOURCE_H
#define PLAN2001_ASH_RESOURCE_H
#include_next <sys/resource.h>
#include <stdint.h>
typedef unsigned long long rlim_t;
struct rlimit { rlim_t rlim_cur, rlim_max; };
#define RLIM_INFINITY (~(rlim_t)0)
#define RLIM_SAVED_MAX RLIM_INFINITY
#define RLIM_SAVED_CUR RLIM_INFINITY
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_NOFILE 7
int getrlimit(int resource,struct rlimit *limit);
int setrlimit(int resource,const struct rlimit *limit);
#endif
