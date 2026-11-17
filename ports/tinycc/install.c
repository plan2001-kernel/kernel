/* Bounded installation into a real ObjectSpace-backed Unix file view.
 * Each file publication is atomic; a failed bundle never earns a completion
 * marker and may be retried. Existing different files fail without overwrite. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "install.h"
#include "project_fs.h"
extern long objopen(unsigned long long,void *,long,unsigned long long,void *);
static unsigned long long number(const unsigned char *p,unsigned n)
{
    unsigned long long v=0;while(n--)v=(v<<8)|*p++;return v;
}
static int valid(const char *path)
{
    if(!*path || *path=='/')return 0;
    while(*path){
        const char *start=path;
        while(*path && *path!='/')path++;
        size_t n=path-start;
        if(!n || n>255 || (n==1 && *start=='.') ||
           (n==2 && start[0]=='.' && start[1]=='.'))return 0;
        if(*path && !*++path)return 0;
    }
    return 1;
}
static int entry(const char *path,const unsigned char *data,size_t n,int volume)
{
    char absolute[512];absolute[0]='/';strcpy(absolute+1,path);
    char *last=strrchr(absolute,'/');
    for(char *at=absolute+1;at<last;at++)if(*at=='/'){
        *at=0;
        if(mkdir(absolute,0700)<0 && errno!=EEXIST)return -1;
        *at='/';
    }
    *last=0;
    if(last>absolute && mkdir(absolute,0700)<0 && errno!=EEXIST)return -1;
    PjObject parent={0},object={0};
    if(p9_project_resolve(last==absolute?"/":absolute,&parent))return -1;
    const char *label=last+1;
    int rc=pj_resolve(&parent,label,&object);
    if(rc && errno==ENOENT){
        if(!strcmp(path,"usr/bin/tcc")){
            ObjOpenGeOidReq req={volume,0,P2001_TCC_OID};
            int h=(int)objopen(ObjOpenGeOid,&req,sizeof req,Vread|Vmeta|Vdelegate|Vexecute,0);
            if(h<=0){pj_close(&parent);return -1;}
            rc=pj_from_handle(&object,h);
            if(!rc)rc=pj_link(&parent,label,&object);
        }else{
            rc=pj_create(&parent,label,VkindGe,data,n,&object);
        }
    }
    pj_close(&parent);
    if(rc){pj_close(&object);return -1;}
    unsigned char *blob;size_t size,raw,meta;
    rc=pj_blob(&object,&blob,&size,&raw,&meta);pj_close(&object);
    if(rc)return -1;
    rc=raw==n && !memcmp(blob,data,n)?0:-1;free(blob);
    if(rc)errno=EEXIST;
    return rc;
}
int p2001_toolchain_install(const void *buffer,size_t n,int volume)
{
    if(!buffer || n<12 || n>=64u*1024u*1024u){errno=EINVAL;return -1;}
    const unsigned char *p=buffer,*end=p+n;
    if(n<12 || memcmp(p,"P2TCB001",8)){errno=EINVAL;return -1;}
    unsigned long long count=number(p+8,4);p+=12;
    if(!count || count>4096){errno=E2BIG;return -1;}
    /* Validate the entire framing and every path before mutating the root. */
    const unsigned char *first=p;
    for(unsigned pass=0;pass<2;pass++){
        p=first;
        for(unsigned long long i=0;i<count;i++){
            if((size_t)(end-p)<10){errno=EINVAL;return -1;}
            size_t len=(size_t)number(p,2);
            unsigned long long size=number(p+2,8);p+=10;
            if(!len || len>=511 || (size_t)(end-p)<len ||
               size>(unsigned long long)(end-p-len) || memchr(p,0,len)){
                errno=EINVAL;return -1;
            }
            char name[512];memcpy(name,p,len);name[len]=0;p+=len;
            if(!valid(name)){errno=EINVAL;return -1;}
            if(pass && entry(name,p,(size_t)size,volume))return -1;
            p+=(size_t)size;
        }
        if(p!=end){errno=EINVAL;return -1;}
    }
    return 0;
}
