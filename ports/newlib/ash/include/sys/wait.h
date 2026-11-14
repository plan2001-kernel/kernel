/* Newlib's wait status macros omit WCOREDUMP. This is only a status-bit
 * decoder, not a claim that kernel process handles implement waitpid.
 */
#ifndef PLAN2001_ASH_NEWLIB_WAIT_H
#define PLAN2001_ASH_NEWLIB_WAIT_H
#include_next <sys/wait.h>
#ifndef WCOREDUMP
#define WCOREDUMP(status) (((status) & 0x80) != 0)
#endif
#endif
