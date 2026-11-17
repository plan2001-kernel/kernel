/* Pure byte-order helpers and opaque network source prototypes.
 * There is no BSD-socket implementation or ambient network authority.
 */
#ifndef PLAN2001_ASH_NEWLIB_ARPA_INET_H
#define PLAN2001_ASH_NEWLIB_ARPA_INET_H
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htons(x) __builtin_bswap16((uint16_t)(x))
#define ntohs(x) __builtin_bswap16((uint16_t)(x))
#define htonl(x) __builtin_bswap32((uint32_t)(x))
#define ntohl(x) __builtin_bswap32((uint32_t)(x))
#else
#define htons(x) ((uint16_t)(x))
#define ntohs(x) ((uint16_t)(x))
#define htonl(x) ((uint32_t)(x))
#define ntohl(x) ((uint32_t)(x))
#endif
int inet_pton(int address_family,const char *source,void *destination);
const char *inet_ntop(int address_family,const void *source,char *destination,socklen_t size);
#endif
