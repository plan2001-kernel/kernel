/* ObjectSpace ABI v1: no directory paths, no mandatory parent, no Pgrp.
 * The object ABI uses version 2 for these additional operations.
 * Labels are local nonempty NUL-terminated byte sequences, not paths.
 * Multiple bindings may have identical labels: BindingID disambiguates.
 * relation is an opaque application-defined classification; no kernel
 * authority is derived from it. The binding's rights are an attenuation
 * of the source capability and never an owner-identity check.
 */
#ifndef PLAN2001_OBJECTSPACE_ABI_H
#define PLAN2001_OBJECTSPACE_ABI_H
enum {
    ObjSpaceLabelMax = 48,
    ObjSpaceBindingMax = 64,
    ObjSpacePageMax = 8,
    ObjSpaceRelationNames = 0,
    ObjSpaceRelationContains = 1,
    ObjSpaceRelationImports = 2,
    ObjSpaceRelationUses = 3,
    ObjSpaceFlagStale = 1,
    ObjSpaceFlagRevoked = 2, /* revoked binding derivation */
};
/* A graph snapshot is a single GE/O object: all binding changes are
 * published by one GE transaction. The caller must already have a valid
 * VkindGeVolume capability; OID alone does not authorize restoration.
 * SAVE: OBJINVOKE(space, ObjOpSpaceSave, in=&req), aux=GE version.
 * LOAD: OBJOPEN(ObjOpenSpaceGraph, &req, sizeof req, 0, nil).
 * The GE/O object must already exist (e.g. OBJCREATE(ObjKindGe)); LOAD
 * never reams a volume or invents a privileged grant. */
typedef struct ObjSpaceDiskReq {
    int volume;                       /* authorized volume Handle */
    unsigned int reserved;            /* must be zero */
    unsigned long long oid;           /* existing graph snapshot OID */
} ObjSpaceDiskReq;

typedef struct ObjSpaceBindReq {
    int target;                       /* process-local capability */
    unsigned int rights;              /* zero => source handle rights */
    unsigned int relation;            /* uninterpreted semantic tag */
    unsigned int flags;               /* must be zero on insertion */
    char label[ObjSpaceLabelMax];     /* NUL terminated, no pathname parsing */
} ObjSpaceBindReq;
typedef struct ObjSpaceEntry {
    unsigned long long binding_id;    /* stable during space lifetime */
    unsigned long long incarnation;
    unsigned long long space;
    unsigned long long oid;
    unsigned int kind;
    unsigned int rights;
    unsigned int relation;
    unsigned int flags;               /* STALE: target no longer live */
    char label[ObjSpaceLabelMax];
} ObjSpaceEntry;
#endif
