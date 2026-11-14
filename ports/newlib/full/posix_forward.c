/* Newlib 4.6.0 _r wrappers on the AMD64 unknown-elf target are built
 * against public POSIX symbol names when supplied syscalls are disabled.
 * Forward these to the Plan2001 native underscore primitives. These
 * entrypoints are REAL, not fake-success stubs.
 *
 * errno is supplied by upstream Newlib's reentrancy runtime.
 */
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

extern int _read(int, char *, int);
extern int _write(int, const char *, int);
extern int _close(int);
extern off_t _lseek(int, off_t, int);
extern void *_sbrk(ptrdiff_t);
extern int _fstat(int, struct stat *);
extern int _isatty(int);
extern int _getpid(void);
extern int _kill(int, int);
extern int _open(const char *, int, ...);
extern int _stat(const char *, struct stat *);
extern int _lstat(const char *, struct stat *);
extern int _unlink(const char *);
extern int _link(const char *, const char *);
extern int _chdir(const char *);
extern int _mkdir(const char *, mode_t);
extern int _rename(const char *, const char *);
extern int _access(const char *, int);

/* Newlib's _open_r/_stat_r wrappers are not public open/stat when
 * --disable-newlib-supplied-syscalls is selected. Provide real POSIX
 * forwarders so ash does not accidentally resolve host-Linux functions.
 */
int open(const char *path, int flags, ...)
{
    mode_t mode=0;
    if(flags & O_CREAT) {
        va_list ap;
        va_start(ap,flags);
        mode=(mode_t)va_arg(ap,int);
        va_end(ap);
    }
    return _open(path,flags,mode);
}
int stat(const char *path, struct stat *st)
{
    return _stat(path,st);
}
int lstat(const char *path, struct stat *st)
{
    return _lstat(path,st);
}
int unlink(const char *path)
{
    return _unlink(path);
}
int link(const char *src, const char *dst)
{
    return _link(src,dst);
}
int chdir(const char *path)
{
    return _chdir(path);
}
int mkdir(const char *path, mode_t mode)
{
    return _mkdir(path,mode);
}
int rename(const char *src, const char *dst)
{
    return _rename(src,dst);
}
int access(const char *path, int mode)
{
    return _access(path,mode);
}

int read(int fd, void *buf, size_t count)
{
    if(count > INT_MAX) { errno = EINVAL; return -1; }
    return _read(fd, (char *)buf, (int)count);
}

int write(int fd, const void *buf, size_t count)
{
    if(count > INT_MAX) { errno = EINVAL; return -1; }
    return _write(fd, (const char *)buf, (int)count);
}

int close(int fd)
{
    return _close(fd);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return _lseek(fd, offset, whence);
}

void *sbrk(ptrdiff_t change)
{
    return _sbrk(change);
}

int fstat(int fd, struct stat *st)
{
    return _fstat(fd, st);
}

int isatty(int fd)
{
    return _isatty(fd);
}

pid_t getpid(void)
{
    return (pid_t)_getpid();
}

int kill(pid_t pid, int sig)
{
    return _kill((int)pid, sig);
}

/* Plan2001 SLEEP(17) takes milliseconds, while POSIX sleep() takes seconds.
 * The generated native sleep stub is renamed to p9_native_sleep_millis
 * during link; do not export it with the POSIX name and wrong units.
 */
extern long p9_native_sleep_millis(long);

unsigned int sleep(unsigned int seconds)
{
    unsigned int remaining = seconds;
    while(remaining != 0) {
        /* Bound the milliseconds argument and avoid overflow. */
        unsigned int chunk = remaining > 2147483u ? 2147483u : remaining;
        if(p9_native_sleep_millis((long)chunk * 1000L) < 0) {
            errno = EINTR;
            return remaining;
        }
        remaining -= chunk;
    }
    return 0;
}

/* Newlib's public time adapter uses the direct Plan2001 _NSEC(53)
 * implementation, never Linux's host gettimeofday system call.
 */
extern int _gettimeofday(struct timeval *, void *);

int gettimeofday(struct timeval *tv, void *timezone)
{
    return _gettimeofday(tv, timezone);
}
