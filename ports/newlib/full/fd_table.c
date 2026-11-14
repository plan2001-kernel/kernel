/* Single-threaded Unix descriptors, distinct from native capability Handles.
 * Duplicates share a backend open description and offset. Descriptor flags
 * are private to each descriptor; only the last close releases the backend.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include "fd_backend.h"
#include "plan2001_full.h"
#include "../../unix/uxfd.h"
#include "../../unix/uxpipe.h"

#define FD_COUNT (3 + UXFD_SLOTS)
typedef struct {
    int ends, handles[2];
    uintptr_t maps[2];
    UxPipe reader, writer;
} PipePair;
typedef struct { int refs, backend, flags; PipePair *pipe; } Description;
typedef struct { Description *description; int flags; } Descriptor;
static Description descriptions[FD_COUNT];
static Descriptor descriptors[FD_COUNT];
static int initialized;
static PipePair pipes[FD_COUNT/2];

static void initialize(void)
{
    int i;
    if(initialized) return;
    for(i=0;i<3;++i) {
        descriptions[i].refs=1;
        descriptions[i].backend=i;
        descriptions[i].flags=i ? O_WRONLY : O_RDONLY;
        descriptors[i].description=&descriptions[i];
    }
    initialized=1;
}
static Description *lookup(int fd)
{
    initialize();
    if(fd<0 || fd>=FD_COUNT || !descriptors[fd].description) {
        errno=EBADF;
        return 0;
    }
    return descriptors[fd].description;
}
static int available(int minimum)
{
    int i;
    for(i=minimum;i<FD_COUNT;++i)
        if(!descriptors[i].description) return i;
    errno=EMFILE;
    return -1;
}
int _open(const char *path,int flags,...)
{
    int fd, i, backend;
    mode_t mode=0;
    initialize();
    if(flags&O_NONBLOCK) { errno=ENOSYS; return -1; }
    if(flags&O_CREAT) {
        va_list args;
        va_start(args,flags);
        mode=(mode_t)va_arg(args,int);
        va_end(args);
    }
    fd=available(0);
    if(fd<0) return -1;
    for(i=0;i<FD_COUNT;++i) if(!descriptions[i].refs) break;
    if(i==FD_COUNT) { errno=ENFILE; return -1; }
    backend=p9_backend_open(path,flags,mode);
    if(backend<0) return -1;
    descriptions[i].refs=1;
    descriptions[i].backend=backend;
    descriptions[i].flags=flags & (O_ACCMODE|O_APPEND|O_NONBLOCK);
    descriptions[i].pipe=0;
    descriptors[fd].description=&descriptions[i];
    descriptors[fd].flags=0;
#ifdef O_CLOEXEC
    if(flags&O_CLOEXEC) descriptors[fd].flags=FD_CLOEXEC;
#endif
    return fd;
}
int _close(int fd)
{
    Description *d=lookup(fd);
    if(!d) return -1;
    descriptors[fd].description=0;
    descriptors[fd].flags=0;
    if(--d->refs) return 0;
    if(d->pipe) {
        PipePair *p=d->pipe;
        int result=uxpipe_finish(d->flags==O_RDONLY ? &p->reader : &p->writer);
        if(!--p->ends) {
            int i;
            for(i=0;i<2;++i) {
                (void)p9_full_region_unmap(p->maps[i]);
                (void)p9_full_region_close(p->handles[i]);
            }
        }
        d->pipe=0;
        if(result<0) { errno=EIO; return -1; }
        return 0;
    }
    return p9_backend_close(d->backend);
}
int _read(int fd,char *buf,int count)
{
    Description *d=lookup(fd);
    if(!d) return -1;
    if((d->flags&O_ACCMODE)==O_WRONLY) { errno=EBADF; return -1; }
    if(d->pipe) {
        long n;
        if(count<0 || (!buf && count)) { errno=EINVAL; return -1; }
        n=uxpipe_read(&d->pipe->reader,buf,
                     (unsigned)count>UXPIPE_MAX_IO ? UXPIPE_MAX_IO : (unsigned)count,-1);
        if(n<0) { errno=EIO; return -1; }
        return (int)n;
    }
    return p9_backend_read(d->backend,buf,count);
}
int _write(int fd,const char *buf,int count)
{
    Description *d=lookup(fd);
    if(!d) return -1;
    if((d->flags&O_ACCMODE)==O_RDONLY) { errno=EBADF; return -1; }
    if(d->pipe) {
        long n;
        if(count<0 || (!buf && count)) { errno=EINVAL; return -1; }
        if(__atomic_load_n(&d->pipe->reader.ack->closed,__ATOMIC_ACQUIRE)) {
            errno=EPIPE;
            return -1;
        }
        n=uxpipe_write(&d->pipe->writer,buf,
                      (unsigned)count>UXPIPE_MAX_IO ? UXPIPE_MAX_IO : (unsigned)count,-1);
        if(n<0) { errno=EIO; return -1; }
        return (int)n;
    }
    return p9_backend_write(d->backend,buf,count);
}
off_t _lseek(int fd,off_t offset,int whence)
{
    Description *d=lookup(fd);
    if(d && d->pipe) { errno=ESPIPE; return (off_t)-1; }
    return d ? p9_backend_lseek(d->backend,offset,whence) : (off_t)-1;
}
int _fstat(int fd,struct stat *st)
{
    Description *d=lookup(fd);
    if(d && d->pipe) {
        if(!st) { errno=EFAULT; return -1; }
        memset(st,0,sizeof(*st));
        st->st_mode=S_IFIFO|0600;
        st->st_nlink=1;
        st->st_blksize=UXPIPE_CAPACITY;
        return 0;
    }
    return d ? p9_backend_fstat(d->backend,st) : -1;
}
int _isatty(int fd)
{
    Description *d=lookup(fd);
    if(d && d->pipe) { errno=ENOTTY; return 0; }
    return d ? p9_backend_isatty(d->backend) : 0;
}
int dup2(int oldfd,int newfd)
{
    Description *d=lookup(oldfd);
    int saved;
    if(!d) return -1;
    if(newfd<0 || newfd>=FD_COUNT) { errno=EBADF; return -1; }
    if(oldfd==newfd) return newfd;
    /* Closing newfd must not destroy d when both descriptors alias it. */
    ++d->refs;
    saved=errno;
    if(descriptors[newfd].description) (void)_close(newfd);
    errno=saved;
    descriptors[newfd].description=d;
    descriptors[newfd].flags=0;
    return newfd;
}
int fcntl(int fd,int command,...)
{
    Description *d=lookup(fd);
    va_list args;
    int value, target;
    if(!d) return -1;
    if(command==F_GETFD) return descriptors[fd].flags;
    if(command==F_GETFL) return d->flags;
    switch(command) {
    case F_DUPFD:
#ifdef F_DUPFD_CLOEXEC
    case F_DUPFD_CLOEXEC:
#endif
    case F_SETFD:
    case F_SETFL:
        va_start(args,command);
        value=va_arg(args,int);
        va_end(args);
        break;
    default: errno=EINVAL; return -1;
    }
    if(command==F_SETFD) {
        if(value&~FD_CLOEXEC) { errno=EINVAL; return -1; }
        descriptors[fd].flags=value;
        return 0;
    }
    if(command==F_SETFL) {
        /* Backends have no mutable/nonblocking status operation yet. */
        if((value&(O_APPEND|O_NONBLOCK)) != (d->flags&(O_APPEND|O_NONBLOCK))) {
            errno=ENOSYS;
            return -1;
        }
        return 0;
    }
    if(value<0 || value>=FD_COUNT) { errno=EINVAL; return -1; }
    target=available(value);
    if(target<0) return -1;
    if(dup2(fd,target)<0) return -1;
#ifdef F_DUPFD_CLOEXEC
    if(command==F_DUPFD_CLOEXEC) descriptors[target].flags=FD_CLOEXEC;
#endif
    return target;
}
int dup(int fd)
{
    return fcntl(fd,F_DUPFD,0);
}

void p9_full_close_descriptors(void)
{
    int i;
    initialize();
    for(i=0;i<FD_COUNT;++i)
        if(descriptors[i].description) (void)_close(i);
}

int pipe(int result[2])
{
    int fds[2], objects[2], nf=0, no=0, i;
    PipePair *p=0;
    initialize();
    if(!result) { errno=EFAULT; return -1; }
    for(i=0;i<FD_COUNT;++i) {
        if(nf<2 && !descriptors[i].description) fds[nf++]=i;
        if(no<2 && !descriptions[i].refs) objects[no++]=i;
    }
    if(nf<2 || no<2) { errno=EMFILE; return -1; }
    for(i=0;i<FD_COUNT/2;++i) if(!pipes[i].ends) { p=&pipes[i]; break; }
    if(!p) { errno=ENFILE; return -1; }
    memset(p,0,sizeof(*p));
    for(i=0;i<2;++i) p->handles[i]=-1;
    for(i=0;i<2;++i) {
        long handle=p9_full_region_create(UXPIPE_PAGE);
        long base;
        if(handle<=0) goto fail;
        p->handles[i]=(int)handle;
        base=p9_full_region_map((int)handle,0);
        if(base<=0) goto fail;
        p->maps[i]=(uintptr_t)base;
    }
    if(uxpipe_format((UxPipeData *)p->maps[0],(UxPipeAck *)p->maps[1]) ||
       uxpipe_bind(&p->reader,(UxPipeData *)p->maps[0],(UxPipeAck *)p->maps[1],
                   p->maps[0],p->maps[1],UXPIPE_READER) ||
       uxpipe_bind(&p->writer,(UxPipeData *)p->maps[0],(UxPipeAck *)p->maps[1],
                   p->maps[0],p->maps[1],UXPIPE_WRITER)) goto fail;
    p->ends=2;
    for(i=0;i<2;++i) {
        Description *d=&descriptions[objects[i]];
        d->refs=1;
        d->backend=-1;
        d->flags=i ? O_WRONLY : O_RDONLY;
        d->pipe=p;
        descriptors[fds[i]].description=d;
        descriptors[fds[i]].flags=0;
        result[i]=fds[i];
    }
    return 0;
fail:
    for(i=0;i<2;++i) {
        if(p->maps[i]) (void)p9_full_region_unmap(p->maps[i]);
        if(p->handles[i]>0) (void)p9_full_region_close(p->handles[i]);
    }
    errno=ENOMEM;
    return -1;
}
