/* Missing generic unknown-elf declarations. Implementations fail closed when
 * no native userspace facility exists; never import Linux headers. */
#ifndef PLAN2001_MULTICALL_NATIVE_POSIX_H
#define PLAN2001_MULTICALL_NATIVE_POSIX_H
#include <sys/types.h>
int mknod(const char *, mode_t, dev_t);
#endif
