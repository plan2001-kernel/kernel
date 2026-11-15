/* The kernel's console grant triple has no Vdelegate. A userland process
 * cannot manufacture Vdelegate and MUST NOT try to inherit these Handles.
 *
 * This trusted boot-manifest process uses kernel console rings itself and
 * creates SIX fresh capability regions for three UXPIPE channels:
 *
 * stream      supervisor maps           F5 child receives
 * stdin       data RW / ACK RO           data RO / ACK RW
 * stdout      data RO / ACK RW           data RW / ACK RO
 * stderr      data RO / ACK RW           data RW / ACK RO
 *
 * The child also receives attenuated GE/O Vexecute (always) and,
 * only with an explicit provisioned manifest selector, transaction rights.
 * There is no guessed GE/O authority or privileged Unix kernel interface.
 */
#include "supervisor.h"
#include "uxspawn.h"
#include "obj.h"
#include "vobj.h"

extern int p2001_session_probe(void);
extern long p2001_session_try_read(int, void *, unsigned long);
extern long p2001_session_write(int, const void *, unsigned long);
extern long obj_open(unsigned long long, void *, long,
                     unsigned long long, ObjId *);
extern long obj_close(int);
extern long obj_list(void *, unsigned long long);
extern long obj_region_create(unsigned long long, ObjId *);
extern long obj_region_map(int, unsigned long long);
extern long obj_region_map_flags(int, unsigned long long, unsigned int);
extern long obj_region_unmap(unsigned long long);
extern long obj_process_terminate(int);
extern long sleep(long);

#define UX_SUPERVISOR_STORE (Vread|Vwrite|Vcreate|Vcommit|Vmeta)
#define UX_SUPERVISOR_LAUNCH (Vexecute|Vdelegate)

static unsigned int count(const char *s)
{
    unsigned int n=0;
    while (s[n] && n<24) ++n;
    return n;
}
int uxsupervisor_parse_oid(const char *arg, unsigned long long *oid)
{
    unsigned int i, digits, n, d;
    unsigned long long value=0;
    if (!arg || !oid) return -1;
    n=count(arg);
    if (n<3 || n>18 || arg[0]!='0' || arg[1]!='x') return -1;
    digits=n-2;
    if (!digits || digits>16) return -1;
    for (i=2; i<n; ++i) {
        char c=arg[i];
        d=c>='0'&&c<='9'?(unsigned int)(c-'0'):
          c>='a'&&c<='f'?(unsigned int)(c-'a'+10):
          c>='A'&&c<='F'?(unsigned int)(c-'A'+10):16u;
        if (d>=16) return -1;
        value=value*16+d;
    }
    if (!value) return -1;
    *oid=value;
    return 0;
}
static int prepare_one(UxSupervisor *s, unsigned int channel)
{
    unsigned int i;
    int role= channel==0 ? UXPIPE_WRITER : UXPIPE_READER;
    long base;
    UxPipeData *data;
    UxPipeAck *ack;
    for (i=0;i<2;++i) {
        unsigned int idx=channel*2+i;
        s->handles[idx]=(int)obj_region_create(UXPIPE_PAGE,0);
        if (s->handles[idx]<=0) return -1;
        base=obj_region_map(s->handles[idx],0);
        if (base<0) return -1;
        s->maps[idx]=(unsigned long long)base;
    }
    data=(UxPipeData *)s->maps[channel*2];
    ack=(UxPipeAck *)s->maps[channel*2+1];
    if (uxpipe_format(data,ack)!=0) return -1;
    for (i=0;i<2;++i) {
        unsigned int idx=channel*2+i;
        (void)obj_region_unmap(s->maps[idx]);
        s->maps[idx]=0;
    }
    for (i=0;i<2;++i) {
        unsigned int idx=channel*2+i;
        int writable=(channel==0 ? i==0 : i==1);
        base=writable ?
            obj_region_map(s->handles[idx],0) :
            obj_region_map_flags(s->handles[idx],0,
                                 ObjRegionMapReadonly);
        if (base<0) return -1;
        s->maps[idx]=(unsigned long long)base;
    }
    return uxpipe_bind(&s->streams[channel],
                       (UxPipeData *)s->maps[channel*2],
                       (UxPipeAck *)s->maps[channel*2+1],
                       s->maps[channel*2],s->maps[channel*2+1],role);
}
int uxsupervisor_prepare(UxSupervisor *s)
{
    VobjListRec records[16];
    unsigned int i;
    long n;
    if (!s || p2001_session_probe()!=1) return -1;
    /* Before opening a volume or creating proxy regions, only the kernel's
     * three original nondelegable session grants may be present. This
     * rejects an accidental or malicious extra capability.
     */
    n=obj_list(records,16);
    if (n!=3) return -1;
    for (i=0;i<3;++i) {
        unsigned int expected=i==0 ? Vread|Vmeta : Vwrite|Vmeta;
        if (records[i].kind!=VkindRegion ||
            records[i].rights!=expected)
            return -1;
    }
    for (i=0;i<6;++i) {s->handles[i]=-1;s->maps[i]=0;}
    for (i=0;i<3;++i)
        if (prepare_one(s,i)!=0) {
            uxsupervisor_release(s);
            return -1;
        }
    s->pending_len=s->pending_pos=0;
    s->edit_len=0;
    return 0;
}
int uxsupervisor_pump(UxSupervisor *s)
{
    unsigned char output[256];
    unsigned char ch;
    long n;
    unsigned int channel, repeat;
    if (!s) return -1;
    if (s->pending_pos < s->pending_len) {
        n=uxpipe_write(&s->streams[0],s->pending+s->pending_pos,
                       s->pending_len-s->pending_pos,0);
        if (n>0) {
            s->pending_pos+=(unsigned int)n;
            if (s->pending_pos==s->pending_len)
                s->pending_pos=s->pending_len=0;
        }
    } else {
        /* This is a text console, not a terminal emulator. Provide the
         * visible input echo and a small canonical line editor before
         * forwarding complete lines to ash over its explicit UXPIPE. */
        n=p2001_session_try_read(0,&ch,1);
        if (n<0) return -1;
        if (n==1) {
            static const char erase[]="\b \b";
            static const char newline='\n';
            if (ch=='\r' || ch=='\n') {
                if (p2001_session_write(1,&newline,1)!=1) return -1;
                if (s->edit_len) {
                    for (unsigned int i=0;i<s->edit_len;++i)
                        s->pending[i]=s->edit[i];
                }
                s->pending[s->edit_len]='\n';
                s->pending_len=s->edit_len+1;
                s->pending_pos=0;
                s->edit_len=0;
            } else if (ch==8 || ch==127) {
                if (s->edit_len) {
                    --s->edit_len;
                    if (p2001_session_write(1,erase,sizeof(erase)-1)
                        !=(long)(sizeof(erase)-1)) return -1;
                }
            } else if (ch>=32 && ch<127 &&
                       s->edit_len<sizeof(s->edit)-1) {
                s->edit[s->edit_len++]=ch;
                if (p2001_session_write(1,&ch,1)!=1) return -1;
            }
        }
    }
    for (channel=1;channel<=2;++channel) {
        for (repeat=0;repeat<32;++repeat) {
            n=uxpipe_read(&s->streams[channel],output,
                          sizeof(output),0);
            if (n<=0) break;    /* empty/EOF, no busy-wait */
            if (p2001_session_write((int)channel,output,
                                    (unsigned long)n)!=n)
                return -1;
        }
    }
    return 0;
}
void uxsupervisor_release(UxSupervisor *s)
{
    unsigned int i;
    if (!s) return;
    for (i=0;i<6;++i) {
        if (s->maps[i]) {
            (void)obj_region_unmap(s->maps[i]);
            s->maps[i]=0;
        }
        if (s->handles[i]>0) {
            (void)obj_close(s->handles[i]);
            s->handles[i]=-1;
        }
    }
}
int uxsupervisor_run(unsigned long long child_oid,
                     unsigned long long manifest_oid)
{
    static UxSupervisor proxy;
    UxLaunch launch={0};
    char child_oid_arg[19], manifest_arg[19];
    char *argv[4]={"unix-child",child_oid_arg,0,0};
    int exec=-1, storage=-1, child=-1, exited_ok=0, status=-1;
    unsigned int i, used=0;
    unsigned long long id;
    const char digits[]="0123456789abcdef";
    static const char start[]="PLAN2001-UNIX-SUPERVISOR-READY\n";
    if (!child_oid || child_oid==manifest_oid) return -1;
    if (uxsupervisor_prepare(&proxy)!=0) return -1;
    /* Only an actual boot process can mint these authorized GE/O handles.
     * Exec and storage are separate capability derivation paths. */
    exec=(int)obj_open(ObjOpenVolume,0,0,UX_SUPERVISOR_LAUNCH,0);
    if (exec<=0) goto cleanup;
    if (manifest_oid) {
        storage=(int)obj_open(ObjOpenVolume,0,0,
                             UX_SUPERVISOR_STORE|Vdelegate,0);
        if (storage<=0) goto cleanup;
    }
    child_oid_arg[0]='0';child_oid_arg[1]='x';
    for(i=0;i<16;++i) {
        id=child_oid >> (60-4*i);
        child_oid_arg[2+i]=digits[id & 15];
    }
    child_oid_arg[18]=0;
    if (manifest_oid) {
        manifest_arg[0]='0';manifest_arg[1]='x';
        for(i=0;i<16;++i) {
            id=manifest_oid >> (60-4*i);
            manifest_arg[2+i]=digits[id & 15];
        }
        manifest_arg[18]=0;
        argv[2]=manifest_arg;
    }
    launch.executable_volume=exec;
    launch.oid=child_oid;
    for(i=0;i<6;++i) {
        launch.grant[i].handle=proxy.handles[i];
        launch.grant[i].rights=
            (i==0 || i==3 || i==5 ? Vread : Vwrite)|Vdelegate;
    }
    launch.grant[6].handle=exec;
    launch.grant[6].rights=Vexecute|Vdelegate;
    launch.grant_count=7;
    if (storage>0) {
        launch.grant[7].handle=storage;
        launch.grant[7].rights=UX_SUPERVISOR_STORE;
        launch.grant_count=8;
    }
    child=uxspawn_start(&launch,argv,manifest_oid ? 3 : 2);
    if (child<=0) goto cleanup;
    (void)p2001_session_write(1,start,sizeof(start)-1);
    for (;;) {
        int waitstatus=uxspawn_wait(child,0,&exited_ok);
        if (waitstatus<0 || uxsupervisor_pump(&proxy)!=0) goto cleanup;
        if (waitstatus==0) {
            /* Drain output already published before process exit. */
            if (uxsupervisor_pump(&proxy)!=0) goto cleanup;
            status=exited_ok ? 0 : -1;
            break;
        }
        (void)sleep(2);
        ++used; /* bounded arithmetic not used for authorization */
    }
cleanup:
    if (child>0) {
        if (status!=0) (void)obj_process_terminate(child);
        (void)obj_close(child);
    }
    if (storage>0) (void)obj_close(storage);
    if (exec>0) (void)obj_close(exec);
    uxsupervisor_release(&proxy);
    return status;
}
