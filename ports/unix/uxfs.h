/* Bounded Unix path projection and GE/O-backed file inventory.
 * Names never confer authority: persistence requires an explicitly supplied
 * authorized volume backend, not an OID lookup without a capability.
 */
#ifndef PLAN2001_UXFS_H
#define PLAN2001_UXFS_H
#include "pvd.h"
#define UXFS_NODES 64u
#define UXFS_DATA_MAX 1024u
#define UXFS_DIR 1u
#define UXFS_FILE 2u
#define UXFS_MAGIC 0x3153465855323030ULL /* "002UXFS1" ABI-local marker */
#define UXFS_VERSION 1u

typedef unsigned long long UxOid;
typedef struct {
    char path[PVD_PATH_MAX];
    UxOid oid;                 /* identity only; never authorization */
    unsigned int length;
    unsigned int kind;        /* UXFS_DIR or UXFS_FILE; 0 means unused */
} UxNode;
typedef struct {
    Pvd *view;
    int dirty;                 /* unsaved file metadata/content mutations */
    unsigned char modified[UXFS_NODES]; /* file content needs fresh OID */
    UxNode nodes[UXFS_NODES];
    unsigned char data[UXFS_NODES][UXFS_DATA_MAX];
} UxFs;

typedef struct {
    void *context;            /* must hold authorized volume Handle */
    int (*create)(void *, UxOid *oid);
    int (*put)(void *, UxOid oid, const void *, unsigned int);
    int (*get)(void *, UxOid oid, void *, unsigned int, unsigned int *);
    int (*commit)(void *);
} UxStore;

typedef int (*UxEmit)(void *, const void *, unsigned int);
void uxfs_init(UxFs *fs, Pvd *view);
int uxfs_path(const UxFs *fs, const char *path, char out[PVD_PATH_MAX]);
int uxfs_mkdir(UxFs *fs, const char *path);
int uxfs_write(UxFs *fs, const char *path, const void *buf,
               unsigned int size, int append);
int uxfs_info(UxFs *fs, const char *path, unsigned int *kind,
              unsigned int *length);
int uxfs_read(UxFs *fs, const char *path, void *buf,
              unsigned int capacity, unsigned int *size);
int uxfs_copy(UxFs *fs, const char *src, const char *dest);
int uxfs_move(UxFs *fs, const char *src, const char *dest);
int uxfs_remove(UxFs *fs, const char *path);
int uxfs_list(UxFs *fs, const char *path, UxEmit emit, void *ctx);
int uxfs_save(UxFs *fs, const UxStore *store, UxOid manifest);
int uxfs_restore(UxFs *fs, const UxStore *store, UxOid manifest);
#endif
