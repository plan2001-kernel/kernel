/* Production UXPIPE/UxFdTable/UxService RPC on separate host threads.
 * Only REGIONWAIT/WAKE and the GE/O provider are mocked here. Actual
 * independent kernel processes and real GE/O reboot are tested in QEMU.
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "uxrpc.h"

static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
long sleep(long ms)
{
    struct timespec ts={ms/1000,(ms%1000)*1000000L};
    while(nanosleep(&ts,&ts)<0 && errno==EINTR) {}
    return 0;
}
long region_wait(unsigned long long base,unsigned long long off,
                 unsigned long long expected,long long timeout)
{
    unsigned long long *p=(unsigned long long *)(base+off);
    int rc=1;
    pthread_mutex_lock(&mu);
    while(__atomic_load_n(p,__ATOMIC_ACQUIRE)==expected) {
        if(timeout==0) {rc=2;break;}
        if(timeout<0) assert(pthread_cond_wait(&changed,&mu)==0);
        else {
            struct timespec t;
            assert(clock_gettime(CLOCK_REALTIME,&t)==0);
            t.tv_sec+=timeout/1000;
            t.tv_nsec+=(timeout%1000)*1000000;
            if(t.tv_nsec>=1000000000L) {++t.tv_sec;t.tv_nsec-=1000000000L;}
            if(pthread_cond_timedwait(&changed,&mu,&t)==ETIMEDOUT) {
                rc=2;break;
            }
        }
    }
    pthread_mutex_unlock(&mu);
    return rc;
}
long region_wake(unsigned long long base,unsigned long long off,
                 unsigned long long count)
{
    (void)base;(void)off;(void)count;
    pthread_mutex_lock(&mu);
    assert(pthread_cond_broadcast(&changed)==0);
    pthread_mutex_unlock(&mu);
    return 1;
}
#define DISK_ITEMS 24u
#define DISK_BYTES 20000u
typedef struct {
    UxOid oid;
    unsigned int length;
    unsigned char bytes[DISK_BYTES];
} Entry;
typedef struct {
    Entry committed[DISK_ITEMS],pending[DISK_ITEMS];
    UxOid last_oid;
    unsigned commits;
} MockDisk;
static MockDisk disk;
static Pvd view,restored_view;
static UxService service,restored;
static UxRpcServer server;
static UxRpcClient client;
static UxPipe req_reader,req_writer,res_reader,res_writer;
static UxPipeData req_data,res_data;
static UxPipeAck req_ack,res_ack;
static int make(void *ctx,UxOid *oid)
{
    MockDisk *d=(MockDisk *)ctx;
    *oid=++d->last_oid;
    return *oid ? 0 : -1;
}
static int put(void *ctx,UxOid oid,const void *src,unsigned n)
{
    MockDisk *d=(MockDisk *)ctx;
    if(!oid || n>DISK_BYTES) return -1;
    for(unsigned i=0;i<DISK_ITEMS;++i)
        if(!d->pending[i].oid || d->pending[i].oid==oid) {
            d->pending[i].oid=oid;
            d->pending[i].length=n;
            memcpy(d->pending[i].bytes,src,n);
            return 0;
        }
    return -1;
}
static int get(void *ctx,UxOid oid,void *dst,unsigned cap,unsigned *length)
{
    MockDisk *d=(MockDisk *)ctx;
    for(unsigned i=0;i<DISK_ITEMS;++i)
        if(d->committed[i].oid==oid && oid) {
            if(d->committed[i].length>cap) return -1;
            *length=d->committed[i].length;
            memcpy(dst,d->committed[i].bytes,*length);
            return 0;
        }
    return -1;
}
static int commit(void *ctx)
{
    MockDisk *d=(MockDisk *)ctx;
    for(unsigned i=0;i<DISK_ITEMS;++i) {
        Entry *p=&d->pending[i];
        if(!p->oid) continue;
        unsigned j;
        for(j=0;j<DISK_ITEMS;++j)
            if(d->committed[j].oid==p->oid || !d->committed[j].oid) break;
        if(j==DISK_ITEMS) return -1;
        d->committed[j]=*p;
    }
    ++d->commits;
    return 0;
}
static void *serve(void *unused)
{
    int rc;
    (void)unused;
    do {rc=uxrpc_server_once(&server);} while(rc==0);
    assert(rc==1);
    return 0;
}
static unsigned char output[128];
static unsigned used;
static int emit(void *ctx,const void *src,unsigned n)
{
    (void)ctx;
    assert(n<=sizeof output-used);
    memcpy(output+used,src,n);used+=n;
    return 0;
}
int main(void)
{
    UxStore store={&disk,make,put,get,commit};
    UxShellIO io={0};
    pthread_t t;
    unsigned char tmp[32]={0};
    unsigned kind=0,length=0;
    int fd;
    disk.last_oid=500;
    assert(pvd_bootstrap(&view)==PVD_OK);
    uxservice_init(&service,&view);
    assert(uxservice_bind(&service,&store,100,0)==PVD_OK);
    io.emit=emit;
    assert(uxservice_command(&service,"echo shell > /tmp/shared",&io)==PVD_OK);
    assert(disk.commits==1);
    assert(uxpipe_format(&req_data,&req_ack)==0);
    assert(uxpipe_format(&res_data,&res_ack)==0);
    assert(uxpipe_bind(&req_reader,&req_data,&req_ack,
                       (unsigned long long)&req_data,
                       (unsigned long long)&req_ack,UXPIPE_READER)==0);
    assert(uxpipe_bind(&res_writer,&res_data,&res_ack,
                       (unsigned long long)&res_data,
                       (unsigned long long)&res_ack,UXPIPE_WRITER)==0);
    assert(uxrpc_server_init(&server,&service,"/tmp",
                             &req_reader,&res_writer)==PVD_OK);
    assert(uxpipe_bind(&req_writer,&req_data,&req_ack,
                       (unsigned long long)&req_data,
                       (unsigned long long)&req_ack,UXPIPE_WRITER)==0);
    assert(uxpipe_bind(&res_reader,&res_data,&res_ack,
                       (unsigned long long)&res_data,
                       (unsigned long long)&res_ack,UXPIPE_READER)==0);
    assert(uxrpc_client_init(&client,&req_writer,&res_reader)==PVD_OK);
    assert(pthread_create(&t,0,serve,0)==0);
    fd=uxrpc_open(&client,"/tmp/shared",UXFD_READ);
    assert(fd>=3);
    assert(uxrpc_read(&client,fd,tmp,sizeof tmp)==6);
    assert(!memcmp(tmp,"shell\n",6));
    assert(uxrpc_close(&client,fd)==PVD_OK);
    assert(uxrpc_open(&client,"/tmp/../etc/denied",
                      UXFD_CREATE|UXFD_WRITE)==PVD_EACCES);
    assert(uxrpc_open(&client,"/usr/missing",UXFD_READ)==PVD_EACCES);
    fd=uxrpc_open(&client,"/tmp/client",UXFD_WRITE|UXFD_CREATE|UXFD_TRUNC);
    assert(fd>=3);
    assert(uxrpc_write(&client,fd,"child\n",6)==6);
    assert(uxrpc_close(&client,fd)==PVD_OK);
    assert(uxrpc_stat(&client,"/tmp/client",&kind,&length)==PVD_OK);
    assert(kind==UXFS_FILE && length==6);
    assert(uxrpc_emit(&client,"CLIENT-PASS\n",12)==PVD_OK);
    assert(uxrpc_quit(&client)==PVD_OK);
    assert(pthread_join(t,0)==0);
    assert(server.output_size==12 && !memcmp(server.output,"CLIENT-PASS\n",12));
    assert(disk.commits>=2);
    used=0;
    assert(uxservice_command(&service,"cat /tmp/client",&io)==PVD_OK);
    assert(used==6 && !memcmp(output,"child\n",6));
    assert(pvd_bootstrap(&restored_view)==PVD_OK);
    uxservice_init(&restored,&restored_view);
    assert(uxservice_bind(&restored,&store,100,1)==PVD_OK);
    used=0;
    assert(uxservice_command(&restored,"cat /tmp/client",&io)==PVD_OK);
    assert(used==6 && !memcmp(output,"child\n",6));
    assert(uxfs_info(&restored.fs,"/etc/denied",&kind,&length)==PVD_ENOENT);
    puts("PASS: cross-process UXPIPE file RPC, restricted paths, shared shell view and durable manifest restore");
    return 0;
}
