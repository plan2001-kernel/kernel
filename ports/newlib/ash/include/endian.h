/* Expose the real freestanding machine-endian definitions to BusyBox.
 * Newlib ships <machine/endian.h> and <sys/endian.h>, not <endian.h>.
 */
#ifndef P2001_ASH_NEWLIB_ENDIAN_H
#define P2001_ASH_NEWLIB_ENDIAN_H
#include <sys/endian.h>
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER __BYTE_ORDER__
#endif
#endif
