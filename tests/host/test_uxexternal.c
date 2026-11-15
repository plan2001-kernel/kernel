/* Host contract test for F5's native external ls adapter.
 * Uses real UXFS listing and real UXSPAWN option packing; mocks only
 * capability-region kernel calls and process completion. QEMU separately
 * verifies actual SPAWN with executable GE/O authority.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "obj.h"
#include "vobj.h"
#include "uxexternal.h"
/* The real kernel already supplies WAIT/WAKE. Host tests never execute
 * a real pipeline, but UXPIPE is linked to validate the service boundary. */
long region_wait(unsigned long long b,unsigned long long o,
                 unsigned long long e,long long t)
{ (void)b; (void)o; (void)e; (void)t; return -1; }
long region_wake(unsigned long long b,unsigned long long o,
                 unsigned long long n)
{ (void)b; (void)o; (void)n; return 0; }
static Pvd pvd;
static UxFs fs;
static UxExternal external;
static unsigned long long source[512], output_region[512];
static unsigned int created, launched, closed, unmapped, terminated;
static int broken_child;
long obj_region_create(unsigned long long size, ObjId *out)
{
    (void)out;
    assert(size==4096);
    ++created;
    return created % 2 ? 11 : 12;
}
long obj_region_map(int handle, unsigned long long addr)
{
    assert(handle==11 && addr==0);
    return (long)source;
}
long obj_region_map_flags(int handle, unsigned long long addr,
                          unsigned int flags)
{
    assert(handle==12 && addr==0 && flags==ObjRegionMapReadonly);
    memset(output_region,0,sizeof(output_region));
    return (long)output_region;
}
long obj_region_unmap(unsigned long long addr)
{
    assert(addr==(unsigned long long)source ||
           addr==(unsigned long long)output_region);
    ++unmapped;
    return 0;
}
long obj_close(int handle)
{
    assert(handle==11 || handle==12 || handle==33);
    ++closed;
    return 0;
}
long obj_process_terminate(int handle)
{
    assert(handle==33);
    ++terminated;
    return 0;
}
long obj_ospawn(int volume, unsigned long long oid, char **argv, int argc,
                 struct Ospawn *opts)
{
    const unsigned long long *args = (const unsigned long long *)opts;
    unsigned long long n = __atomic_load_n(source,__ATOMIC_ACQUIRE);
    assert(volume==7 && oid==99 && argc==3);
    assert(!strcmp(argv[0],"busybox") && !strcmp(argv[1],"ls") &&
           !strcmp(argv[2],"/"));
    assert(args[0]==~0ULL && args[2]==2 && args[3]==2);
    assert(args[4]==11 && args[5]==Vread);
    assert(args[6]==12 && args[7]==Vwrite);
    assert(n<=4088);
    memcpy((unsigned char *)output_region+8,(unsigned char *)source+8,n);
    __atomic_store_n(output_region,n,__ATOMIC_RELEASE);
    ++launched;
    return 33;
}
long obj_process_wait(int handle, ObjProcessInfo *info, long long timeout)
{
    assert(handle==33 && timeout==30000);
    info->state=ObjProcCompleted;
    info->cause=broken_child ? ObjProcCauseFault : ObjProcCauseExit;
    info->msglen=0;
    return 0;
}
int main(void)
{
    char *root_args[]={"ls","/",0};
    char *tmp_args[]={"ls","/tmp/empty",0};
    char *bad_args[]={"ls","/missing",0};
    unsigned char result[UXFS_DATA_MAX];
    unsigned int n=999;
    assert(pvd_bootstrap(&pvd)==PVD_OK);
    uxfs_init(&fs,&pvd);
    external.fs=&fs;
    external.executable_volume=0;
    external.executable_oid=99;
    assert(uxexternal_ls(&external,"ls",root_args,2,result,sizeof(result),&n)<0);
    assert(created==0);
    external.executable_volume=7;
    assert(uxexternal_ls(&external,"ls",root_args,2,result,sizeof(result),&n)==0);
    assert(n==strlen("bin\ndev\netc\nhome\ntmp\nusr\nvar\n"));
    assert(!memcmp(result,"bin\ndev\netc\nhome\ntmp\nusr\nvar\n",n));
    assert(launched==1 && closed==3 && unmapped==2 && !terminated);
    assert(uxfs_mkdir(&fs,"/tmp/empty")==PVD_OK);
    assert(uxexternal_ls(&external,"ls",tmp_args,2,result,sizeof(result),&n)==0);
    assert(n==0 && launched==2);
    assert(uxexternal_ls(&external,"ls",bad_args,2,result,sizeof(result),&n)<0);
    assert(launched==2);
    broken_child=1;
    assert(uxexternal_ls(&external,"ls",root_args,2,result,sizeof(result),&n)<0);
    assert(terminated==1);
    puts("PASS: F5 external ls only with explicit launch authority, RO/RW grants, empty dirs, child failure");
    return 0;
}
