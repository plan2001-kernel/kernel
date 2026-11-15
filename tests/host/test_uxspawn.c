#include <assert.h>
#include <stdio.h>
#include "uxspawn.h"
#include "obj.h"
#include "vobj.h"
static unsigned int launches, waits;
long obj_ospawn(int volume, unsigned long long oid, char **argv,
                int argc, struct Ospawn *options)
{
    const unsigned long long *abi = (const unsigned long long *)options;
    assert(volume==7 && oid==99 && argc==2 && argv[0][0]=='l');
    assert(abi[0]==~0ULL && abi[1]==0 && abi[2]==2);
    assert(abi[3]==2); /* explicit stdin and stdout grant count */
    assert(abi[4]==11 && abi[5]==Vread);
    assert(abi[6]==12 && abi[7]==Vwrite);
    ++launches;
    return 55; /* opaque Process Handle, not PID */
}
long obj_process_wait(int process, ObjProcessInfo *info, long long timeout)
{
    assert(process==55 && timeout==1000);
    info->state=ObjProcCompleted;
    info->cause=ObjProcCauseExit;
    info->msglen=0;
    ++waits;
    return 0;
}
int main(void)
{
    UxLaunch launch={0};
    char *argv[]={"ls","/",0};
    int success=0, h;
    launch.executable_volume=7;
    launch.oid=99;
    launch.grant_count=2;
    launch.grant[0].handle=11;
    launch.grant[0].rights=Vread;
    launch.grant[1].handle=12;
    launch.grant[1].rights=Vwrite;
    h=uxspawn_start(&launch,argv,2);
    assert(h==55 && launches==1);
    assert(uxspawn_wait(h,1000,&success)==0 && success && waits==1);
    launch.grant[0].rights=Vcreate; /* cannot delegate arbitrary authority */
    assert(uxspawn_start(&launch,argv,2)<0 && launches==1);
    launch.grant[0].rights=Vread;
    launch.executable_volume=0; /* no ambient executable authority */
    assert(uxspawn_start(&launch,argv,2)<0 && launches==1);
    puts("PASS: native Ospawn/Oinheritv layout, attenuated fd grants, Process Handle");
    return 0;
}
