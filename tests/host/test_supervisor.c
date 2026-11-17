static unsigned long long expected_project=0x4f45584543000007ULL;
/* Supervisor authority contract test: only actual kernel Handle grants,
 * six freshly created region streams and the exact attenuated GE/O rights
 * may reach the F5 Unix child. All kernel interfaces are mocked; real
 * process execution is validated separately by QEMU.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "supervisor.h"
#include "uxspawn.h"
#include "obj.h"
#include "vobj.h"

static unsigned char regions[6][4096] __attribute__((aligned(4096)));
static int created, opened_exec, opened_store, opened_project, launches, closed, unmapped;
static int allow_console=1, expect_storage=0, failed_child=0;
static int received_grants, terminate_count;
static const unsigned char *keyboard;
static unsigned keyboard_pos;
static char echoed[256];
static unsigned echoed_len;

int p2001_session_probe(void) {return allow_console ? 1 : -1;}
long p2001_session_try_read(int fd, void *buf, unsigned long n)
{
    assert(fd==0 && n==1);
    if(!keyboard || !keyboard[keyboard_pos]) return 0;
    *(unsigned char *)buf=keyboard[keyboard_pos++]; return 1;
}
long p2001_session_write(int fd,const void *bytes,unsigned long n)
{
    assert(fd==1 || fd==2);
    if(keyboard && echoed_len+n<=sizeof echoed) {
        memcpy(echoed+echoed_len,bytes,n); echoed_len+=(unsigned)n;
    }
    return (long)n;
}
long obj_list(void *buf,unsigned long long capacity)
{
    VobjListRec *records=(VobjListRec *)buf;
    assert(capacity>=3);
    memset(records,0,3*sizeof(*records));
    for(int i=0;i<3;++i) {
        records[i].handle=(unsigned int)(10+i);
        records[i].kind=VkindRegion;
        records[i].rights=i==0 ? Vread|Vmeta : Vwrite|Vmeta;
    }
    return 3;
}
long obj_open(unsigned long long provider, void *req,long len,
              unsigned long long rights, ObjId *out)
{
    (void)out;
    if(provider==ObjOpenProject){ObjProjectOpen *p=req;assert(len==sizeof *p && p->volume==24 && p->oid==expected_project);++opened_project;return 23;}
    assert(provider==ObjOpenVolume && req==0 && len==0);
    if(rights==0)return 24;
    if (rights==(Vexecute|Vdelegate)) {++opened_exec;return 21;}
    assert(rights==(Vread|Vwrite|Vcreate|Vcommit|Vmeta|Vdelegate));
    ++opened_store;return 22;
}
long obj_region_create(unsigned long long size, ObjId *out)
{
    (void)out;assert(size==4096 && created<6);
    memset(regions[created],0,sizeof regions[created]);
    return 101+created++;
}
long obj_region_map(int handle,unsigned long long addr)
{
    assert(handle>=101 && handle<=106 && addr==0);
    return (long)regions[handle-101];
}
long obj_region_map_flags(int handle,unsigned long long addr,unsigned int flags)
{
    assert(flags==ObjRegionMapReadonly);
    return obj_region_map(handle,addr);
}
long obj_region_unmap(unsigned long long base)
{assert(base!=0);++unmapped;return 0;}
long obj_close(int handle)
{assert((handle>=101 && handle<=106) || handle==21 || handle==22 || handle==55 || handle==23 || handle==24);
 ++closed;return 0;}
long region_wait(unsigned long long base,unsigned long long offset,
                 unsigned long long expect,long long timeout)
{(void)base;(void)offset;(void)expect;(void)timeout;return 2;}
long region_wake(unsigned long long base,unsigned long long offset,
                 unsigned long long count)
{(void)base;(void)offset;(void)count;return 0;}
long obj_ospawn(int exec,unsigned long long oid,char **argv,int argc,
                struct Ospawn *options)
{
    const unsigned long long *raw=(const unsigned long long *)options;
    assert(exec==21 && oid==0x1234 && argc==(expect_storage ? 3 : 2));
    assert(!strcmp(argv[0],"unix-child") && !strcmp(argv[1],"0x0000000000001234"));
    if(expect_storage)assert(!strcmp(argv[2],"0x0000000000003456"));
    assert(raw[0]==~0ULL && raw[1]==0 && raw[2]==2);
    assert(raw[3]==8ULL);
    for(int i=0;i<6;++i) {
        assert(raw[4+i*2]==(unsigned long long)(101+i));
        assert(raw[5+i*2]==(unsigned long long)(
                    (i==0||i==3||i==5 ? Vread : Vwrite)|Vdelegate));
    }
    assert(raw[16]==21 && raw[17]==(Vexecute|Vdelegate));
    if(expect_storage) {
        assert(raw[18]==22);
        assert(raw[19]==(Vread|Vwrite|Vcreate|Vcommit|Vmeta));
    }
    if(!expect_storage){assert(raw[18]==23);assert(raw[19]==(Vread|Vmeta|Vcontrol|Vdelegate|Vwrite|Vcreate|Vcommit|Vexecute));}
    received_grants=(int)raw[3];
    ++launches;
    return 55;
}
long obj_process_wait(int handle,ObjProcessInfo *info,long long timeout)
{
    assert(handle==55 && timeout==0);
    info->state=ObjProcCompleted;
    info->cause=failed_child?ObjProcCauseFault:ObjProcCauseExit;
    info->msglen=0;
    return 0;
}
long obj_process_terminate(int handle)
{assert(handle==55);++terminate_count;return 0;}
long sleep(long timeout) {(void)timeout;return 0;}

void rerrstr(char *buf,unsigned n){if(n)buf[0]=0;}

static void reset(void)
{
    created=opened_exec=opened_store=opened_project=launches=closed=unmapped=0;
    terminate_count=received_grants=0;
}
static void type(UxSupervisor *s,const char *keys)
{
    keyboard=(const unsigned char *)keys; keyboard_pos=0;
    for(unsigned i=0;i<strlen(keys)+3;++i) assert(uxsupervisor_pump(s)==0);
    assert(keyboard_pos==strlen(keys));
}
static void canonical_input(void)
{
    UxSupervisor s;
    UxPipe input;
    char buf[UXPIPE_CAPACITY];
    reset();
    assert(uxsupervisor_prepare(&s)==0);
    assert(uxpipe_bind(&input,s.streams[0].data,s.streams[0].ack,
        s.maps[0],s.maps[1],UXPIPE_READER)==0);
    echoed_len=0;
    type(&s,"\004");
    assert(uxpipe_read(&input,buf,sizeof buf,0)==0);
    assert(echoed_len==0 && !s.streams[0].data->closed);
    type(&s,"next\n");
    assert(uxpipe_read(&input,buf,sizeof buf,0)==5 && !memcmp(buf,"next\n",5));
    type(&s,"abc\004");
    assert(uxpipe_read(&input,buf,sizeof buf,0)==3 && !memcmp(buf,"abc",3));
    assert(uxpipe_read(&input,buf,sizeof buf,0)==-1); /* flush, not EOF */
    type(&s,"\004");
    assert(uxpipe_read(&input,buf,sizeof buf,0)==0);
    type(&s,"ok\n");
    assert(uxpipe_read(&input,buf,sizeof buf,0)==3 && !memcmp(buf,"ok\n",3));
    memset(buf,'x',sizeof buf);
    assert(uxpipe_write(&s.streams[0],buf,sizeof buf,0)==sizeof buf);
    type(&s,"\004");
    assert(s.pending_eof==1); /* preserve event while the ring is full */
    unsigned drained=0;
    while(drained<sizeof buf) {
        long n=uxpipe_read(&input,buf+drained,sizeof buf-drained,0);
        assert(n>0); drained+=(unsigned)n;
    }
    assert(uxsupervisor_pump(&s)==0 && !s.pending_eof);
    assert(uxpipe_read(&input,buf,sizeof buf,0)==0);
    keyboard=0;
    uxsupervisor_release(&s);
    puts("PASS: canonical Ctrl+D flush/one-read EOF, backpressure and subsequent input");
}
int main(void)
{
    unsigned long long oid=0;
    assert(uxsupervisor_parse_oid("0x1234",&oid)==0 && oid==0x1234);
    assert(uxsupervisor_parse_oid("0x0",&oid)<0);
    assert(uxsupervisor_parse_oid("1234",&oid)<0);
    assert(uxsupervisor_parse_oid("0xfffffffffffffffff",&oid)<0);
    assert(uxsupervisor_parse_oid("0x12no",&oid)<0);
    assert(uxsupervisor_run(0x1234,0,0)<0);
    assert(uxsupervisor_run(0x1234,0x3456,expected_project)<0);
    assert(created==0);
    allow_console=0;
    assert(uxsupervisor_run(0x1234,0,expected_project)<0);
    assert(created==0);
    allow_console=1;
    reset();
    expect_storage=0;
    assert(uxsupervisor_run(0x1234,0,expected_project)==0);
    assert(created==6 && opened_exec==1 && opened_store==0);
    assert(launches==1 && received_grants==8 && opened_project==1);
    assert(closed==10 && unmapped==12 && !terminate_count);
    reset(); expected_project=0x4f45584543000008ULL;
    assert(uxsupervisor_run(0x1234,0,expected_project)==0);
    assert(opened_project==1 && launches==1);
    reset();expect_storage=1;
    assert(uxsupervisor_run(0x1234,0x3456,0)==0);
    assert(opened_exec==1 && opened_store==1 && received_grants==8);
    assert(closed==9 && !terminate_count);
    reset();expect_storage=0;failed_child=1;
    assert(uxsupervisor_run(0x1234,0,expected_project)<0);
    assert(terminate_count==1);
    canonical_input();
    puts("PASS: supervisor never delegates kernel consoles, precisely attenuates six streams and executable/storage grants");
    return 0;
}
