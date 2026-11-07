#ifndef PLAN2001_GE_RELATIONS_R1_H
#define PLAN2001_GE_RELATIONS_R1_H
#ifdef GR_KERNEL
typedef usize size_t;
typedef uvlong uint64_t;
typedef u32int uint32_t;
#define UINT64_C(x) x##ULL
#ifndef NULL
#define NULL nil
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define GR_META_OID UINT64_C(0xfffffffffffffffb)
#define GR_MAX_OBJECTS 256
#define GR_MAX_TAGS 64
#define GR_MAX_EDGES 1024
#define GR_MAX_NAME 128
#define GR_MAX_BLOB 32768
#define GR_MAX_DATA (1024*1024)
#define GR_WORDS ((GR_MAX_OBJECTS+63)/64)

enum { GR_OK=0, GR_INVALID=-1, GR_NOTFOUND=-2, GR_LIMIT=-3,
       GR_IO=-4, GR_CORRUPT=-5 };
/* read: 0 success, 1 missing, -1 failure. NULL/0 queries content size.
 * create allocates and publishes a unique OID using GE's native allocator.
 * write stages bytes; commit publishes and synchronizes existing GE storage.
 * Implementations must not throw/longjmp through this library.
 */
typedef struct {
    void *arg;
    int (*read)(void *,uint64_t,void *,size_t,size_t *);
    int (*create)(void *,const void *,size_t,uint64_t *);
    int (*write)(void *,uint64_t,const void *,size_t);
    int (*commit)(void *);
} GrBackend;
typedef struct { uint64_t oid; char name[GR_MAX_NAME]; } GrTag;
typedef struct { uint64_t source,target; } GrEdge;
typedef struct {
    unsigned nobjects,ntags,nedges;
    uint64_t objects[GR_MAX_OBJECTS];
    GrTag tags[GR_MAX_TAGS];
    GrEdge edges[GR_MAX_EDGES];
} GrState;
typedef struct {
    GrBackend backend;
    unsigned char workspace[GR_MAX_BLOB];
    GrState state;
    int valid;
    short taghash[2*GR_MAX_TAGS];
    uint64_t postings[GR_MAX_TAGS][GR_WORDS];
} GrDB;

int gr_open(GrDB *,const GrBackend *);
int gr_create(GrDB *,const void *,size_t,uint64_t *);
int gr_get(GrDB *,uint64_t,void *,size_t,size_t *);
int gr_add(GrDB *,uint64_t,const char *,size_t);
int gr_remove(GrDB *,uint64_t,const char *,size_t);
int gr_tags(GrDB *,uint64_t,uint64_t *,size_t,size_t *);
const char *gr_tag_name(const GrDB *,uint64_t);
int gr_find(GrDB *,const char *,size_t,uint64_t *,size_t,size_t *);
/* Explicit wire codec, also usable by future non-Python clients/tools. */
int gr_encode(const GrState *,void *,size_t,size_t *);
int gr_decode(GrState *,const void *,size_t);
#endif
