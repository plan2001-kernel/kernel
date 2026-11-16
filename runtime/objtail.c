#if defined(PLAN2001_GCC) && !defined(PLAN2001_GCC_USER)
#define OBJTAIL_NATIVE_TYPES
#endif
#include "objtail.h"


static const unsigned char tailmagic[8] = { 'P', '2', 'O', 'T', 'A', 'I', 'L', '1' };
static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}
static void put64(unsigned char *p, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (56 - 8*i));
}
static uint32_t get32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t get64(const unsigned char *p)
{
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}
int objtail_pack(unsigned char out[ObjTailSize], uint64_t metadata_offset,
                 uint64_t metadata_length)
{
    if (!out || metadata_offset > UINT64_MAX - ObjTailSize ||
        metadata_length > UINT64_MAX - ObjTailSize - metadata_offset)
        return -1;
    for(unsigned i=0;i<sizeof tailmagic;i++)out[i]=tailmagic[i];
    put32(out + 8, ObjTailVersion);
    put32(out + 12, 0); /* flags: reserved, must be zero */
    put64(out + 16, metadata_offset);
    put64(out + 24, metadata_length);
    return 0;
}
int objtail_parse(const unsigned char last32[ObjTailSize], uint64_t object_size,
                  uint64_t *raw_length, uint64_t *metadata_length)
{
    uint64_t available, offset, length;
    if (!last32 || !raw_length || !metadata_length ||
        object_size < ObjTailSize ||
        get32(last32 + 8) != ObjTailVersion || get32(last32 + 12) != 0)
        return -1;
    for(unsigned i=0;i<sizeof tailmagic;i++)if(last32[i]!=tailmagic[i])return -1;
    available = object_size - ObjTailSize;
    offset = get64(last32 + 16);
    length = get64(last32 + 24);
    if (offset > available || length != available - offset) return -1;
    *raw_length = offset;
    *metadata_length = length;
    return 0;
}
