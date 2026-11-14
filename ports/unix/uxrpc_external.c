/* Trusted shell-side file RPC endpoint. Two producer/consumer UXPIPE
 * channels, four region grants, ONE executable capability. No volume
 * storage grant passes to the child. Each request uses UxFdTable on the
 * canonical UxService instance, and each write commits through GE/O.
 */
#include "uxrpc_external.h"
#include "uxrpc.h"
#include "uxspawn.h"
#include "obj.h"
#include "vobj.h"

#define UXRPC_MAX_REQUESTS 64u
#define UXRPC_CHILD_TIMEOUT 30000LL

extern long obj_region_create(unsigned long long,ObjId *);
extern long obj_region_map(int,unsigned long long);
extern long obj_region_map_flags(int,unsigned long long,unsigned int);
extern long obj_region_unmap(unsigned long long);
extern long obj_close(int);
extern long obj_process_terminate(int);
extern long p2001_session_write(int,const void *,unsigned long);

static int same(const char *a,const char *b)
{
    if(!a || !b) return 0;
    while(*a && *a==*b) {++a;++b;}
    return *a==*b;
}
int uxrpc_external_run(UxService *service,int volume,UxOid oid,
                       char *const *argv,unsigned int argc,void *output,
                       unsigned int capacity,unsigned int *size)
{
    UxLaunch launch={0};
    UxPipe request,response;
    UxRpcServer server;
    int h[4]={-1,-1,-1,-1},process=-1,exited_ok=0,status=-1;
    int stage=0;
    unsigned long long maps[4]={0,0,0,0};
    char *args[5]={"uxrpc-client",0,0,0,0};
    unsigned int i;
    if(!service || !service->fs.view || volume<=0 || !oid || !argv ||
       !output || !size || !capacity || argc<3 || argc>4 ||
       !same(argv[0],"uxfile") || !argv[1] || !argv[2] ||
       (argc==4 && !argv[3])) return -1;
    if((same(argv[1],"get") || same(argv[1],"rm")) ? argc!=3 :
       (same(argv[1],"put") || same(argv[1],"append")) ? argc!=4 : 1)
        return -1;
    args[1]=argv[1];args[2]=argv[2];
    if(argc==4)args[3]=argv[3];
    for(i=0;i<4;++i) {
        long mapped;
        stage=1;
        h[i]=(int)obj_region_create(UXPIPE_PAGE,0);
        if(h[i]<=0) goto cleanup;
        stage=2;
        mapped=obj_region_map(h[i],0);
        if(mapped<=0) goto cleanup;
        maps[i]=(unsigned long long)mapped;
    }
    stage=3;
    if(uxpipe_format((UxPipeData *)maps[0],(UxPipeAck *)maps[1]) ||
       uxpipe_format((UxPipeData *)maps[2],(UxPipeAck *)maps[3]))
        goto cleanup;
    stage=4;
    for(i=0;i<4;++i) {
        if(obj_region_unmap(maps[i])<0) goto cleanup;
        maps[i]=0;
    }
    stage=5;
    /* Parent: request reader RO/RW; response writer RW/RO. */
    for(i=0;i<4;++i) {
        long mapped=(i==0 || i==3) ?
            obj_region_map_flags(h[i],0,ObjRegionMapReadonly) :
            obj_region_map(h[i],0);
        if(mapped<=0) goto cleanup;
        maps[i]=(unsigned long long)mapped;
    }
    stage=6;
    if(uxpipe_bind(&request,(UxPipeData *)maps[0],(UxPipeAck *)maps[1],
                   maps[0],maps[1],UXPIPE_READER) ||
       uxpipe_bind(&response,(UxPipeData *)maps[2],(UxPipeAck *)maps[3],
                   maps[2],maps[3],UXPIPE_WRITER) ||
       uxrpc_server_init(&server,service,"/tmp",&request,&response))
        goto cleanup;
    launch.executable_volume=volume;
    launch.oid=oid;
    launch.grant_count=4;
    launch.grant[0].handle=h[0];launch.grant[0].rights=Vwrite;
    launch.grant[1].handle=h[1];launch.grant[1].rights=Vread;
    launch.grant[2].handle=h[2];launch.grant[2].rights=Vread;
    launch.grant[3].handle=h[3];launch.grant[3].rights=Vwrite;
    stage=7;
    process=uxspawn_start(&launch,args,(int)argc);
    if(process<=0) goto cleanup;
    stage=8;
    for(i=0;i<UXRPC_MAX_REQUESTS;++i) {
        int rc=uxrpc_server_once(&server);
        if(rc==1) break;
        if(rc<0) goto cleanup;
    }
    stage=9;
    if(i==UXRPC_MAX_REQUESTS ||
       uxspawn_wait(process,UXRPC_CHILD_TIMEOUT,&exited_ok)!=0 ||
       !exited_ok || server.output_size>capacity) goto cleanup;
    for(i=0;i<server.output_size;++i)
        ((unsigned char *)output)[i]=server.output[i];
    *size=server.output_size;
    status=0;
cleanup:
    if(status!=0) {
        char marker[]="PLAN2001-UXRPC-FAIL-STAGE-0\n";
        for(i=0;i<sizeof(marker)-1;++i)
            if(marker[i]=='0') marker[i]=(char)('0'+stage);
        (void)p2001_session_write(1,marker,sizeof(marker)-1);
    }
    if(process>0) {
        if(status!=0) (void)obj_process_terminate(process);
        (void)obj_close(process);
    }
    for(i=0;i<4;++i) {
        if(maps[i]) (void)obj_region_unmap(maps[i]);
        if(h[i]>0) (void)obj_close(h[i]);
    }
    return status;
}
