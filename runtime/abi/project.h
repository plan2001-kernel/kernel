/* Durable ObjectSpace operations. Names are local bytes, never paths.
 * Identity and saved rights are descriptive; a live Handle authorizes each call. */
#ifndef PLAN2001_PROJECT_ABI_H
#define PLAN2001_PROJECT_ABI_H
#define PROJECT_MAGIC 0x503250524f4a3031ULL
#define PROJECT_NAME_MAX 255u
enum { ProjectRead=1, ProjectCreate=2, ProjectLink=3, ProjectUnlink=4,
       ProjectRename=5, ProjectPut=6, ProjectResolve=7, ProjectAttenuate=8, ProjectDetached=10, ProjectLinks=11, ProjectMetadata=12 };
typedef struct ObjProjectOpen {
    int volume;
    unsigned int rights;
    unsigned long long oid; /* zero creates a root with supplied payload */
    const void *data;
    unsigned long long length;
} ObjProjectOpen;
typedef struct ObjProjectReq {
    unsigned int operation, kind, rights, flags;
    int other;
    unsigned int relation;
    unsigned long long binding, replace, epoch;
    const void *data;
    unsigned long long length;
    const void *metadata;
    unsigned long long metadata_length;
    const void *other_metadata;
    unsigned long long other_metadata_length, other_epoch;
    char name[256];
} ObjProjectReq;
/* Raw namespace: BE magic, epoch, BindingID high-water, count (4*uint64).
 * Each record: BE binding, OID, rights, relation, kind, namelen; name bytes.
 * Followed by BE FNV1a64 over header and records, opaque metadata and footer.
 * All targets currently share the namespace's authorized volume. */
#endif
