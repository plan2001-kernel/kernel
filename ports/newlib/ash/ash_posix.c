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
#include <sys/resource.h>
#include "../../unix/uxfd.h"

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

int getrlimit(int resource,struct rlimit *limit)
{
    if(!limit) { errno=EFAULT; return -1; }
    /* The one real POSIX-relevant bound exposed in this initial profile is
     * the per-process userland descriptor slot limit: 1024 descriptors.
     * Kernel budgets are controlled by SPAWN capabilities, not RLIMIT_DATA.
     */
    limit->rlim_cur=0;
    limit->rlim_max=0;
    if(resource==RLIMIT_NOFILE) {
        limit->rlim_cur=3+UXFD_SLOTS;
        limit->rlim_max=3+UXFD_SLOTS;
        return 0;
    }
    errno=ENOSYS;
    return -1;
}
int setrlimit(int resource,const struct rlimit *limit)
{
    (void)resource;
    if(!limit) { errno=EFAULT; return -1; }
    /* Untrusted POSIX limits cannot override native process budgets. */
    errno=ENOSYS;
    return -1;
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

extern int p9_fd_events(int,short);
extern int p9_process_interrupted(void);
extern long p9_native_sleep_millis(long);
extern unsigned long long _nsec(void);
int poll(struct pollfd *fds,nfds_t count,int timeout_ms)
{
    unsigned long long start=_nsec();
    if(count && !fds) { errno=EFAULT; return -1; }
    if(count>19) { errno=EINVAL; return -1; }
    (void)p9_process_interrupted();
    for(;;) {
        int ready=0;
        nfds_t i;
        for(i=0;i<count;++i) {
            fds[i].revents=0;
            if(fds[i].fd<0) continue;
            int flags=p9_fd_events(fds[i].fd,fds[i].events);
            if(flags<0) return -1;
            fds[i].revents=(short)flags;
            if(flags) ++ready;
        }
        if(ready || timeout_ms==0) return ready;
        if(p9_process_interrupted()) { errno=EINTR; return -1; }
        if(timeout_ms>0 && (_nsec()-start)/1000000ULL >= (unsigned)timeout_ms) return 0;
        (void)p9_native_sleep_millis(1);
    }
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
