#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "libbb.h"
#include "obj.h"
#include "vobj.h"

static unsigned char region[4096];
static unsigned char received[256];
static unsigned int used, mapped, unmapped, permitted = 1;
long obj_list(void *records, unsigned long long count)
{
    VobjListRec rec[2] = {{0}};
    assert(count >= 2);
    rec[0].handle=4; rec[0].kind=VkindRegion; rec[0].rights=Vread;
    rec[1].handle=5; rec[1].kind=VkindRegion; rec[1].rights=Vwrite;
    if (!permitted) rec[0].rights=Vwrite;
    memcpy(records,rec,sizeof(rec));
    return 2;
}
long obj_region_map(int handle, unsigned long long addr)
{
    assert(handle==4 && addr==0);
    ++mapped;
    return (long)region;
}
long obj_region_unmap(unsigned long long addr)
{
    assert(addr==(unsigned long long)region);
    ++unmapped;
    return 0;
}
ssize_t full_write(int fd,const void *data,size_t n)
{
    assert(fd==1 && used+n<=sizeof(received));
    memcpy(received+used,data,n);
    used+=(unsigned int)n;
    return (ssize_t)n;
}
extern int p2001_ls_main(int,char**);
int main(void)
{
    char *argv[]={"ls","/",0};
    const char *names="bin\ndev\nusr\n";
    unsigned int i, n=(unsigned int)strlen(names);
    region[0]=(unsigned char)n;
    for(i=1;i<8;++i)region[i]=0;
    memcpy(region+8,names,n);
    assert(p2001_ls_main(2,argv)==0);
    assert(used==n && !memcmp(received,names,n));
    assert(mapped==1 && unmapped==1);
    assert(p2001_ls_main(2,(char*[]){"ls","/etc",0})==64);
    permitted=0;
    assert(p2001_ls_main(2,argv)==1);
    assert(mapped==1 && unmapped==1);
    permitted=1;
    region[0]=255; region[1]=255;
    assert(p2001_ls_main(2,argv)==1);
    assert(mapped==2 && unmapped==2);
    puts("PASS: native ls child reads only granted RO region and writes validated listing");
    return 0;
}
