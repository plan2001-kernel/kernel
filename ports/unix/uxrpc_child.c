/* Restricted Unix file-service client: the same native ELF can be invoked
 * as a normal shell child. It owns NO GE/O volume Handle and cannot open one.
 * All open/read/write/close calls cross UXPIPE to the trusted UXFS owner.
 */
#include "uxrpc_child.h"
#include "uxrpc.h"
#include "obj.h"
#include "vobj.h"

extern long obj_list(void *, unsigned long long);
extern long obj_region_map(int, unsigned long long);
extern long obj_region_map_flags(int, unsigned long long, unsigned int);
extern long obj_region_unmap(unsigned long long);

static int same(const char *a,const char *b)
{
    if(!a || !b) return 0;
    while(*a && *a==*b) {++a;++b;}
    return *a==*b;
}
static unsigned int length(const char *s)
{
    unsigned int n=0;
    while(s && s[n] && n<UXFS_DATA_MAX) ++n;
    return n;
}
static int perform(UxRpcClient *client,int argc,char **argv)
{
    unsigned char bytes[UXFS_DATA_MAX];
    unsigned int n;
    long got;
    int fd,success=0;
    if(argc<3 || !argv[1] || !argv[2]) return -1;
    if(same(argv[1],"get")) {
        if(argc!=3) return -1;
        fd=uxrpc_open(client,argv[2],UXFD_READ);
        if(fd<0) return -1;
        got=uxrpc_read(client,fd,bytes,sizeof bytes);
        if(got<0 || uxrpc_close(client,fd)!=PVD_OK) return -1;
        return uxrpc_emit(client,bytes,(unsigned int)got)==PVD_OK ? 0 : -1;
    }
    if(same(argv[1],"rm")) {
        if(argc!=3) return -1;
        return uxrpc_unlink(client,argv[2])==PVD_OK ? 0 : -1;
    }
    if(!same(argv[1],"put") && !same(argv[1],"append"))
        return -1;
    if(argc!=4 || !argv[3]) return -1;
    n=length(argv[3]);
    if(n>=UXFS_DATA_MAX) return -1;
    for(unsigned int i=0;i<n;++i) bytes[i]=(unsigned char)argv[3][i];
    bytes[n++]='\n';
    fd=uxrpc_open(client,argv[2],UXFD_WRITE|UXFD_CREATE|
                  (same(argv[1],"append") ? UXFD_APPEND : UXFD_TRUNC));
    if(fd<0) return -1;
    got=uxrpc_write(client,fd,bytes,n);
    if(got==(long)n) success=1;
    if(uxrpc_close(client,fd)!=PVD_OK) success=0;
    return success ? 0 : -1;
}
int p2001_uxrpc_child_main(int argc,char **argv)
{
    VobjListRec records[16];
    static const unsigned rights[4]={Vwrite,Vread,Vread,Vwrite};
    UxPipe request,response;
    UxRpcClient client;
    int h[4]={0,0,0,0};
    unsigned long long map[4]={0,0,0,0};
    int region_count=0,ok=-1;
    long n=obj_list(records,16);
    if(argc<3 || argc>4 || !argv || !same(argv[0],"uxrpc-client") ||
       n!=4) return 1;
    for(int i=0;i<n;++i) {
        if(records[i].kind==VkindRegion) {
            if(region_count==4 ||
               records[i].rights!=rights[region_count]) goto cleanup;
            h[region_count++]=(int)records[i].handle;
        } else goto cleanup;
    }
    if(region_count!=4) goto cleanup;
    for(int i=0;i<4;++i) {
        long m=(i==1 || i==2) ?
            obj_region_map_flags(h[i],0,ObjRegionMapReadonly) :
            obj_region_map(h[i],0);
        if(m<=0) goto cleanup;
        map[i]=(unsigned long long)m;
    }
    if(uxpipe_bind(&request,(UxPipeData *)map[0],(UxPipeAck *)map[1],
                   map[0],map[1],UXPIPE_WRITER) ||
       uxpipe_bind(&response,(UxPipeData *)map[2],(UxPipeAck *)map[3],
                   map[2],map[3],UXPIPE_READER) ||
       uxrpc_client_init(&client,&request,&response)) goto cleanup;
    ok=perform(&client,argc,argv);
    if(uxrpc_quit(&client)!=PVD_OK) ok=-1;
cleanup:
    for(int i=0;i<4;++i)
        if(map[i]) (void)obj_region_unmap(map[i]);
    return ok==0 ? 0 : 1;
}
