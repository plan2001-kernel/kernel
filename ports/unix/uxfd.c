/* One bounded POSIX-like file descriptor table. Each open file uses a
 * canonical path and a private offset. All data changes pass through the
 * trusted UXFS owner and its explicitly delegated GE/O backend. */
#include "uxfd.h"
static unsigned int strsize(const char *s)
{
    unsigned int i=0;
    while (s[i] && i<PVD_PATH_MAX) ++i;
    return i;
}
static void copybytes(void *dst,const void *src,unsigned int n)
{
    unsigned char *d=dst; const unsigned char *s=src;
    while(n--) *d++=*s++;
}
static UxFdSlot *slot(UxFdTable *t,int fd)
{
    if(!t || !t->service || fd<3 || fd>=3+UXFD_SLOTS ||
       !t->slots[fd-3].used) return 0;
    return &t->slots[fd-3];
}
void uxfd_init(UxFdTable *t,UxService *service)
{
    unsigned i;
    if(!t) return;
    t->service=service;
    for(i=0;i<UXFD_SLOTS;++i) t->slots[i].used=0;
}
int uxfd_open(UxFdTable *t,const char *path,unsigned int flags)
{
    char resolved[PVD_PATH_MAX];
    unsigned int kind=0, size=0, i, n;
    int rc;
    if(!t || !t->service || !(flags&(UXFD_READ|UXFD_WRITE)) ||
       ((flags&(UXFD_CREATE|UXFD_TRUNC|UXFD_APPEND)) &&
        !(flags&UXFD_WRITE))) return PVD_EINVAL;
    rc=uxfs_path(&t->service->fs,path,resolved);
    if(rc) return rc;
    rc=uxfs_info(&t->service->fs,resolved,&kind,&size);
    if(rc==PVD_ENOENT) {
        if(!(flags&UXFD_CREATE)) return rc;
        rc=uxfs_write(&t->service->fs,resolved,0,0,0);
        if(rc) return rc;
    } else if(rc!=PVD_OK) return rc;
    else if(kind!=UXFS_FILE) return PVD_EINVAL;
    n=strsize(resolved);
    if(n>=PVD_PATH_MAX) return PVD_ENAMETOOLONG;
    for(i=0;i<UXFD_SLOTS;++i)
        if(!t->slots[i].used) {
            UxFdSlot *f=&t->slots[i];
            if((flags&UXFD_TRUNC) &&
               (rc=uxfs_write(&t->service->fs,resolved,0,0,0))!=PVD_OK)
                return rc;
            copybytes(f->path,resolved,n+1);
            f->position=(flags&UXFD_APPEND) ? size : 0;
            if(flags&UXFD_TRUNC) f->position=0;
            f->rights=flags;
            f->used=1;
            rc=uxservice_sync(t->service);
            if(rc) {f->used=0; return rc;}
            return (int)i+3;
        }
    return PVD_ENOSPC;
}
long uxfd_read(UxFdTable *t,int fd,void *dst,unsigned count)
{
    UxFdSlot *f=slot(t,fd);if(!f || !(f->rights&UXFD_READ))return PVD_EINVAL;
    long n=uxfs_pread(&t->service->fs,f->path,dst,count,f->position);
    if(n>0)f->position+=(unsigned)n;
    return n;
}
long uxfd_write(UxFdTable *t,int fd,const void *src,unsigned count)
{
    UxFdSlot *f=slot(t,fd);unsigned pos,kind;int rc;
    if(!f || !(f->rights&UXFD_WRITE))return PVD_EINVAL;
    if(!count)return 0;
    pos=f->position;
    if(f->rights&UXFD_APPEND){rc=uxfs_info(&t->service->fs,f->path,&kind,&pos);if(rc)return rc;}
    long n=uxfs_pwrite(&t->service->fs,f->path,src,count,pos,0);
    if(n<0)return n;
    f->position=pos+(unsigned)n;rc=uxservice_sync(t->service);
    return rc ? rc : n;
}
long long uxfd_seek(UxFdTable *t,int fd,long long delta,int whence)
{
    UxFdSlot *f=slot(t,fd);
    unsigned int kind=0, size=0;
    long long base, next;
    if(!f) return PVD_EINVAL;
    if(whence==0) base=0;
    else if(whence==1) base=f->position;
    else if(whence==2) {
        if(uxfs_info(&t->service->fs,f->path,&kind,&size)) return PVD_ENOENT;
        base=size;
    } else return PVD_EINVAL;
    if((delta<0 && base < -delta) ||
       (delta>0 && base > (long long)UXFS_DATA_MAX-delta)) return PVD_EINVAL;
    next=base+delta;
    if(next<0 || next>UXFS_DATA_MAX) return PVD_EINVAL;
    f->position=(unsigned int)next;
    return next;
}
int uxfd_info(UxFdTable *t,int fd,unsigned int *kind,unsigned int *length)
{
    UxFdSlot *f=slot(t,fd);
    return f ? uxfs_info(&t->service->fs,f->path,kind,length) : PVD_EINVAL;
}
int uxfd_close(UxFdTable *t,int fd)
{
    UxFdSlot *f=slot(t,fd);
    if(!f) return PVD_EINVAL;
    f->used=0;
    return PVD_OK;
}
int uxfd_unlink(UxFdTable *t,const char *path)
{
    int rc;
    if(!t || !t->service) return PVD_EINVAL;
    rc=uxfs_remove(&t->service->fs,path);
    return rc ? rc : uxservice_sync(t->service);
}
