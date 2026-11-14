/* Bounded POSIX dirent adapter over the same authoritative UxService as
 * the shell. A directory snapshot is a userspace view, never GE/O authority.
 * No PATH-based kernel VFS, no host libc and no magic raw OID.
 */
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../full/plan2001_full.h"

struct p2001_dir {
    unsigned used, next;
    UxService *service;
    char base[PVD_PATH_MAX];
    struct dirent entries[UXFS_NODES+2];
};

static int dir_error(int code)
{
    switch(code) {
    case PVD_ENOENT: errno=ENOENT; break;
    case PVD_EACCES: errno=EACCES; break;
    case PVD_ENOSPC: errno=ENOSPC; break;
    case PVD_ENAMETOOLONG: errno=ENAMETOOLONG; break;
    default: errno=EIO; break;
    }
    return -1;
}
static int add_entry(struct p2001_dir *d,const char *name,unsigned int n,
                     unsigned int kind)
{
    struct dirent *e;
    unsigned int i;
    if(!n || n>=sizeof(d->entries[0].d_name) ||
       d->used>=UXFS_NODES+2) return -1;
    e=&d->entries[d->used];
    memset(e,0,sizeof *e);
    for(i=0;i<n;++i) e->d_name[i]=name[i];
    e->d_name[n]=0;
    e->d_ino=(ino_t)(d->used+1);
    e->d_off=(off_t)(d->used+1);
    e->d_reclen=(unsigned short)sizeof(*e);
    e->d_type=kind==UXFS_DIR ? DT_DIR : kind==UXFS_FILE ? DT_REG : DT_UNKNOWN;
    ++d->used;
    return 0;
}
static int emit_child(void *context,const void *name,unsigned int n)
{
    struct p2001_dir *d=(struct p2001_dir *)context;
    char full[PVD_PATH_MAX];
    unsigned int base=(unsigned int)strlen(d->base);
    unsigned int pos=0,kind=0,size=0;
    const unsigned char *p=(const unsigned char *)name;
    int rc;
    if(!n || n>PVD_LABEL_MAX || base+n+2>=sizeof full)
        return -1;
    memcpy(full,d->base,base);
    pos=base;
    if(pos>1) full[pos++]='/';
    memcpy(full+pos,p,n);
    full[pos+n]=0;
    rc=uxfs_info(&d->service->fs,full,&kind,&size);
    return rc ? -1 : add_entry(d,(const char *)p,n,kind);
}
DIR *opendir(const char *path)
{
    UxService *service=p9_full_file_service();
    struct p2001_dir *d;
    unsigned int kind=0,size=0;
    int rc;
    if(!service) { errno=ENOSYS; return NULL; }
    if(!path) { errno=EFAULT; return NULL; }
    rc=uxfs_info(&service->fs,path,&kind,&size);
    if(rc) { (void)dir_error(rc); return NULL; }
    if(kind!=UXFS_DIR) { errno=ENOTDIR; return NULL; }
    d=(struct p2001_dir *)malloc(sizeof(*d));
    if(!d) { errno=ENOMEM; return NULL; }
    memset(d,0,sizeof(*d));
    d->service=service;
    rc=uxfs_path(&service->fs,path,d->base);
    if(!rc && add_entry(d,".",1,UXFS_DIR)==0 &&
       add_entry(d,"..",2,UXFS_DIR)==0)
        rc=uxfs_list(&service->fs,path,emit_child,d);
    if(rc) { free(d); (void)dir_error(rc); return NULL; }
    return d;
}
struct dirent *readdir(DIR *dir)
{
    if(!dir) { errno=EBADF; return NULL; }
    if(dir->next==dir->used) return NULL; /* EOF, do not modify errno */
    return &dir->entries[dir->next++];
}
int closedir(DIR *dir)
{
    if(!dir) { errno=EBADF; return -1; }
    free(dir);
    return 0;
}
void rewinddir(DIR *dir)
{
    if(dir) dir->next=0;
}
long telldir(DIR *dir)
{
    if(!dir) { errno=EBADF; return -1; }
    return (long)dir->next;
}
void seekdir(DIR *dir,long offset)
{
    if(!dir || offset<0 || (unsigned long)offset>dir->used) {
        errno=EINVAL; return;
    }
    dir->next=(unsigned int)offset;
}
int dirfd(DIR *dir)
{
    (void)dir; errno=ENOSYS; return -1;
}
