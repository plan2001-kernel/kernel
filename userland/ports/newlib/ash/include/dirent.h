/* Plan2001 POSIX directory projection, backed by explicit UXFS service.
 * Installed Newlib x86_64-unknown-elf sys/dirent.h deliberately errors.
 * This v1 API enumerates an immutable userspace snapshot: no kernel VFS.
 */
#ifndef PLAN2001_NEWLIB_DIRENT_H
#define PLAN2001_NEWLIB_DIRENT_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#endif
struct dirent {
    ino_t d_ino;       /* view-local ordinal, not a capability or GE/O OID */
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];  /* max native label: 47 bytes + NUL */
};
typedef struct p2001_dir DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
void rewinddir(DIR *dir);
long telldir(DIR *dir);
void seekdir(DIR *dir, long offset);
int dirfd(DIR *dir); /* ENOSYS: no kernel directory FD exists */
#ifdef __cplusplus
}
#endif
#endif
