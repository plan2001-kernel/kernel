/* Newlib exports non-POSIX itoa/utoa(number,char*,base) conversion helpers.
 * BusyBox libbb has its own itoa/utoa(number) with incompatible signatures. Expose the
 * genuine Newlib declarations FIRST; then rename only the application
 * symbol consistently in ash's source unit. No upstream source edits.
 */
#ifndef PLAN2001_ASH_NEWLIB_STDLIB_H
#define PLAN2001_ASH_NEWLIB_STDLIB_H
#include_next <stdlib.h>
#define itoa bb_itoa
#define utoa bb_utoa
#endif
