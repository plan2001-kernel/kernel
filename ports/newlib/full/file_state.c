/* A serialized shared snapshot of the bounded volatile UXFS service.
 * The region is an explicit capability, never a pathname or guessed OID.
 * Native persistent services keep their callback authority process-local;
 * those require a separately provisioned file-service RPC connection.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "process.h"
#include "plan2001_full.h"
#include "fd_backend.h"
#include "../../unix/uxfd.h"
#include "vobj.h"
typedef struct {
    uint64_t magic;
    Pvd view;
    UxFs fs;
    UxFdSlot slots[UXFD_SLOTS];
    unsigned refs[UXFD_SLOTS];
} SharedFiles;
#define FILE_MAGIC UINT64_C(0x3153464453583950)
static int shared_handle;
static SharedFiles *transaction;
static UxService imported_service;
static Pvd imported_view;
extern UxFdTable *p9_backend_files(void);
extern long p9_native_sleep_millis(long);
extern int p9_raw_read(int,char *,int),p9_raw_write(int,const char *,int);
extern int p9_raw_open(const char *,int,...),p9_raw_close(int);
extern int p9_raw_fstat(int,struct stat *),p9_raw_isatty(int);
extern off_t p9_raw_lseek(int,off_t,int);
extern int p9_raw_stat(const char *,struct stat *),p9_raw_lstat(const char *,struct stat *);
extern int p9_raw_unlink(const char *),p9_raw_link(const char *,const char *);
extern int p9_raw_chdir(const char *),p9_raw_mkdir(const char *,mode_t);
extern int p9_raw_rmdir(const char *);
extern int p9_raw_rename(const char *,const char *),p9_raw_access(const char *,int);
extern char *p9_raw_getcwd(char *,size_t);

static SharedFiles *lock_files(void)
{
    long base;
    unsigned tries=0;
    do {
        base=p9_full_region_map(shared_handle,0);
        if(base>0) return (SharedFiles *)(uintptr_t)base;
        (void)p9_native_sleep_millis(1);
    } while(++tries<5000);
    errno=EBUSY; return NULL;
}
static int begin(void)
{
    UxFdTable *table;
    Pvd *view;
    if(!shared_handle) return 0;
    if(transaction) { errno=EDEADLK; return -1; }
    table=p9_backend_files();
    if(!table) { errno=EIO; return -1; }
    p9_process_enter();
    transaction=lock_files();
    if(!transaction) { p9_process_leave(); return -1; }
    if(transaction->magic!=FILE_MAGIC) {
        (void)p9_full_region_unmap((uintptr_t)transaction); transaction=NULL;
        p9_process_leave();
        errno=EIO; return -1;
    }
    view=table->service->fs.view;
    /* Topology is shared; current-directory selection remains local. */
    memcpy(view->nodes,transaction->view.nodes,sizeof view->nodes);
    view->count=transaction->view.count;
    table->service->fs=transaction->fs;
    table->service->fs.view=view;
    memcpy(table->slots,transaction->slots,sizeof table->slots);
    return 0;
}
static void end(void)
{
    UxFdTable *table;
    int saved=errno;
    if(!transaction) return;
    table=p9_backend_files();
    transaction->fs=table->service->fs;
    transaction->fs.view=NULL;
    memcpy(transaction->view.nodes,table->service->fs.view->nodes,sizeof transaction->view.nodes);
    transaction->view.count=table->service->fs.view->count;
    memcpy(transaction->slots,table->slots,sizeof transaction->slots);
    (void)p9_full_region_unmap((uintptr_t)transaction);
    transaction=NULL;
    p9_process_leave();
    errno=saved;
}
void p9_file_refresh(void) { if(begin()==0) end(); }
void p9_file_detach(void)
{
    if(shared_handle>0) (void)p9_full_region_close(shared_handle);
    shared_handle=0;
}
int p9_file_export(P9Plan *plan)
{
    UxFdTable *table=p9_backend_files();
    SharedFiles *state;
    unsigned i;
    if(!table || table->service->persistent) { errno=ENOTSUP; return -1; }
    if(!shared_handle) {
        shared_handle=(int)p9_full_region_create((sizeof(SharedFiles)+4095u)&~4095u);
        if(shared_handle<=0) { shared_handle=0; errno=ENOMEM; return -1; }
        state=lock_files();
        if(!state) { (void)p9_full_region_close(shared_handle); shared_handle=0; return -1; }
        memset(state,0,sizeof(*state));
        state->magic=FILE_MAGIC; state->view=*table->service->fs.view;
        state->fs=table->service->fs; state->fs.view=NULL;
        memcpy(state->slots,table->slots,sizeof state->slots);
        for(i=0;i<UXFD_SLOTS;++i) state->refs[i]=table->slots[i].used ? 1 : 0;
        (void)p9_full_region_unmap((uintptr_t)state);
    }
    return p9_plan_add(plan,shared_handle,Vread|Vwrite|Vdelegate);
}
int p9_file_import(int handle)
{
    SharedFiles *state;
    /* After RFORKEX the copied parent Handle number is stale. Import only
     * the generation-tagged child Handle obtained through SYSCTL. */
    shared_handle=0; transaction=NULL;
    p9_full_unbind_files();
    shared_handle=handle;
    state=lock_files();
    if(!state) return -1;
    if(state->magic!=FILE_MAGIC) {
        (void)p9_full_region_unmap((uintptr_t)state); errno=EIO; return -1;
    }
    imported_view=state->view;
    (void)p9_full_region_unmap((uintptr_t)state);
    uxservice_init(&imported_service,&imported_view);
    if(p9_full_bind_files(&imported_service)<0) return -1;
    p9_file_refresh();
    return 0;
}
int p9_file_ref(int fd,int delta)
{
    unsigned *refs;
    if(fd<3 || fd>=3+UXFD_SLOTS || !shared_handle) { errno=EBADF; return -1; }
    if(begin()<0) return -1;
    refs=&transaction->refs[fd-3];
    if(!*refs || (*refs==~0u && delta>0)) { end(); errno=EIO; return -1; }
    if(delta>0) ++*refs;
    else --*refs;
    end(); return 0;
}
#define WRAP_FILE(fd,expression) do { \
    int guarded=(fd)>=3; long long value; \
    if(guarded && begin()<0) return -1; \
    value=(expression); if(guarded) end(); return value; \
} while(0)
int p9_backend_read(int fd,char *buf,int n) { WRAP_FILE(fd,p9_raw_read(fd,buf,n)); }
int p9_backend_write(int fd,const char *buf,int n) { WRAP_FILE(fd,p9_raw_write(fd,buf,n)); }
off_t p9_backend_lseek(int fd,off_t pos,int whence) { WRAP_FILE(fd,p9_raw_lseek(fd,pos,whence)); }
int p9_backend_fstat(int fd,struct stat *st) { WRAP_FILE(fd,p9_raw_fstat(fd,st)); }
int p9_backend_isatty(int fd) { WRAP_FILE(fd,p9_raw_isatty(fd)); }
int p9_backend_open(const char *path,int flags,...)
{
    mode_t mode=0;
    int fd;
    if(flags&O_CREAT) { va_list ap; va_start(ap,flags); mode=(mode_t)va_arg(ap,int); va_end(ap); }
    if(begin()<0) return -1;
    fd=p9_raw_open(path,flags,mode);
    if(transaction && fd>=3) transaction->refs[fd-3]=1;
    end(); return fd;
}
int p9_backend_close(int fd)
{
    int result;
    if(fd<3 || !shared_handle) return p9_raw_close(fd);
    if(begin()<0) return -1;
    if(fd>=3+UXFD_SLOTS || !transaction->refs[fd-3]) { end(); errno=EBADF; return -1; }
    result=--transaction->refs[fd-3] ? 0 : p9_raw_close(fd);
    end(); return result;
}
#define PATH_WRAP(expression) do { int result; \
    if(begin()<0) { return -1; } result=(expression); end(); return result; \
} while(0)
int _stat(const char *p,struct stat *s) { PATH_WRAP(p9_raw_stat(p,s)); }
int _lstat(const char *p,struct stat *s) { PATH_WRAP(p9_raw_lstat(p,s)); }
int _unlink(const char *p) { PATH_WRAP(p9_raw_unlink(p)); }
int _link(const char *p,const char *q) { PATH_WRAP(p9_raw_link(p,q)); }
int _chdir(const char *p) { PATH_WRAP(p9_raw_chdir(p)); }
int _mkdir(const char *p,mode_t mode) { PATH_WRAP(p9_raw_mkdir(p,mode)); }
int rmdir(const char *p) { PATH_WRAP(p9_raw_rmdir(p)); }
int _rename(const char *p,const char *q) { PATH_WRAP(p9_raw_rename(p,q)); }
int _access(const char *p,int mode) { PATH_WRAP(p9_raw_access(p,mode)); }
char *getcwd(char *p,size_t n)
{
    char *result;
    if(begin()<0) return NULL;
    result=p9_raw_getcwd(p,n); end(); return result;
}
