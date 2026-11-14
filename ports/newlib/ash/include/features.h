/* BusyBox 1.36.1 checks _NEWLIB_VERSION and includes features.h.
 * Newlib 4.6.0 intentionally publishes its real feature-test definitions
 * as <sys/features.h>, not <features.h>. No glibc header may be imported.
 */
#ifndef P2001_ASH_NEWLIB_FEATURES_H
#define P2001_ASH_NEWLIB_FEATURES_H
#include <sys/features.h>
#endif
