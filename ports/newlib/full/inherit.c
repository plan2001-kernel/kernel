#include <errno.h>
#include <string.h>
#include "process.h"
#include "sysctl.h"
#include "vobj.h"
int p9_plan_add(P9Plan *p,int handle,uint64_t rights)
{
    unsigned i;
    if(handle<=0 || !rights) { errno=EBADF; return -1; }
    for(i=0;i<p->count;++i) if(p->grants[i].handle==handle) {
        p->grants[i].rights|=rights;
        return (int)i;
    }
    if(p->count==P9_GRANTS) { errno=E2BIG; return -1; }
    p->grants[p->count]=(P9Grant){handle,rights};
    return (int)p->count++;
}
int p9_plan_resolve(int *handles,unsigned count)
{
    VobjListRec records[P9_GRANTS];
    SysctlReq req={0};
    long n;
    unsigned i,j;
    if(count>P9_GRANTS) { errno=E2BIG; return -1; }
    req.version=SYSCTL_ABI_VERSION;
    req.data=records;
    req.datalen=sizeof records;
    n=sysctl(SysctlScopeOwnProcess,SysctlHandleList,&req,sizeof req);
    if(n!=(long)count) { errno=EACCES; return -1; }
    memset(handles,0,count*sizeof(*handles));
    for(i=0;i<count;++i) {
        /* RFORKEX destinations and SPAWN list order are explicit slots.
         * SYSCTL supplies the generation-tagged Handle, never a guess. */
        j=(records[i].handle&255u)-1u;
        if(j>=count || handles[j]) { errno=EACCES; return -1; }
        handles[j]=(int)records[i].handle;
    }
    return 0;
}
