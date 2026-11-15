/* Host-only contract tests: no claim of guest capability enforcement. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
extern int p9_fd_events(int,short);
#include "../../ports/newlib/full/fd_backend.h"
#include "../../ports/newlib/full/process.h"
#include "../../ports/unix/uxfd.h"
#include "sysctl.h"

extern int _open(const char *,int,...), _close(int);
extern int _read(int,char *,int), _write(int,const char *,int);
extern off_t _lseek(int,off_t,int);
extern int _fstat(int,struct stat *);
extern void p9_full_close_descriptors(void);
static int backend_used, closes, offset, region_count, fail_create, attempts;
static void *regions[64];
int p9_process_interrupted(void) { return 0; }
void p9_process_enter(void) {}
void p9_process_leave(void) {}
UxFdTable *p9_backend_files(void) { return NULL; }
int p9_backend_stream(int fd,int handles[2],int *role)
{ (void)fd; (void)handles; (void)role; errno=ENOTSUP; return -1; }
int p9_file_export(P9Plan *p) { (void)p; return -1; }
int p9_file_import(int handle) { (void)handle; return -1; }
int p9_file_ref(int fd,int delta) { (void)fd; (void)delta; return -1; }
long sysctl(unsigned scope,unsigned operation,SysctlReq *req,long size)
{ (void)scope; (void)operation; (void)req; (void)size; return -1; }
int p9_backend_open(const char *path,int flags,...)
{
    (void)path; (void)flags;
    assert(!backend_used);
    backend_used=1;
    offset=0;
    return 3;
}
int p9_backend_close(int fd)
{
    if(fd==3) { assert(backend_used); backend_used=0; ++closes; }
    return 0;
}
int p9_backend_read(int fd,char *buf,int n)
{
    (void)fd;
    if(n && offset<3) { *buf="abc"[offset++]; return 1; }
    return 0;
}
int p9_backend_write(int fd,const char *buf,int n)
{
    (void)fd; (void)buf; offset+=n; return n;
}
off_t p9_backend_lseek(int fd,off_t pos,int whence)
{
    (void)fd; assert(whence==SEEK_SET); offset=(int)pos; return pos;
}
int p9_backend_fstat(int fd,struct stat *st)
{
    (void)fd; memset(st,0,sizeof(*st)); st->st_mode=S_IFREG; return 0;
}
int p9_backend_isatty(int fd) { return fd<3; }
long p9_full_region_create(uint64_t n)
{
    int i;
    if(++attempts==fail_create) return -1;
    for(i=1;i<64;++i) if(!regions[i]) {
        regions[i]=calloc(1,(size_t)n);
        assert(regions[i]); ++region_count; return i;
    }
    return -1;
}
long p9_full_region_map(int handle,int readonly)
{
    (void)readonly; return (long)(uintptr_t)regions[handle];
}
long p9_full_region_unmap(uintptr_t base) { (void)base; return 0; }
long p9_full_region_close(int handle)
{
    assert(regions[handle]); free(regions[handle]); regions[handle]=0;
    --region_count; return 0;
}
long region_wait(unsigned long long b,unsigned long long o,
                 unsigned long long e,long long t)
{
    (void)b; (void)o; (void)e; (void)t;
    assert(!"unexpected blocking wait"); return -1;
}
long region_wake(unsigned long long b,unsigned long long o,unsigned long long n)
{
    (void)b; (void)o; (void)n; return 0;
}
long p9_native_sleep_millis(long n) { (void)n; assert(0); return -1; }

int main(void)
{
    int fd, alias, saved, p[2], copy, fds[32], n=0;
    char c, bytes[8];
    struct stat st;
    assert(signal(SIGPIPE,SIG_IGN)!=SIG_ERR);
    fd=_open("file",O_RDWR);
    assert(fd==3);
    alias=fcntl(fd,F_DUPFD,8);
    assert(alias==8);
    assert(_read(fd,&c,1)==1 && c=='a');
    assert(_read(alias,&c,1)==1 && c=='b');
    assert(fcntl(fd,F_SETFD,FD_CLOEXEC)==0);
    assert(fcntl(alias,F_GETFD)==0);
    assert(dup2(fd,fd)==fd && fcntl(fd,F_GETFD)==FD_CLOEXEC);
    assert(fcntl(fd,F_GETFL)==O_RDWR);
    assert(fcntl(fd,F_SETFL,O_APPEND)==-1 && errno==ENOSYS);
    assert(fcntl(fd,F_DUPFD,-1)==-1 && errno==EINVAL);
    assert(fcntl(fd,F_SETFD,FD_CLOEXEC|128)==-1 && errno==EINVAL);
    assert(dup2(alias,fd)==fd && fcntl(fd,F_GETFD)==0);
    assert(dup2(-1,alias)==-1 && errno==EBADF);
    assert(_read(alias,&c,1)==1 && c=='c');
    assert(_close(fd)==0 && !closes);
    assert(_lseek(alias,0,SEEK_SET)==0);
    assert(_read(alias,&c,1)==1 && c=='a');
    saved=dup(1);
    assert(saved==3);
    assert(dup2(alias,1)==1);
    assert(_write(1,"z",1)==1 && offset==2);
    assert(dup2(saved,1)==1 && _close(saved)==0);
    assert(!closes);
    assert(_close(alias)==0 && closes==1);
    assert(_close(alias)==-1 && errno==EBADF);
    assert(pipe(p)==0 && region_count==2);
    copy=dup(p[1]); assert(copy>=0);
    assert(p9_fd_events(p[0],POLLIN)==0);
    assert(p9_fd_events(p[1],POLLOUT)==POLLOUT);
    assert(_write(p[0],"x",1)==-1 && errno==EBADF);
    assert(_write(p[1],"hello",5)==5);
    assert(p9_fd_events(p[0],POLLIN)==POLLIN);
    assert(_close(p[1])==0);
    assert(_read(p[0],bytes,5)==5 && !memcmp(bytes,"hello",5));
    assert(_write(copy,"!",1)==1);
    assert(_close(copy)==0 && region_count==2);
    assert(_read(p[0],bytes,5)==1 && bytes[0]=='!');
    assert(_read(p[0],bytes,5)==0);
    assert(p9_fd_events(p[0],POLLIN)==(POLLIN|POLLHUP));
    assert(_fstat(p[0],&st)==0 && S_ISFIFO(st.st_mode));
    assert(_lseek(p[0],0,SEEK_SET)==-1 && errno==ESPIPE);
    assert(_close(p[0])==0 && region_count==0);
    assert(pipe(p)==0);
    assert(_close(p[0])==0);
    assert(_write(p[1],"x",1)==-1 && errno==EPIPE);
    assert(_close(p[1])==0 && region_count==0);
    attempts=0; fail_create=2;
    p[0]=99; p[1]=98;
    assert(pipe(p)==-1 && region_count==0 && p[0]==99 && p[1]==98);
    fail_create=0;
    while((fd=dup(1))>=0) fds[n++]=fd;
    assert(errno==EMFILE && n==16);
    p[0]=99; p[1]=98;
    assert(pipe(p)==-1 && errno==EMFILE && !region_count);
    assert(p[0]==99 && p[1]==98);
    while(n) assert(_close(fds[--n])==0);
    assert(_close(0)==0);
    fd=_open("file",O_RDONLY); assert(fd==0);
    assert(_close(fd)==0 && closes==2);
    assert(pipe(p)==0 && region_count==2);
    assert(dup(p[1])>=0);
    p9_full_close_descriptors();
    assert(region_count==0);
    return 0;
}
