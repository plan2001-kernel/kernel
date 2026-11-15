/* Output helpers needed by upstream multicall; UXPIPE has no terminal width.
 * Upstream's help formatter accepts a fallback width independently of tty I/O.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
extern ssize_t full_write(int, const void *, size_t);
ssize_t full_write1_str(const char *s) { return full_write(1, s, strlen(s)); }
ssize_t full_write2_str(const char *s) { return full_write(2, s, strlen(s)); }
int get_terminal_width(int fd)
{
    const char *value = getenv("COLUMNS");
    char *end;
    long width;
    (void)fd;
    if (!value || !*value) return 80;
    width = strtol(value, &end, 10);
    return !*end && width > 1 && width < 10000 ? (int)width : 80;
}

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
extern void xfunc_die(void);
/* UXFS v1 cannot create or follow symlinks. Fail explicitly. */
int symlink(const char *target, const char *path)
{
    (void)target; (void)path; errno = ENOSYS; return -1;
}
ssize_t readlink(const char *path, char *buf, size_t capacity)
{
    (void)path; (void)buf; (void)capacity; errno = ENOSYS; return -1;
}
/* POSIX libgen: in-place, handles empty input, trailing slash and root. */
char *dirname(char *path)
{
    static char dot[] = ".";
    size_t n;
    if (!path || !*path) return dot;
    n = strlen(path);
    while (n > 1 && path[n-1] == '/') path[--n] = 0;
    while (n && path[n-1] != '/') --n;
    if (!n) return dot;
    while (n > 1 && path[n-1] == '/') --n;
    path[n] = 0;
    return path;
}

#include <fcntl.h>
#include <dirent.h>
extern void bb_simple_perror_msg(const char *);
void exit_SUCCESS(void) { exit(0); }

/* Native capability/UXFS profile has no device creation or Unix ownership
 * mutation. These paths fail explicitly rather than faking metadata changes. */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
int mknod(const char *path,mode_t mode,dev_t device)
{ (void)path; (void)mode; (void)device; errno=ENOSYS; return -1; }
int chmod(const char *path,mode_t mode)
{ (void)path; (void)mode; errno=ENOSYS; return -1; }
int fchmod(int fd,mode_t mode)
{ (void)fd; (void)mode; errno=ENOSYS; return -1; }
int chown(const char *path,uid_t uid,gid_t gid)
{ (void)path; (void)uid; (void)gid; errno=ENOSYS; return -1; }
int lchown(const char *path,uid_t uid,gid_t gid)
{ return chown(path,uid,gid); }
int fchown(int fd,uid_t uid,gid_t gid)
{ (void)fd; (void)uid; (void)gid; errno=ENOSYS; return -1; }
int utimes(const char *path,const struct timeval times[2])
{ (void)path; (void)times; errno=ENOSYS; return -1; }
