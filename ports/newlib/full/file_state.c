/* UXFS Unix descriptors over authoritative durable ObjectSpace objects.
 * Only open-description offsets are shared in regions. No file data or
 * namespace topology is copied between processes. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "project_fs.h"
#include "plan2001_full.h"
#include "fd_backend.h"
#include "sysctl.h"
#include "../../unix/uxfd.h"
static PjObject root;
static char cwd[4096]="/";
static PjObject *cwd_frames;
static unsigned cwd_count;
typedef struct {PjObject object;int region,flags;} OpenFile;
static OpenFile files[UXFD_SLOTS];
extern long objopen(unsigned long long,void*,long,unsigned long long,void*);
extern long objclose(int);
extern int p9_raw_read(int,char*,int),p9_raw_write(int,const char*,int);
extern int p9_raw_fstat(int,struct stat*),p9_raw_isatty(int);
extern long p9_native_sleep_millis(long);
int p9_project_root(void){return root.handle;}
int p9_file_import(int handle){return pj_from_handle(&root,handle);}
int p9_project_boot(void)
{
    if(root.handle)return 0;
    VobjListRec list[1024];SysctlReq req={0};req.version=SYSCTL_ABI_VERSION;req.data=list;req.datalen=sizeof list;
    long n=sysctl(SysctlScopeOwnProcess,SysctlHandleList,&req,sizeof req);if(n<0)return -1;
    int h=0;for(long i=0;i<n;i++)if(list[i].kind==VkindObjectSpace){if(h){errno=EINVAL;return -1;}h=(int)list[i].handle;}
    if(!h){errno=EACCES;return -1;}
    return p9_file_import(h);
}
int p9_project_normalize(const char *path,char out[4096])
{
    if(!path || !*path){errno=ENOENT;return -1;}
    size_t len=strlen(path),base=path[0]=='/'?0:strlen(cwd);
    if(len+base+2>=4096){errno=ENAMETOOLONG;return -1;}
    char in[4096];if(base){memcpy(in,cwd,base);in[base++]='/';}memcpy(in+base,path,len+1);
    size_t i=0,n=1;out[0]='/';out[1]=0;
    while(in[i]){while(in[i]=='/')i++;size_t start=i;while(in[i] && in[i]!='/')i++;size_t k=i-start;
        if(!k || (k==1 && in[start]=='.'))continue;
        if(k==2 && in[start]=='.' && in[start+1]=='.'){while(n>1 && out[n-1]!='/')n--;if(n>1)n--;out[n]=0;continue;}
        if(k>255){errno=ENAMETOOLONG;return -1;}if(n>1)out[n++]='/';memcpy(out+n,in+start,k);n+=k;out[n]=0;
    }return 0;
}
static int copy_object(PjObject *source,PjObject *out)
{
    ObjProjectReq r={0};r.operation=ProjectAttenuate;r.rights=source->rights;
    int h=pj_call(source->handle,&r,NULL,0);if(h<0)return -1;
    if(pj_from_handle(out,h)){objclose(h);return -1;}return 0;
}
static void close_frames(PjObject *frames,unsigned n){int saved=errno;for(unsigned i=0;i<n;i++)pj_close(&frames[i]);free(frames);errno=saved;}
/* The caller's traversal route owns the parent references. A rename or an
 * alias elsewhere in the graph cannot change what '..' means in this view. */
static int walk(const char *path,PjObject **result,unsigned *count)
{
    if(!path || !*path){errno=ENOENT;return -1;}
    size_t length=strlen(path);if(length>=4096){errno=ENAMETOOLONG;return -1;}
    unsigned n=0,capacity=(unsigned)length+cwd_count+2;
    PjObject *frames=calloc(capacity,sizeof *frames);if(!frames){errno=ENOMEM;return -1;}
    if(path[0]!='/' && cwd_count){
        for(unsigned i=0;i<cwd_count;i++){if(copy_object(&cwd_frames[i],&frames[n]))goto fail;n++;}
    }else {if(copy_object(&root,&frames[n]))goto fail;n++;}
    size_t at=0;
    while(path[at]){
        while(path[at]=='/')at++;
        size_t start=at;while(path[at] && path[at]!='/')at++;
        size_t len=at-start;if(!len)break;
        if(frames[n-1].kind!=VkindObjectSpace){errno=ENOTDIR;goto fail;}
        if(len==1 && path[start]=='.')continue;
        if(len==2 && path[start]=='.' && path[start+1]=='.'){if(n>1)pj_close(&frames[--n]);continue;}
        if(len>255){errno=ENAMETOOLONG;goto fail;}
        char name[256];memcpy(name,path+start,len);name[len]=0;
        if(pj_resolve(&frames[n-1],name,&frames[n]))goto fail;n++;
    }
    if(length && path[length-1]=='/' && frames[n-1].kind!=VkindObjectSpace){errno=ENOTDIR;goto fail;}
    *result=frames;*count=n;return 0;
fail:close_frames(frames,n);return -1;
}
int p9_project_resolve(const char *path,PjObject *o)
{
    PjObject *frames;unsigned n;if(walk(path,&frames,&n))return -1;
    *o=frames[n-1];frames[n-1].handle=0;close_frames(frames,n);return 0;
}
static int parent(const char *path,PjObject *o,char label[256])
{
    if(!path || !*path){errno=ENOENT;return -1;}
    size_t n=strlen(path);if(n>=4096){errno=ENAMETOOLONG;return -1;}
    char prefix[4096];memcpy(prefix,path,n+1);
    while(n>1 && prefix[n-1]=='/')prefix[--n]=0;
    char *p=strrchr(prefix,'/');const char *name=p?p+1:prefix;
    if(!*name || !strcmp(name,".") || !strcmp(name,"..")){errno=EBUSY;return -1;}
    if(strlen(name)>255){errno=ENAMETOOLONG;return -1;}strcpy(label,name);
    if(!p)return p9_project_resolve(".",o);
    if(p==prefix)p[1]=0;else *p=0;
    return p9_project_resolve(prefix,o);
}
int p9_file_export_cwd(P9FdImage *image,P9Plan *plan)
{
    image->cwd_count=cwd_count;
    for(unsigned i=0;i<cwd_count;i++){
        if(i>=P9_FDS){errno=EMFILE;return -1;}
        int index=p9_plan_add(plan,cwd_frames[i].handle,cwd_frames[i].rights);if(index<0)return -1;
        image->cwd_handles[i]=index;
    }return 0;
}
int p9_file_import_cwd(const P9FdImage *image,const int *handles,unsigned count)
{
    if(image->cwd_count>P9_FDS){errno=EINVAL;return -1;}
    PjObject *frames=calloc(image->cwd_count?image->cwd_count:1,sizeof *frames);if(!frames){errno=ENOMEM;return -1;}
    unsigned n=0;
    for(;n<image->cwd_count;n++){
        int index=image->cwd_handles[n];if(index<0 || (unsigned)index>=count){errno=EINVAL;goto fail;}
        if(pj_from_handle(&frames[n],handles[index]))goto fail;
        if(frames[n].kind!=VkindObjectSpace){errno=ENOTDIR;goto fail;}
    }
    close_frames(cwd_frames,cwd_count);cwd_frames=frames;cwd_count=n;strcpy(cwd,image->cwd);return 0;
fail:close_frames(frames,n);return -1;
}
int p9_file_export(P9Plan *plan){if(!root.handle){errno=EACCES;return -1;}return p9_plan_add(plan,root.handle,root.rights);}
void p9_file_refresh(void){}
void p9_file_detach(void){close_frames(cwd_frames,cwd_count);cwd_frames=NULL;cwd_count=0;pj_close(&root);}
int p9_file_ref(int fd,int delta){(void)delta;return fd>=3 && fd<3+UXFD_SLOTS && files[fd-3].object.handle?0:-1;}
int p9_file_export_fd(int fd,P9Plan *plan,int *a,int *b)
{
    if(fd<3 || fd>=3+UXFD_SLOTS){errno=EBADF;return -1;}OpenFile *f=&files[fd-3];
    *a=p9_plan_add(plan,f->object.handle,f->object.rights);*b=p9_plan_add(plan,f->region,Vread|Vwrite|Vdelegate);return *a<0 || *b<0?-1:0;
}
int p9_file_import_fd(int fd,int h,int region,int flags)
{
    if(fd<3 || fd>=3+UXFD_SLOTS){errno=EBADF;return -1;}OpenFile *f=&files[fd-3];
    if(pj_from_handle(&f->object,h))return -1;f->region=region;f->flags=flags;return 0;
}
static uint64_t *offset_lock(OpenFile *f)
{
    for(;;){
        long b=p9_full_region_map(f->region,0);if(b>0)return (uint64_t*)(uintptr_t)b;
        if(p9_process_interrupted()){errno=EINTR;return NULL;}
        p9_native_sleep_millis(1);
    }
}
static void offset_unlock(uint64_t *p){int saved=errno;p9_full_region_unmap((uintptr_t)p);errno=saved;}
int p9_backend_open(const char *path,int flags,...)
{
    if(p9_process_executable(path,NULL)){errno=EACCES;return -1;}
    int fd;for(fd=0;fd<UXFD_SLOTS && files[fd].object.handle;fd++);if(fd==UXFD_SLOTS){errno=EMFILE;return -1;}
    PjObject o={0};int found=p9_project_resolve(path,&o)==0;
    if(found && (flags&O_CREAT) && (flags&O_EXCL)){pj_close(&o);errno=EEXIST;return -1;}
    if(!found){if(!(flags&O_CREAT) || errno!=ENOENT)return -1;
        PjObject par;char label[256];if(parent(path,&par,label))return -1;
        int rc=pj_create(&par,label,VkindGe,NULL,0,&o);pj_close(&par);if(rc)return -1;
    }
    if(o.kind==VkindObjectSpace){pj_close(&o);errno=EISDIR;return -1;}
    if(((flags&O_ACCMODE)!=O_RDONLY && !(o.rights&Vwrite)) ||
       ((flags&O_ACCMODE)!=O_WRONLY && !(o.rights&Vread))){pj_close(&o);errno=EACCES;return -1;}
    if((flags&O_TRUNC) && pj_write(&o,NULL,0,0,1)<0){pj_close(&o);return -1;}
    int region=(int)p9_full_region_create(4096);if(region<=0){pj_close(&o);return -1;}
    OpenFile *f=&files[fd];f->object=o;f->region=region;f->flags=flags;
    uint64_t *pos=offset_lock(f);if(!pos){p9_full_region_close(region);pj_close(&f->object);return -1;}*pos=0;offset_unlock(pos);return fd+3;
}
int p9_backend_close(int fd)
{
    if(fd<3){extern int p9_raw_close(int);return p9_raw_close(fd);}
    if(fd>=3+UXFD_SLOTS || !files[fd-3].object.handle){errno=EBADF;return -1;}
    OpenFile *f=&files[fd-3];pj_close(&f->object);p9_full_region_close(f->region);f->region=0;return 0;
}
int p9_backend_read(int fd,char *buf,int n)
{
    if(fd<3)return (int)p9_raw_read(fd,buf,n);
    if(n<0 || fd>=3+UXFD_SLOTS || !files[fd-3].object.handle){errno=EBADF;return -1;}
    OpenFile *f=&files[fd-3];if((f->flags&O_ACCMODE)==O_WRONLY){errno=EBADF;return -1;}
    uint64_t *off=offset_lock(f);if(!off)return -1;long rc=pj_read(&f->object,buf,(unsigned)n,*off);if(rc>0)*off+=(uint64_t)rc;offset_unlock(off);return (int)rc;
}
int p9_backend_write(int fd,const char *buf,int n)
{
    if(fd<3)return (int)p9_raw_write(fd,buf,n);
    if(n<0 || fd>=3+UXFD_SLOTS || !files[fd-3].object.handle){errno=EBADF;return -1;}
    OpenFile *f=&files[fd-3];if((f->flags&O_ACCMODE)==O_RDONLY){errno=EBADF;return -1;}
    uint64_t *off=offset_lock(f);if(!off)return -1;
    long rc=pj_write(&f->object,buf,(unsigned)n,*off,(f->flags&O_APPEND)?2:0);
    if(rc>0)*off=f->object.position_after;offset_unlock(off);return (int)rc;
}
off_t p9_backend_lseek(int fd,off_t delta,int whence)
{
    if(fd<3 || fd>=3+UXFD_SLOTS || !files[fd-3].object.handle){errno=EBADF;return -1;}OpenFile *f=&files[fd-3];
    uint64_t *off=offset_lock(f);if(!off)return -1;int64_t base=0;
    if(whence==SEEK_CUR)base=(int64_t)*off;
    else if(whence==SEEK_END){size_t raw;UxMeta m;if(pj_metadata_size(&f->object,&m,&raw)){offset_unlock(off);return -1;}base=(int64_t)raw;}
    else if(whence!=SEEK_SET){offset_unlock(off);errno=EINVAL;return -1;}
    if(delta< -base || delta>INT64_MAX-base){offset_unlock(off);errno=EINVAL;return -1;}*off=(uint64_t)(base+delta);off_t result=(off_t)*off;offset_unlock(off);return result;
}
static int object_stat(PjObject *o,struct stat *st)
{
    size_t raw;UxMeta m={0};if(pj_metadata_size(o,&m,&raw))return -1;
    memset(st,0,sizeof *st);st->st_ino=(ino_t)o->id.oid;
    uint64_t dev=UINT64_C(14695981039346656037);for(unsigned i=0;i<16;i++){dev^=o->id.volume[i];dev*=UINT64_C(1099511628211);}st->st_dev=(dev_t)dev;
    st->st_mode=(m.mode&07777)|(o->kind==VkindObjectSpace?S_IFDIR:S_IFREG);st->st_uid=m.uid;st->st_gid=m.gid;st->st_rdev=m.rdev;st->st_size=(off_t)raw;
    ObjProjectReq links={0};links.operation=ProjectLinks;links.other=o->handle;
    int count=pj_call(root.handle,&links,NULL,0);if(count<0)return -1;
    st->st_nlink=(nlink_t)count;st->st_blksize=4096;st->st_blocks=(blkcnt_t)((raw+511)/512);
    st->st_atim.tv_sec=m.atime_ns/1000000000LL;st->st_atim.tv_nsec=m.atime_ns%1000000000LL;
    st->st_mtim.tv_sec=m.mtime_ns/1000000000LL;st->st_mtim.tv_nsec=m.mtime_ns%1000000000LL;
    st->st_ctim.tv_sec=m.ctime_ns/1000000000LL;st->st_ctim.tv_nsec=m.ctime_ns%1000000000LL;return 0;
}
int p9_backend_fstat(int fd,struct stat *st){if(fd<3)return p9_raw_fstat(fd,st);if(fd>=3+UXFD_SLOTS || !files[fd-3].object.handle){errno=EBADF;return -1;}return object_stat(&files[fd-3].object,st);}
int p9_backend_isatty(int fd){return fd<3?p9_raw_isatty(fd):0;}
int _stat(const char *path,struct stat *st)
{
    uint64_t oid;if(p9_process_executable(path,&oid)){memset(st,0,sizeof *st);st->st_mode=S_IFREG|0111;st->st_ino=(ino_t)oid;st->st_nlink=1;return 0;}
    PjObject o;if(p9_project_resolve(path,&o))return -1;int rc=object_stat(&o,st);pj_close(&o);return rc;
}
int _lstat(const char *p,struct stat *s){return _stat(p,s);}
int _chdir(const char *path)
{
    PjObject *frames;unsigned n;if(walk(path,&frames,&n))return -1;
    if(frames[n-1].kind!=VkindObjectSpace){close_frames(frames,n);errno=ENOTDIR;return -1;}
    char display[4096];if(p9_project_normalize(path,display)){close_frames(frames,n);return -1;}
    close_frames(cwd_frames,cwd_count);cwd_frames=frames;cwd_count=n;strcpy(cwd,display);return 0;
}
char *getcwd(char *buf,size_t n){size_t len=strlen(cwd)+1;if(!buf){buf=malloc(n?n:len);if(!buf){errno=ENOMEM;return NULL;}n=n?n:len;}if(n<len){errno=ERANGE;return NULL;}memcpy(buf,cwd,len);return buf;}
int _mkdir(const char *path,mode_t mode){(void)mode;PjObject par;char label[256];if(parent(path,&par,label))return -1;int rc=pj_create(&par,label,VkindObjectSpace,NULL,0,NULL);pj_close(&par);return rc;}
int _unlink(const char *path){PjObject par;char label[256];if(parent(path,&par,label))return -1;int rc=pj_remove(&par,label,0);pj_close(&par);return rc;}
int rmdir(const char *path){PjObject par;char label[256];if(parent(path,&par,label))return -1;int rc=pj_remove(&par,label,1);pj_close(&par);return rc;}
int _link(const char *old,const char *path){PjObject target,par;char label[256];if(p9_project_resolve(old,&target))return -1;if(parent(path,&par,label)){pj_close(&target);return -1;}int rc=pj_link(&par,label,&target);pj_close(&par);pj_close(&target);return rc;}
int _rename(const char *old,const char *path){PjObject src,dst;char a[256],b[256];if(parent(old,&src,a))return -1;if(parent(path,&dst,b)){pj_close(&src);return -1;}int rc=pj_rename(&src,a,&dst,b,1);pj_close(&src);pj_close(&dst);return rc;}
int _access(const char *p,int mode){struct stat st;if(mode&X_OK){if(p9_process_executable(p,NULL))return 0;errno=EACCES;return -1;}return _stat(p,&st);}
typedef struct {int (*emit)(void*,const char*,unsigned,unsigned);void *context;} ProjectList;
static int project_emit(void *p,const ProjectEntry *e)
{
    ProjectList *list=p;char name[256];memcpy(name,e->name,e->name_length);name[e->name_length]=0;
    return list->emit(list->context,name,(unsigned)e->name_length,e->kind==VkindObjectSpace?UXFS_DIR:UXFS_FILE);
}
int p9_project_list(const char *path,int (*emit)(void*,const char*,unsigned,unsigned),void *ctx)
{
    PjObject o;if(p9_project_resolve(path,&o))return -1;
    if(o.kind!=VkindObjectSpace){pj_close(&o);errno=ENOTDIR;return -1;}
    ProjectList list={emit,ctx};int rc=pj_list(&o,project_emit,&list);pj_close(&o);return rc;
}
