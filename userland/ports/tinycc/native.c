/* TinyCC host operations over the delegated native file view. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "project_fs.h"
char *realpath(const char *path, char *resolved)
{
    char normal[4096];
    struct stat st;
    if (p9_project_normalize(path, normal) || stat(path, &st)) return NULL;
    if (!resolved) {
        resolved = malloc(strlen(normal)+1);
        if (!resolved) { errno=ENOMEM; return NULL; }
    }
    strcpy(resolved, normal);
    return resolved;
}

/* TinyCC repeatedly seeks across archive symbol tables. Snapshot each input
 * once per open so those small reads do not refetch the complete GE/O value.
 * This is compiler-local input buffering; ordinary Unix file reads retain
 * their live behavior. Outputs always use the real descriptor backend. */
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
typedef struct Input Input;
struct Input { Input *next; int fd; unsigned char *data; size_t size; off_t at; };
static Input *inputs;
static Input *input(int fd)
{
    for(Input *i=inputs;i;i=i->next)if(i->fd==fd)return i;
    return NULL;
}
int p2001_tcc_open(const char *path,int flags,...)
{
    mode_t mode=0;
    if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);}
    int fd=open(path,flags,mode);
    if(fd<0 || (flags&O_ACCMODE)!=O_RDONLY)return fd;
    struct stat st;
    if(fstat(fd,&st)<0 || !S_ISREG(st.st_mode))return fd;
    if(st.st_size<0 || st.st_size>64*1024*1024){close(fd);errno=EFBIG;return -1;}
    Input *i=calloc(1,sizeof *i);
    if(!i){close(fd);errno=ENOMEM;return -1;}
    i->size=(size_t)st.st_size;i->data=malloc(i->size?i->size:1);
    if(!i->data){free(i);close(fd);errno=ENOMEM;return -1;}
    size_t got=0;
    while(got<i->size){
        ssize_t n=read(fd,i->data+got,i->size-got);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){free(i->data);free(i);close(fd);errno=EIO;return -1;}
        got+=(size_t)n;
    }
    i->fd=fd;i->next=inputs;inputs=i;return fd;
}
ssize_t p2001_tcc_read(int fd,void *buf,size_t n)
{
    Input *i=input(fd);if(!i)return read(fd,buf,n);
    if((uint64_t)i->at>=i->size)return 0;
    size_t left=i->size-(size_t)i->at;if(n>left)n=left;
    memcpy(buf,i->data+i->at,n);i->at+=(off_t)n;return (ssize_t)n;
}
off_t p2001_tcc_lseek(int fd,off_t at,int whence)
{
    Input *i=input(fd);if(!i)return lseek(fd,at,whence);
    off_t base=whence==SEEK_SET?0:whence==SEEK_CUR?i->at:whence==SEEK_END?(off_t)i->size:-1;
    if(base<0 || at < -base || at>INT64_MAX-base){errno=EINVAL;return -1;}
    i->at=base+at;return i->at;
}
int p2001_tcc_close(int fd)
{
    Input **p=&inputs;
    while(*p){Input *i=*p;if(i->fd==fd){*p=i->next;free(i->data);free(i);break;}p=&i->next;}
    return close(fd);
}

#ifdef PLAN2001_TCC_DRIVER
#include <stdio.h>
#include "program.h"
extern int p2001_tcc_main(int,char **);
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--register")){
        if(argc!=3){fprintf(stderr,"usage: tcc --register PROGRAM\n");return 2;}
        if(p9_program_register(argv[2])){perror(p9_program_diagnostic());return 1;}
        printf("registered %s\n",argv[2]);return 0;
    }
    return p2001_tcc_main(argc,argv);
}
#endif
