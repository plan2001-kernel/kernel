/* Newlib exports a non-POSIX itoa(int,char*,int). BusyBox libbb's own
 * itoa(int) uses the same name with a different signature. Expose the
 * genuine Newlib declarations FIRST; then rename only the application
 * symbol consistently in ash's source unit. No upstream source edits.
 */
#ifndef PLAN2001_ASH_NEWLIB_STDLIB_H
#define PLAN2001_ASH_NEWLIB_STDLIB_H
#include_next <stdlib.h>
#define itoa bb_itoa
#endif
