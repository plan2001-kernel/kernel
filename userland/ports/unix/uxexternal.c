/* Native F5 external ls launcher, opt-in only.
 * A separate child sees an immutable listing snapshot (RO region), an empty
 * output frame (RW region) and NOTHING else. Execution uses a volume Handle
 * with Vexecute, NOT the Unix command's pathname.
 *
 * The current child is a Plan2001-specific listing applet, not BusyBox's
 * upstream ls.c. Only the caller's PVD grants names. This is intentionally
 * not a general POSIX exec or fork.
 */
#include "uxexternal.h"
#include "uxspawn.h"
#include "uxpipe.h"
#include "obj.h"
#include "vobj.h"

#define EXTERNAL_FRAME 4096u
#define EXTERNAL_DATA (EXTERNAL_FRAME - 8u)
#define EXTERNAL_TIMEOUT_MS 30000LL

extern long obj_region_create(unsigned long long size, ObjId *out);
extern long obj_region_map(int handle, unsigned long long addr);
extern long obj_region_map_flags(int handle, unsigned long long addr,
                                 unsigned int flags);
extern long obj_region_unmap(unsigned long long base);
extern long obj_close(int handle);
extern long obj_process_terminate(int handle);

typedef struct {
    unsigned char *data;
    unsigned int used;
} Listing;

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int emit_listing(void *ctx, const void *name, unsigned int n)
{
    Listing *list = (Listing *)ctx;
    const unsigned char *src = (const unsigned char *)name;
    unsigned int i;
    if (!n || n > PVD_LABEL_MAX || n+1u > EXTERNAL_DATA-list->used)
        return -1;
    for (i=0; i<n; ++i) {
        if (src[i] < 32 || src[i] >= 127 || src[i] == '/')
            return -1;
        list->data[list->used+i] = src[i];
    }
    list->used += n;
    list->data[list->used++] = '\n';
    return 0;
}
int uxexternal_ls(void *ctx, const char *command, char *const *argv,
                  unsigned int argc, void *output, unsigned int capacity,
                  unsigned int *size)
{
    UxExternal *runtime = (UxExternal *)ctx;
    Listing names;
    UxLaunch launch = {0};
    char *child_argv[3] = {"busybox", "ls", "/"};
    int source = -1, stdout_region = -1, process = -1, completed = 0;
    unsigned long long source_map = 0, stdout_map = 0, n;
    unsigned int i;
    int status = -1;
    if (!runtime || !runtime->fs || runtime->executable_volume <= 0 ||
        !runtime->executable_oid || !argv || !command || !output ||
        !size || !capacity || argc < 1 || argc > 2 ||
        !equal(command,"ls") || !equal(argv[0],"ls"))
        return -1;
    source = (int)obj_region_create(EXTERNAL_FRAME, 0);
    if (source <= 0) goto cleanup;
    stdout_region = (int)obj_region_create(EXTERNAL_FRAME, 0);
    if (stdout_region <= 0) goto cleanup;
    {
        long mapped = obj_region_map(source, 0);
        if (mapped < 0) goto cleanup;
        source_map = (unsigned long long)mapped;
    }
    {
        long mapped = obj_region_map_flags(stdout_region, 0,
                                           ObjRegionMapReadonly);
        if (mapped < 0) goto cleanup;
        stdout_map = (unsigned long long)mapped;
    }
    names.data = (unsigned char *)(source_map + 8);
    names.used = 0;
    if (uxfs_list(runtime->fs, argc == 2 ? argv[1] : ".",
                  emit_listing, &names) != PVD_OK)
        goto cleanup;
    /* The 64-bit length is an acquire/release publication word. The
     * contents are immutable after this store for the child lifetime.
     */
    __atomic_store_n((unsigned long long *)source_map,
                     (unsigned long long)names.used, __ATOMIC_RELEASE);
    launch.executable_volume = runtime->executable_volume;
    launch.oid = runtime->executable_oid;
    launch.grant_count = 2;
    launch.grant[0].handle = source;
    launch.grant[0].rights = Vread;
    launch.grant[1].handle = stdout_region;
    launch.grant[1].rights = Vwrite;
    process = uxspawn_start(&launch, child_argv, 3);
    if (process <= 0) goto cleanup;
    if (uxspawn_wait(process, EXTERNAL_TIMEOUT_MS, &completed) != 0 ||
        !completed) goto cleanup;
    n = __atomic_load_n((unsigned long long *)stdout_map,
                        __ATOMIC_ACQUIRE);
    if (n > EXTERNAL_DATA || n > capacity) goto cleanup;
    for (i = 0; i < (unsigned int)n; ++i)
        ((unsigned char *)output)[i] =
            ((const unsigned char *)(stdout_map + 8))[i];
    *size = (unsigned int)n;
    status = 0;
cleanup:
    if (process > 0) {
        if (status != 0) (void)obj_process_terminate(process);
        (void)obj_close(process);
    }
    if (stdout_map) (void)obj_region_unmap(stdout_map);
    if (source_map) (void)obj_region_unmap(source_map);
    if (stdout_region > 0) (void)obj_close(stdout_region);
    if (source > 0) (void)obj_close(source);
    return status;
}

/* Native SPAWN/UXPIPE echo | cat. The parent owns four region capabilities,
 * formats the buffers, unmaps all writers before delegation, and runs two
 * independent executable instances with attenuated rights. No kernel Unix
 * pipe, guessed OID or access to the Unix storage volume is introduced.
 */
int uxexternal_pipeline(void *ctx, char *const *left, unsigned int lcount,
                        char *const *right, unsigned int rcount,
                        void *output, unsigned int capacity,
                        unsigned int *size)
{
    UxExternal *runtime=(UxExternal *)ctx;
    int h[4]={-1,-1,-1,-1};
    unsigned long long maps[4]={0,0,0,0};
    UxPipe receiver;
    UxLaunch first={0},second={0};
    char *args[22], *rhs[3]={"busybox","cat",0};
    int producer=-1,consumer=-1,producer_ok=0,consumer_ok=0,status=-1;
    unsigned int i,used=0;
    if(!runtime || !runtime->executable_volume || !runtime->executable_oid ||
       !left || !right || !output || !size || !capacity ||
       !lcount || lcount>20 || rcount!=1 ||
       !equal(left[0],"echo") || !equal(right[0],"cat"))
        return -1;
    args[0]="busybox";
    for(i=0;i<lcount;++i) args[i+1]=left[i];
    args[lcount+1]=0;
    for(i=0;i<4;++i) {
        long mapped;
        h[i]=(int)obj_region_create(UXPIPE_PAGE,0);
        if(h[i]<=0) goto cleanup;
        mapped=obj_region_map(h[i],0);
        if(mapped<=0) goto cleanup;
        maps[i]=(unsigned long long)mapped;
    }
    if(uxpipe_format((UxPipeData *)maps[0],(UxPipeAck *)maps[1]) ||
       uxpipe_format((UxPipeData *)maps[2],(UxPipeAck *)maps[3]))
        goto cleanup;
    for(i=0;i<4;++i) {
        if(obj_region_unmap(maps[i])<0) goto cleanup;
        maps[i]=0;
    }
    { long ro=obj_region_map_flags(h[2],0,ObjRegionMapReadonly);
      if(ro<=0) goto cleanup;
      maps[2]=(unsigned long long)ro;
    }
    { long rw=obj_region_map(h[3],0);
      if(rw<=0) goto cleanup;
      maps[3]=(unsigned long long)rw;
    }
    if(uxpipe_bind(&receiver,(UxPipeData *)maps[2],(UxPipeAck *)maps[3],
                   maps[2],maps[3],UXPIPE_READER))
        goto cleanup;
    second.executable_volume=runtime->executable_volume;
    second.oid=runtime->executable_oid;
    second.grant_count=4;
    second.grant[0].handle=h[0];second.grant[0].rights=Vread;
    second.grant[1].handle=h[1];second.grant[1].rights=Vwrite;
    second.grant[2].handle=h[2];second.grant[2].rights=Vwrite;
    second.grant[3].handle=h[3];second.grant[3].rights=Vread;
    consumer=uxspawn_start(&second,rhs,2);
    if(consumer<=0) goto cleanup;
    first.executable_volume=runtime->executable_volume;
    first.oid=runtime->executable_oid;
    first.grant_count=2;
    first.grant[0].handle=h[0];first.grant[0].rights=Vwrite;
    first.grant[1].handle=h[1];first.grant[1].rights=Vread;
    producer=uxspawn_start(&first,args,(int)lcount+1);
    if(producer<=0) goto cleanup;
    for(;;) {
        unsigned char temp[256];
        long got=uxpipe_read(&receiver,temp,sizeof(temp),-1);
        if(got<0) goto cleanup;
        if(!got) break; /* cat published real EOF */
        if((unsigned long)got>capacity-used) goto cleanup;
        for(i=0;i<(unsigned int)got;++i)
            ((unsigned char *)output)[used+i]=temp[i];
        used+=(unsigned int)got;
    }
    if(uxspawn_wait(producer,EXTERNAL_TIMEOUT_MS,&producer_ok)!=0 ||
       !producer_ok ||
       uxspawn_wait(consumer,EXTERNAL_TIMEOUT_MS,&consumer_ok)!=0 ||
       !consumer_ok) goto cleanup;
    *size=used;
    status=0;
cleanup:
    if(producer>0) {
        if(status) (void)obj_process_terminate(producer);
        (void)obj_close(producer);
    }
    if(consumer>0) {
        if(status) (void)obj_process_terminate(consumer);
        (void)obj_close(consumer);
    }
    for(i=0;i<4;++i) {
        if(maps[i]) (void)obj_region_unmap(maps[i]);
        if(h[i]>0) (void)obj_close(h[i]);
    }
    return status;
}
