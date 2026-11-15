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
long uxfd_read(UxFdTable *t,int fd,void *dst,unsigned int count)
{
    unsigned char scratch[UXFS_DATA_MAX];
    unsigned int length=0,n;
    UxFdSlot *f=slot(t,fd);
    int rc;
    if(!f || !(f->rights&UXFD_READ) || (!dst && count)) return PVD_EINVAL;
    if(!count) return 0;
    rc=uxfs_read(&t->service->fs,f->path,scratch,sizeof scratch,&length);
    if(rc) return rc;
    if(f->position>=length) return 0;
    n=length-f->position;
    if(n>count) n=count;
    copybytes(dst,scratch+f->position,n);
    f->position+=n;
    return n;
}
long uxfd_write(UxFdTable *t,int fd,const void *src,unsigned int count)
{
    unsigned char scratch[UXFS_DATA_MAX];
    unsigned int old=0,kind=0,size=0,pos;
    UxFdSlot *f=slot(t,fd);
    int rc;
    if(!f || !(f->rights&UXFD_WRITE) || (!src && count))
        return PVD_EINVAL;
    if(!count) return 0;
    rc=uxfs_info(&t->service->fs,f->path,&kind,&size);
    if(rc) return rc;
    if(kind!=UXFS_FILE) return PVD_EINVAL;
    rc=uxfs_read(&t->service->fs,f->path,scratch,sizeof scratch,&old);
    if(rc) return rc;
    pos=(f->rights&UXFD_APPEND) ? old : f->position;
    if(pos>UXFS_DATA_MAX || count>UXFS_DATA_MAX-pos) return PVD_ENOSPC;
    while(old<pos) scratch[old++]=0;
    copybytes(scratch+pos,src,count);
    if(old<pos+count) old=pos+count;
    rc=uxfs_write(&t->service->fs,f->path,scratch,old,0);
    if(rc) return rc;
    f->position=pos+count;
    rc=uxservice_sync(t->service);
    return rc ? rc : (long)count;
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
