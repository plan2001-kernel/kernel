/* Genuine Newlib types/prototypes, with the target's checked set operations.
 * The generic unknown-elf macros shift a signed int and do not validate
 * signal numbers; use the runtime functions for all 31 supported signals. */
#ifndef PLAN2001_POSIX_SIGNAL_H
#define PLAN2001_POSIX_SIGNAL_H
#include_next <signal.h>
#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember
#endif
