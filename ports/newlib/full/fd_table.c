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
#include <signal.h>
#include <poll.h>
#include "fd_backend.h"
#include "plan2001_full.h"
#include "../../unix/uxfd.h"
#include "../../unix/uxpipe.h"
#include "process.h"
#include "project_fs.h"
#include "vobj.h"

int _close(int);
#define FD_COUNT (3 + UXFD_SLOTS)
typedef struct {
    int ends, handles[2], transient;
    uintptr_t maps[2];
    UxPipe reader, writer;
} PipePair;
typedef struct { int refs, backend, flags; PipePair *pipe; } Description;
typedef struct { Description *description; int flags; } Descriptor;
static Description descriptions[FD_COUNT];
static Descriptor descriptors[FD_COUNT];
static int initialized;
static PipePair pipes[FD_COUNT/2];
extern UxFdTable *p9_backend_files(void);
extern long p9_native_sleep_millis(long);
extern long region_wait(unsigned long long,unsigned long long,unsigned long long,long long);

static int wait_peer(PipePair *p,int flags,unsigned long long expected)
{
    int peer=flags==O_RDONLY ? 0 : 1;
    long base=p9_full_region_map(p->handles[peer],1);
    unsigned long long closed;
    if(base<=0) { errno=EIO; return -1; }
    closed=peer ? ((UxPipeAck *)(uintptr_t)base)->closed : ((UxPipeData *)(uintptr_t)base)->closed;
    if(!closed) (void)region_wait((unsigned long long)base,
        peer ? UXPIPE_ACK_OFFSET : UXPIPE_PUB_OFFSET,expected,10);
    (void)p9_full_region_unmap((uintptr_t)base);
    if(p9_process_interrupted()) { errno=EINTR; return -1; }
    return 0;
}

static UxPipe *acquire(PipePair *p,int flags)
{
    int own=flags==O_RDONLY ? 1 : 0;
    long mapped;
    unsigned tries=0;
    UxPipe *stream=own ? &p->reader : &p->writer;
    if(!p->transient) return stream;
    do {
        mapped=p9_full_region_map(p->handles[own],0);
        if(mapped>0) break;
        if(p9_process_interrupted()) { errno=EINTR; return NULL; }
        (void)p9_native_sleep_millis(1);
    } while(++tries<5000);
    if(mapped<=0) { errno=EBUSY; return NULL; }
    p->maps[own]=(uintptr_t)mapped;
    mapped=p9_full_region_map(p->handles[1-own],1);
    if(mapped<=0) {
        (void)p9_full_region_unmap(p->maps[own]); p->maps[own]=0;
        errno=EIO; return NULL;
    }
    p->maps[1-own]=(uintptr_t)mapped;
    if(uxpipe_bind(stream,(UxPipeData *)p->maps[0],(UxPipeAck *)p->maps[1],
                   p->maps[0],p->maps[1],own ? UXPIPE_READER : UXPIPE_WRITER)) {
        (void)p9_full_region_unmap(p->maps[0]);
        (void)p9_full_region_unmap(p->maps[1]);
        p->maps[0]=p->maps[1]=0;
        errno=EIO; return NULL;
    }
    stream->transient=1;
    return stream;
}
static void release(PipePair *p)
{
    unsigned i;
    if(!p->transient) return;
    for(i=0;i<2;++i) if(p->maps[i]) {
        (void)p9_full_region_unmap(p->maps[i]); p->maps[i]=0;
    }
}
static int endpoint_refs(Description *d,int change)
{
    UxPipe *stream=acquire(d->pipe,d->flags);
    unsigned long long *refs;
    if(!stream) return -1;
    refs=d->flags==O_RDONLY ? &stream->ack->users : &stream->data->users;
    if(!*refs || (change>0 && *refs==~0ULL)) {
        release(d->pipe); errno=EIO; return -1;
    }
    if(change>0) ++*refs;
    else if(!--*refs) (void)uxpipe_finish(stream);
    release(d->pipe);
    return 0;
}

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
static int _open_impl(const char *path,int flags,...)
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
static int _close_impl(int fd)
{
    Description *d=lookup(fd);
    if(!d) return -1;
    descriptors[fd].description=0;
    descriptors[fd].flags=0;
    if(--d->refs) return 0;
    if(d->pipe) {
        PipePair *p=d->pipe;
        int result=endpoint_refs(d,-1);
        if(!--p->ends) {
            int i;
            for(i=0;i<2;++i) {
                if(p->maps[i]) (void)p9_full_region_unmap(p->maps[i]);
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
    (void)p9_process_interrupted();
    Description *d=lookup(fd);
    if(!d) return -1;
    if((d->flags&O_ACCMODE)==O_WRONLY) { errno=EBADF; return -1; }
    if(d->pipe) {
        long n;
        UxPipe *stream;
        if(count<0 || (!buf && count)) { errno=EINVAL; return -1; }
retry_read:
        stream=acquire(d->pipe,d->flags);
        if(!stream) return -1;
        if(d->pipe->transient && count &&
           stream->data->produced==stream->ack->consumed && !stream->data->closed) {
            unsigned long long expected=stream->data->produced;
            release(d->pipe);
            if(wait_peer(d->pipe,d->flags,expected)<0) return -1;
            goto retry_read;
        }
        n=uxpipe_read(stream,buf,
                     (unsigned)count>UXPIPE_MAX_IO ? UXPIPE_MAX_IO : (unsigned)count,
                     d->pipe->transient ? 0 : -1);
        release(d->pipe);
        if(n<0) { errno=p9_process_interrupted() ? EINTR : EIO; return -1; }
        return (int)n;
    }
    return p9_backend_read(d->backend,buf,count);
}
int _write(int fd,const char *buf,int count)
{
    (void)p9_process_interrupted();
    Description *d=lookup(fd);
    if(!d) return -1;
    if((d->flags&O_ACCMODE)==O_RDONLY) { errno=EBADF; return -1; }
    if(d->pipe) {
        long n;
        UxPipe *stream;
        if(count<0 || (!buf && count)) { errno=EINVAL; return -1; }
retry_write:
        stream=acquire(d->pipe,d->flags);
        if(!stream) return -1;
        if(__atomic_load_n(&stream->ack->closed,__ATOMIC_ACQUIRE)) {
            release(d->pipe);
            errno=EPIPE;
            (void)raise(SIGPIPE);
            return -1;
        }
        if(d->pipe->transient && count) {
            unsigned long long used=stream->data->produced-stream->ack->consumed;
            unsigned needed=(unsigned)count>UXPIPE_CAPACITY ? 1 : (unsigned)count;
            if(used<=UXPIPE_CAPACITY && UXPIPE_CAPACITY-used<needed) {
                unsigned long long expected=stream->ack->consumed;
                release(d->pipe);
                if(wait_peer(d->pipe,d->flags,expected)<0) return -1;
                goto retry_write;
            }
        }
        n=uxpipe_write(stream,buf,
                      (unsigned)count>UXPIPE_MAX_IO ? UXPIPE_MAX_IO : (unsigned)count,
                      d->pipe->transient ? 0 : -1);
        release(d->pipe);
        if(n<0) { errno=p9_process_interrupted() ? EINTR : EIO; return -1; }
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
static int dup2_impl(int oldfd,int newfd)
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
static int fcntl_impl(int fd,int command,...)
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

static int pipe_impl(int result[2])
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

static int make_inheritable(Description *d)
{
    unsigned i;
    PipePair *p=d->pipe;
    if(!p) {
        int handles[2],role;
        if(d->backend>=3) { errno=ENOTSUP; return -1; }
        /* Backend verifies explicitly bound stream grants; console Handles
         * are nondelegable and cannot be manufactured for fork. */
        if(p9_backend_stream(d->backend,handles,&role)<0) return -1;
        for(i=0;i<FD_COUNT/2;++i) if(!pipes[i].ends) break;
        if(i==FD_COUNT/2) { errno=ENFILE; return -1; }
        p=&pipes[i]; memset(p,0,sizeof(*p));
        p->handles[0]=handles[0]; p->handles[1]=handles[1];
        p->ends=1; p->transient=1;
        d->pipe=p;
        d->flags=role==UXPIPE_READER ? O_RDONLY : O_WRONLY;
        return 0;
    }
    if(!p->transient) {
        for(i=0;i<2;++i) if(p->maps[i]) {
            (void)p9_full_region_unmap(p->maps[i]); p->maps[i]=0;
        }
        p->transient=1;
    }
    return 0;
}
int p9_fd_export(P9FdImage *image,P9Plan *plan,int exec)
{
    int fd,indices[FD_COUNT],i;
    unsigned acquired=0;
    initialize();
    memset(image,0,sizeof(*image));
    image->files=-1;
    if(p9_project_root()) {
        image->files=p9_file_export(plan);
        if(image->files<0 || !getcwd(image->cwd,sizeof image->cwd) || p9_file_export_cwd(image,plan)<0) return -1;
    }
    for(i=0;i<FD_COUNT;++i) { indices[i]=-1; image->fds[i].object=-1; }
    for(fd=0;fd<FD_COUNT;++fd) {
        Descriptor *f=&descriptors[fd];
        Description *d=f->description;
        int index,object;
        if(!d || (exec&&(f->flags&FD_CLOEXEC))) continue;
        index=(int)(d-descriptions);
        object=indices[index];
        if(object<0) {
            P9FdObject *o=&image->objects[image->count];
            o->source=index; o->flags=d->flags;
            if(!d->pipe && d->backend>=3) {
                if(image->files<0) { errno=EACCES; return -1; }
                o->kind=2; o->backend=d->backend;
                if(p9_file_export_fd(d->backend,plan,&o->first,&o->second)<0)return -1;
            } else {
                if(make_inheritable(d)<0) return -1;
                o->kind=1; o->backend=-1;
                o->first=p9_plan_add(plan,d->pipe->handles[0],
                    (d->flags==O_RDONLY ? Vread : Vwrite)|Vdelegate);
                o->second=p9_plan_add(plan,d->pipe->handles[1],
                    (d->flags==O_RDONLY ? Vwrite : Vread)|Vdelegate);
                if(o->first<0 || o->second<0) return -1;
            }
            object=(int)image->count++;
            indices[index]=object;
        }
        image->fds[fd]=(P9FdEntry){object,f->flags};
    }
    for(acquired=0;acquired<image->count;++acquired)
        if((image->objects[acquired].kind==2 ?
            p9_file_ref(image->objects[acquired].backend,1) :
            endpoint_refs(&descriptions[image->objects[acquired].source],1))<0) {
            while(acquired) {
                --acquired;
                if(image->objects[acquired].kind==2) (void)p9_file_ref(image->objects[acquired].backend,-1);
                else (void)endpoint_refs(&descriptions[image->objects[acquired].source],-1);
            }
            return -1;
        }
    return 0;
}
void p9_fd_export_undo(const P9FdImage *image)
{
    unsigned i;
    for(i=0;i<image->count;++i)
        if(image->objects[i].kind==2) (void)p9_file_ref(image->objects[i].backend,-1);
        else (void)endpoint_refs(&descriptions[image->objects[i].source],-1);
}
int p9_fd_import(const P9FdImage *image,const int *handles,unsigned count)
{
    unsigned i,j;
    if(image->count>FD_COUNT) { errno=EINVAL; return -1; }
    if(image->files>=0) {
        if((unsigned)image->files>=count || !memchr(image->cwd,0,sizeof image->cwd) ||
           p9_file_import(handles[image->files])<0 || p9_file_import_cwd(image,handles,count)<0) return -1;
    }
    memset(descriptors,0,sizeof descriptors);
    memset(descriptions,0,sizeof descriptions);
    memset(pipes,0,sizeof pipes);
    initialized=1;
    for(i=0;i<image->count;++i) {
        const P9FdObject *o=&image->objects[i];
        Description *d=&descriptions[i];
        PipePair *p=NULL;
        if(o->kind==2) {
            if(image->files<0 || o->backend<3 || o->backend>=3+UXFD_SLOTS) { errno=EINVAL; return -1; }
            if(o->first<0 || o->second<0 || (unsigned)o->first>=count || (unsigned)o->second>=count ||
               p9_file_import_fd(o->backend,handles[o->first],handles[o->second],o->flags)<0)return -1;
            d->backend=o->backend; d->flags=o->flags;
            continue;
        }
        if(o->kind!=1 || o->first<0 || o->second<0 ||
           (unsigned)o->first>=count || (unsigned)o->second>=count ||
           (o->flags!=O_RDONLY && o->flags!=O_WRONLY)) { errno=EINVAL; return -1; }
        for(j=0;j<FD_COUNT/2;++j)
            if(pipes[j].ends && pipes[j].handles[0]==handles[o->first] &&
               pipes[j].handles[1]==handles[o->second]) { p=&pipes[j]; break; }
        if(!p) {
            for(j=0;j<FD_COUNT/2;++j) if(!pipes[j].ends) { p=&pipes[j]; break; }
            if(!p) { errno=EMFILE; return -1; }
            p->handles[0]=handles[o->first]; p->handles[1]=handles[o->second]; p->transient=1;
        }
        ++p->ends;
        d->flags=o->flags; d->backend=-1; d->pipe=p;
    }
    for(i=0;i<FD_COUNT;++i) {
        const P9FdEntry *f=&image->fds[i];
        if(f->object==-1) continue;
        if(f->object<0 || (unsigned)f->object>=image->count || (f->flags&~FD_CLOEXEC)) {
            errno=EINVAL; return -1;
        }
        descriptors[i].description=&descriptions[f->object];
        descriptors[i].flags=f->flags;
        ++descriptions[f->object].refs;
    }
    for(i=0;i<image->count;++i) if(!descriptions[i].refs) { errno=EINVAL; return -1; }
    return 0;
}

int _open(const char *path,int flags,...)
{
    mode_t mode=0;
    int result;
    if(flags&O_CREAT) { va_list ap; va_start(ap,flags); mode=(mode_t)va_arg(ap,int); va_end(ap); }
    p9_process_enter(); result=_open_impl(path,flags,mode); p9_process_leave();
    return result;
}
int _close(int fd)
{
    int result;
    p9_process_enter(); result=_close_impl(fd); p9_process_leave();
    return result;
}
int dup2(int source,int target)
{
    int result;
    p9_process_enter(); result=dup2_impl(source,target); p9_process_leave();
    return result;
}
int fcntl(int fd,int command,...)
{
    int argument=0,result;
    if(command==F_DUPFD || command==F_SETFD || command==F_SETFL
#ifdef F_DUPFD_CLOEXEC
       || command==F_DUPFD_CLOEXEC
#endif
      ) { va_list ap; va_start(ap,command); argument=va_arg(ap,int); va_end(ap); }
    p9_process_enter(); result=fcntl_impl(fd,command,argument); p9_process_leave();
    return result;
}
int pipe(int streams[2])
{
    int result;
    p9_process_enter(); result=pipe_impl(streams); p9_process_leave();
    return result;
}

/* Poll observes state without consuming bytes or changing file position. */
int p9_fd_events(int fd,short events)
{
    Description *d=lookup(fd);
    int result=0;
    if(!d) return POLLNVAL;
    if(d->pipe) {
        UxPipe *stream=acquire(d->pipe,d->flags);
        if(!stream) return -1;
        unsigned long long wr=stream->data->produced,rd=stream->ack->consumed;
        if(d->flags==O_RDONLY) {
            if(stream->data->closed) result|=POLLHUP;
            if((events&POLLIN) && (wr!=rd || stream->data->closed)) result|=POLLIN;
        } else {
            if(stream->ack->closed) result|=POLLERR;
            else if((events&POLLOUT) && wr-rd<UXPIPE_CAPACITY) result|=POLLOUT;
        }
        release(d->pipe);
        return result;
    }
    struct stat st;
    if(p9_backend_fstat(d->backend,&st)<0) return POLLNVAL;
    if(!S_ISREG(st.st_mode)) { errno=ENOTSUP; return -1; }
    if((events&POLLIN) && (d->flags&O_ACCMODE)!=O_WRONLY) result|=POLLIN;
    if((events&POLLOUT) && (d->flags&O_ACCMODE)!=O_RDONLY) result|=POLLOUT;
    return result;
}
