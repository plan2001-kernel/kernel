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
};
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
