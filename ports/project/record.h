/* Durable namespace records. IDs describe identity, never authority. */
#ifndef PLAN2001_PROJECT_RECORD_H
#define PLAN2001_PROJECT_RECORD_H
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned char volume[16]; uint64_t oid; } ProjectId;
typedef struct {
    uint64_t binding, oid;
    uint32_t rights, relation, kind;
    const unsigned char *name;
    size_t name_length;
} ProjectEntry;
typedef struct {
    uint64_t epoch, next_binding;
    size_t count;
    const unsigned char *records;
    size_t records_length;
} ProjectRecord;
/* Big endian, sparse records, no pointers or capabilities on disk. */
int project_record_open(ProjectRecord *, const void *, size_t);
int project_record_entry(const ProjectRecord *, size_t, ProjectEntry *);
#endif
