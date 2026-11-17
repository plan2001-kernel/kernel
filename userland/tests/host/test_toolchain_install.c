/* Malformed bundles must not touch the delegated namespace. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "project_fs.h"
#include "install.h"
static int mutations;
int mkdir(const char *p,mode_t m){(void)p;(void)m;mutations++;return -1;}
int p9_project_resolve(const char *p,PjObject *o){(void)p;(void)o;mutations++;return -1;}
int pj_resolve(PjObject *a,const char *p,PjObject *o){(void)a;(void)p;(void)o;mutations++;return -1;}
void pj_close(PjObject *o){(void)o;}
int pj_from_handle(PjObject *o,int h){(void)o;(void)h;return -1;}
int pj_link(PjObject *a,const char *p,PjObject *o){(void)a;(void)p;(void)o;mutations++;return -1;}
int pj_create(PjObject *a,const char *p,unsigned k,const void *d,size_t n,PjObject *o)
{(void)a;(void)p;(void)k;(void)d;(void)n;(void)o;mutations++;return -1;}
int pj_blob(PjObject *o,unsigned char **p,size_t *n,size_t *r,size_t *m)
{(void)o;(void)p;(void)n;(void)r;(void)m;return -1;}
long objopen(unsigned long long a,void *b,long c,unsigned long long d,void *e)
{(void)a;(void)b;(void)c;(void)d;(void)e;mutations++;return -1;}
static void reject(const char *path,int truncate)
{
    unsigned char data[128]={0};memcpy(data,"P2TCB001",8);data[11]=1;
    size_t n=strlen(path);data[13]=n;data[21]=1;memcpy(data+22,path,n);data[22+n]='x';
    assert(p2001_toolchain_install(data,23+n-truncate,1)<0);
    assert(mutations==0);
}
int main(void)
{
    reject("../escape",0);reject("/absolute",0);reject("usr//bad",0);
    reject("usr/./bad",0);reject("usr/../bad",0);reject("usr/bad/",0);
    reject("usr/include/good.h",1);
    puts("PASS: toolchain path traversal and truncated bundle rejection before mutation");
    return 0;
}
