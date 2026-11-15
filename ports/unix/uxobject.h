/* Unix producer profile over opaque native object framing. No field grants
 * authority. Unknown times have clear known bits, never invented timestamps. */
#ifndef PLAN2001_UXOBJECT_H
#define PLAN2001_UXOBJECT_H
#define UXMETA_ATIME 1u
#define UXMETA_MTIME 2u
#define UXMETA_CTIME 4u
#define UXMETA_BIRTH 8u
#define UXMETA_ARENA (1024u*1024u)
typedef struct {
    char name[256], media_type[64];
    long long atime_ns,mtime_ns,ctime_ns,birth_ns;
    unsigned long long rdev;
    unsigned mode,uid,gid,known;
} UxMeta;
void uxmeta_init(UxMeta *,const char *,unsigned,unsigned long long);
/* Metadata is CBOR for this producer, opaque otherwise. Original metadata
 * bytes must be retained and supplied on update so unknown keys survive. */
int uxobject_metadata(const UxMeta *,const unsigned char *,unsigned,
                      unsigned char *,unsigned,unsigned *);
int uxobject_decode(const unsigned char *,unsigned,UxMeta *);
int uxobject_split(const void *,unsigned,unsigned *,unsigned *);
int uxobject_footer(unsigned char *,unsigned,unsigned);
#endif
