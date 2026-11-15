/* Bounded per-process Unix descriptor view over an explicit UxService.
 * FDs are integers only within this address space, not inherited kernel
 * Handles. The service does not bypass GE/O authority or create a VFS.
 */
#ifndef PLAN2001_UXFD_H
#define PLAN2001_UXFD_H
#include "uxservice.h"
#define UXFD_SLOTS 16
#define UXFD_READ 1u
#define UXFD_WRITE 2u
#define UXFD_CREATE 4u
#define UXFD_TRUNC 8u
#define UXFD_APPEND 16u
typedef struct {
    char path[PVD_PATH_MAX];
    unsigned int used, rights, position;
} UxFdSlot;
typedef struct {
    UxService *service;
    UxFdSlot slots[UXFD_SLOTS];
} UxFdTable;
void uxfd_init(UxFdTable *table, UxService *service);
int uxfd_open(UxFdTable *table, const char *path, unsigned int flags);
long uxfd_read(UxFdTable *table, int fd, void *dst, unsigned int n);
long uxfd_write(UxFdTable *table, int fd, const void *src, unsigned int n);
long long uxfd_seek(UxFdTable *table, int fd, long long delta, int whence);
int uxfd_info(UxFdTable *table, int fd, unsigned int *kind,
              unsigned int *length);
int uxfd_close(UxFdTable *table, int fd);
int uxfd_unlink(UxFdTable *table, const char *path);
#endif
