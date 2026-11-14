/* Honest POSIX boundary for ash's non-file primitives.
 * Newlib supplies C runtime and header types. These adapters preserve the
 * native ObjectSpace authority model. Unsupported POSIX process/tty/mmap
 * semantics fail with errors instead of returning false success.
 */
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <poll.h>

void *mempcpy(void *restrict dest,const void *restrict src,size_t n)
{
    /* Copy even when the source has no NUL; return the tail pointer. */
    return (unsigned char *)memcpy(dest,src,n)+n;
}
char *strchrnul(const char *s,int ch)
{
    unsigned char target=(unsigned char)ch;
    while(*s && (unsigned char)*s!=target) ++s;
    return (char *)s;
}

int uname(struct utsname *name)
{
    if(!name) { errno=EFAULT; return -1; }
    memset(name,0,sizeof(*name));
    /* Descriptive, deliberately not inferred from a raw object identity. */
    strcpy(name->sysname,"Plan2001");
    strcpy(name->nodename,"plan2001");
    strcpy(name->release,"userland-v1");
    strcpy(name->version,"native-ObjectSpace");
    strcpy(name->machine,"x86_64");
    return 0;
}

int poll(struct pollfd *fds,nfds_t count,int timeout_ms)
{
    (void)timeout_ms;
    if(count && !fds) { errno=EFAULT; return -1; }
    errno=ENOSYS; /* no bounded multi-FD event dispatcher yet */
    return -1;
}
int tcgetattr(int fd,struct termios *attr)
{
    (void)fd;
    if(!attr) { errno=EFAULT; return -1; }
    errno=ENOTTY; /* UXPIPE F5 is not a POSIX terminal */
    return -1;
}
int tcsetattr(int fd,int action,const struct termios *attr)
{
    (void)fd; (void)action;
    if(!attr) { errno=EFAULT; return -1; }
    errno=ENOTTY;
    return -1;
}
int tcdrain(int fd)
{
    (void)fd; errno=ENOTTY; return -1;
}
int tcflush(int fd,int selector)
{
    (void)fd; (void)selector; errno=ENOTTY; return -1;
}
void *mmap(void *addr,size_t length,int prot,int flags,int fd,off_t offset)
{
    (void)addr;(void)length;(void)prot;(void)flags;(void)fd;(void)offset;
    errno=ENOSYS; /* native region maps require explicit Handle grants */
    return MAP_FAILED;
}
int munmap(void *addr,size_t length)
{
    (void)addr;(void)length;errno=ENOSYS;return -1;
}
int mprotect(void *addr,size_t length,int prot)
{
    (void)addr;(void)length;(void)prot;errno=ENOSYS;return -1;
}
