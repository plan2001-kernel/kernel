#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uxfd.h"
static Pvd view;
static UxService service;
static UxFdTable a,b;
static char out[512];
static unsigned int used;
static int emit(void *ctx,const void *buf,unsigned int n)
{
    (void)ctx;
    if(n>sizeof(out)-used)return -1;
    memcpy(out+used,buf,n);
    used+=n;
    return 0;
}
int main(void)
{
    UxShellIO io={0};
    io.emit=emit;
    char buf[32]={0};
    int fa,fb;
    assert(pvd_bootstrap(&view)==0);
    uxservice_init(&service,&view);
    uxfd_init(&a,&service);
    uxfd_init(&b,&service);
    fa=uxfd_open(&a,"/tmp/shared",UXFD_WRITE|UXFD_CREATE|UXFD_TRUNC);
    assert(fa>=3);
    assert(uxfd_write(&a,fa,"alpha",5)==5);
    fb=uxfd_open(&b,"/tmp/shared",UXFD_READ);
    assert(fb>=3);
    assert(uxfd_read(&b,fb,buf,2)==2 && memcmp(buf,"al",2)==0);
    assert(uxfd_read(&b,fb,buf+2,8)==3 && !memcmp(buf,"alpha",5));
    assert(uxfd_seek(&b,fb,0,0)==0);
    assert(uxfd_read(&b,fb,buf,5)==5);
    assert(uxfd_close(&b,fb)==0);
    assert(uxfd_close(&a,fa)==0);
    assert(uxservice_command(&service,"cat /tmp/shared",&io)==0);
    assert(used==5 && !memcmp(out,"alpha",5));
    assert(uxservice_command(&service,"echo beta >> /tmp/shared",&io)==0);
    fb=uxfd_open(&b,"/tmp/shared",UXFD_READ);
    assert(uxfd_read(&b,fb,buf,11)==10);
    assert(!memcmp(buf,"alphabeta\n",10));
    assert(uxfd_close(&b,fb)==0);
    assert(uxfd_unlink(&a,"/tmp/shared")==0);
    assert(uxfd_open(&b,"/tmp/shared",UXFD_READ)==PVD_ENOENT);
    assert(uxfd_open(&b,"/tmp/../tmp/../missing/file",UXFD_WRITE|UXFD_CREATE)==PVD_ENOENT);
    puts("PASS: shared UXFS service, two FD views, path checks, shell writes, unlink");
    return 0;
}
