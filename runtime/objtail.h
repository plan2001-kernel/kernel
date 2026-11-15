/* Plan2001 portable payload trailer v1. Data always starts at byte zero.
 * Pure byte framing: metadata content is opaque and cannot grant authority.
 */
#ifndef PLAN2001_OBJTAIL_H
#define PLAN2001_OBJTAIL_H
#ifdef OBJTAIL_NATIVE_TYPES
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;
#define UINT64_MAX (~(uint64_t)0)
#else
#include <stdint.h>
#endif

enum { ObjTailSize = 32, ObjTailVersion = 1 };
/* out = magic[8] | version:u32be | flags:u32be |
 *       metadata_offset:u64be | metadata_length:u64be. */
int objtail_pack(unsigned char out[ObjTailSize], uint64_t metadata_offset,
                 uint64_t metadata_length);
/* object_size is the length of the entire logical GE/O object; last32 is
 * exactly its last 32 bytes. Metadata is adjacent to the trailer, without
 * padding or gaps. Returns -1 on invalid input, otherwise 0. */
int objtail_parse(const unsigned char last32[ObjTailSize], uint64_t object_size,
                  uint64_t *raw_length, uint64_t *metadata_length);
#endif
