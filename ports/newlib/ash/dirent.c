/* Bounded POSIX dirent adapter over the same authoritative UxService as
 * the shell. A directory snapshot is a userspace view, never GE/O authority.
 * No PATH-based kernel VFS, no host libc and no magic raw OID.
 */
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../full/plan2001_full.h"
#include "../full/process.h"
#include "../full/project_fs.h"

struct p2001_dir {
    unsigned used, next, capacity;
    char base[4096];
    struct dirent *entries;
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
    if(!n || n>=sizeof(d->entries[0].d_name))return -1;
    if(d->used==d->capacity){unsigned cap=d->capacity?d->capacity*2:16;void *p=realloc(d->entries,cap*sizeof *d->entries);if(!p)return -1;d->entries=p;d->capacity=cap;}
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
static int emit_project(void *context,const char *name,unsigned n,unsigned kind)
{
    struct p2001_dir *d=context;
    if(d->used==d->capacity){unsigned cap=d->capacity?d->capacity*2:16;void *p=realloc(d->entries,cap*sizeof *d->entries);if(!p)return -1;d->entries=p;d->capacity=cap;}
    if(add_entry(d,name,n,kind))return -1;
    char full[4096];size_t base=strlen(d->base);if(base+n+2>=sizeof full)return -1;
    memcpy(full,d->base,base);if(base>1)full[base++]='/';memcpy(full+base,name,n);full[base+n]=0;
    struct stat st;if(stat(full,&st))return -1;d->entries[d->used-1].d_ino=st.st_ino;return 0;
}
DIR *opendir(const char *path)
{
    struct p2001_dir *d;
    int rc;
    if(!p9_project_root()) { errno=ENOSYS; return NULL; }
    if(!path) { errno=EFAULT; return NULL; }
    struct stat info;
    if(stat(path,&info))return NULL;
    if(!S_ISDIR(info.st_mode)){errno=ENOTDIR;return NULL;}
    d=(struct p2001_dir *)malloc(sizeof(*d));
    if(!d) { errno=ENOMEM; return NULL; }
    memset(d,0,sizeof(*d));
    d->capacity=16;
    if(p9_multicall_path) {
        unsigned i;
        for(i=0;p9_multicall_path(i);++i) {
            ++d->capacity;
        }
    }
    d->entries=calloc(d->capacity,sizeof(*d->entries));
    if(!d->entries) { free(d); errno=ENOMEM; return NULL; }
    if(strlen(path)>=sizeof d->base){free(d->entries);free(d);errno=ENAMETOOLONG;return NULL;}
    if(path[0]=='/'){strcpy(d->base,path);rc=0;}else{rc=getcwd(d->base,sizeof d->base)?0:-1;if(!rc && strcmp(path,".")){size_t n=strlen(d->base);if(n+strlen(path)+2>=sizeof d->base)rc=-1;else{if(n>1)strcat(d->base,"/");strcat(d->base,path);}}}
    if(!rc && add_entry(d,".",1,UXFS_DIR)==0 &&
       add_entry(d,"..",2,UXFS_DIR)==0)
        rc=p9_project_list(path,emit_project,d);
    if(!rc && p9_multicall_path) {
        unsigned i;
        const char *entry;
        for(i=0;(entry=p9_multicall_path(i))!=NULL;++i) {
            const char *name=strrchr(entry,'/');
            size_t parent=(size_t)(name-entry);
            uint64_t oid;
            if(strlen(d->base)!=parent || strncmp(entry,d->base,parent) ||
               !p9_process_executable(entry,&oid)) continue;
            if(add_entry(d,name+1,(unsigned)strlen(name+1),UXFS_FILE)) { rc=PVD_ENOSPC; break; }
            d->entries[d->used-1].d_ino=(ino_t)oid;
        }
    }
    if(rc) { free(d->entries); free(d); (void)dir_error(rc); return NULL; }
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
    free(dir->entries);
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
