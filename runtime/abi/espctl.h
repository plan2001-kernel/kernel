#ifndef PLAN2001_ESPCTL_H
#define PLAN2001_ESPCTL_H
#include "../ikecrypto/ikeport.h"
/* Private Pipe byte protocol; all integers big-endian, no native C structs.
 * Header: magic[4], version:u16, opcode:u16, request:u32, length:u32,
 * status:u32, reserved:u32. Responses echo opcode|0x8000 and request.
 * Unknown versions/flags/lengths fail closed. No syscall numbers are added. */
#define ESPCTL_MAGIC 0x45535043U
#define ESPCTL_VERSION 1
#define ESPCTL_HEADER 24
#define ESPCTL_BODY 384
#define ESPCTL_PEERS 16
#define ESPCTL_MAX_LIFE 3600
#define ESPCTL_HELLO 1
#define ESPCTL_RANDOM 2
#define ESPCTL_INSTALL 3
#define ESPCTL_DELETE 4
#define ESPCTL_STATUS 5
#define ESPCTL_OK 0
#define ESPCTL_INVALID 1
#define ESPCTL_UNAVAILABLE 2
#define ESPCTL_STALE 3
#define ESPCTL_LIMIT 4
/* HELLO request empty; response local[16], identitylen:u32, identity[64].
 * RANDOM request count:u32 (1..256); response count random bytes.
 * INSTALL request slot:u32, mode:u32 (0 tunnel/1 transport), lifetime:u32,
 * reserved:u32, local[16], remote[16], rxspi:u32, txspi:u32, rxkey[20],
 * txkey[20] (96 bytes); response generation:u64. Existing policy is pinned.
 * DELETE request slot:u32, reserved:u32, generation:u64; response empty.
 * STATUS request empty; response monotonic seconds:u64, active:u32,
 * reserved:u32, [generation:u64, inbound_activity:u64] x16 (272 bytes).
 * Generations belong to this kernel, never supplied by a client. */
static inline uint32_t espctl_get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static inline uint64_t espctl_get64(const uint8_t *p)
{ return (uint64_t)espctl_get32(p)<<32|espctl_get32(p+4); }
static inline void espctl_put32(uint8_t *p,uint32_t n)
{ p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
static inline void espctl_put64(uint8_t *p,uint64_t n)
{ espctl_put32(p,(uint32_t)(n>>32));espctl_put32(p+4,(uint32_t)n); }
#endif
