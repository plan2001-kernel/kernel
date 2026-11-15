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
char *xasprintf(const char *format, ...)
{
    char *result;
    va_list args;
    va_start(args, format);
    int n = vasprintf(&result, format, args);
    va_end(args);
    if (n < 0) xfunc_die();
    return result;
}
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
int open_or_warn(const char *path, int flags)
{
    int fd = open(path, flags);
    if (fd < 0) bb_simple_perror_msg(path);
    return fd;
}
DIR *warn_opendir(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) bb_simple_perror_msg(path);
    return dir;
}
void fputs_stdout(const char *text) { fputs(text, stdout); }

void exit_SUCCESS(void) { exit(0); }
void bb_die_memory_exhausted(void)
{
    fputs("memory exhausted\n", stderr);
    xfunc_die();
}
FILE *xfopen(const char *path, const char *mode)
{
    FILE *fp = fopen(path, mode);
    if (!fp) { bb_simple_perror_msg(path); xfunc_die(); }
    return fp;
}
