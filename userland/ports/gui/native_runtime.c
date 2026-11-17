/* Minimal C-library bridge for a native manifest application. No Unix launcher. */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "obj.h"
#include "sysctl.h"
#include "vobj.h"
extern long objinvoke(int,void *,long), objopen(unsigned long long,void *,long,unsigned long long,void *);
extern long sysctl(unsigned,unsigned,SysctlReq *,long);
extern long regionwait(unsigned long long,unsigned long long,unsigned long long,long long);
extern long regionwake(unsigned long long,unsigned long long,unsigned long long);
extern long brk_(void *);
extern void _exits(char *);
extern char end[];
extern int p2001_session_probe(void), main(void);
extern long p2001_session_write(int,const void *,size_t),p2001_session_read(int,void *,size_t);
long obj_list(void *p,unsigned long long n){SysctlReq r={0};r.version=SYSCTL_ABI_VERSION;r.data=p;r.datalen=n*sizeof(VobjListRec);return sysctl(SysctlScopeOwnProcess,SysctlHandleList,&r,sizeof r)<0?-1:r.result;}
long obj_region_map(int h,unsigned long long addr){ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpRegionMap;c.arg0=addr;return objinvoke(h,&c,sizeof c);}
long obj_region_unmap(unsigned long long addr){ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpRegionUnmap;c.arg0=addr;return objinvoke(0,&c,sizeof c);}
long region_wait(unsigned long long b,unsigned long long o,unsigned long long e,long long t){return regionwait(b,o,e,t);}
long region_wake(unsigned long long b,unsigned long long o,unsigned long long n){return regionwake(b,o,n);}
void *_sbrk(ptrdiff_t increment){static uintptr_t current; if(!current)current=((uintptr_t)end+7)&~(uintptr_t)7;uintptr_t old=(current+7)&~(uintptr_t)7;if(increment<0 || (uintptr_t)increment>UINTPTR_MAX-old-7){errno=ENOMEM;return (void *)-1;}uintptr_t next=old+increment;if(brk_((void *)((next+7)&~(uintptr_t)7))<0){errno=ENOMEM;return (void *)-1;}current=next;return (void *)old;}
_Noreturn void _exit(int status){_exits(status?"native GUI failure":"");for(;;);}
long _write(int fd,const void *p,size_t n){return p2001_session_write(fd,p,n);}
long _read(int fd,void *p,size_t n){return p2001_session_read(fd,p,n);}
int _close(int fd){(void)fd;errno=EBADF;return -1;}
long _lseek(int fd,long off,int whence){(void)fd;(void)off;(void)whence;errno=ESPIPE;return -1;}
int _fstat(int fd,struct stat *st){if(fd<0 || fd>2){errno=EBADF;return -1;}*st=(struct stat){0};st->st_mode=S_IFCHR;return 0;}
int _isatty(int fd){return fd>=0 && fd<=2;}
int _getpid(void){errno=ENOSYS;return -1;}
int _kill(int pid,int sig){(void)pid;(void)sig;errno=ENOSYS;return -1;}
_Noreturn void p9_start(int argc,char **argv){(void)argc;(void)argv;if(p2001_session_probe()!=1)_exit(125);exit(main());}
static int surface;
static uint32_t *pixels;
int p9_surface_handle(void){if(!surface){long h=objopen(ObjOpenConsoleSurface,0,0,Vread|Vwrite|Vmeta|Vdelegate,0);if(h>0)surface=h;}return surface;}
uint32_t *p9_surface_map(ObjSurfaceInfo *info){ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpSurfaceInfo;c.out=info;c.outlen=sizeof *info;if(!p9_surface_handle() || objinvoke(surface,&c,sizeof c)<0 || info->version!=OBJ_SURFACE_VERSION || info->format!=OBJ_SURFACE_XRGB8888 || !info->width || !info->height || info->stride!=info->width || info->pixel_bytes!=(uint64_t)info->width*info->height*4 || info->pixel_bytes>64u*1024*1024)return 0;if(!pixels){long p=obj_region_map(surface,0);if(p<=0)return 0;pixels=(void *)p;}return pixels;}
int p9_surface_present(void){ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpSurfacePresent;return objinvoke(surface,&c,sizeof c)<0?-1:0;}
int p9_surface_unmap(void){if(pixels && obj_region_unmap((uintptr_t)pixels)<0)return -1;pixels=0;return 0;}

/* Newlib was configured with unprefixed target hooks. These do not create
 * descriptor state: only the three native console streams are supported. */
long read(int fd,void *p,size_t n){return _read(fd,p,n);}
long write(int fd,const void *p,size_t n){return _write(fd,p,n);}
void *sbrk(ptrdiff_t n){return _sbrk(n);}
int close(int fd){return _close(fd);}
int fstat(int fd,struct stat *st){return _fstat(fd,st);}
int isatty(int fd){return _isatty(fd);}
long lseek(int fd,long o,int w){return _lseek(fd,o,w);}
int kill(int p,int s){return _kill(p,s);}
int getpid(void){return _getpid();}
