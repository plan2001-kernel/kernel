/* Kernel ESP datapath and private bootstrap-granted Pipe control service.
 * No IKE packet parser, PSK, P-256 or IKE worker is linked into the kernel. */
#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../core/error.h"
#include "../core/netif.h"
#include "../core/etherif.h"
#include "../core/pipe/pipecore.h"
#include "ip.h"
#include "ipv6.h"
#include "nativev6.h"
#include "espctl.h"
#include "espcontrol.h"
#include "../../runtime/ikecrypto/ikecrypto.h"

static QLock esplock, espstartlock, caplock;
static EspControl control;
static Ipv6Native *espnet;
static Proto espproto;
static int espready, issued;
static uint64_t nowsecs(void){return fastticks2us(fastticks(nil))/1000000;}
static int entropy(void *a,uint8_t *p,size_t n){USED(a);memset(p,0,n);return randomread(p,n)==n;}
static void
espinput(Proto *p,Ipifc *ifc,Block *bp)
{
    Block *plain;Proto *upper;int n;
    USED(p);
    if(!espready||ifc!=espnet->ifc||blocklen(bp)>ESP6_PACKET){freeblist(bp);return;}
    bp=concatblock(bp);if(bp==nil)return;
    plain=allocb(ESP6_PACKET);if(plain==nil){freeb(bp);return;}
    qlock(&esplock);
    n=esp6_open(&control.table,bp->rp,BLEN(bp),plain->wp,nowsecs());
    qunlock(&esplock);
    freeb(bp);
    if(n<0){freeb(plain);return;}
    plain->wp+=n;
    /* esp6_open validates the complete IPv6 header, fixed host selectors and
     * upper-layer protocol; direct dispatch avoids plaintext-policy reentry. */
    upper=Fsrcvpcol(espnet->f,plain->rp[6]);
    if(upper!=nil&&upper->rcv!=nil)upper->rcv(upper,ifc,plain);
    else freeb(plain);
}

int
espctl_output(Fs *f,Block **bpp,int ttl,int tos)
{
    Block *bp=*bpp,*nb;int n,len;
    if(!espready||f!=espnet->f||BLEN(bp)<40||esp6_management(bp->rp,BLEN(bp)))return 0;
    len=blocklen(bp);
    bp=concatblock(bp);*bpp=bp;
    if(bp==nil)return -1;
    /* ipoput6 normally fills these fields later, after selecting a route. */
    bp->rp[0]=IP_VER6|(tos>>4);bp->rp[1]=tos<<4;
    hnputs(bp->rp+4,len-40);bp->rp[7]=ttl;
    nb=allocb(ESP6_PACKET);if(nb==nil)return -1;
    qlock(&esplock);
    n=esp6_seal(&control.table,bp->rp,len,nb->wp,nowsecs());
    qunlock(&esplock);
    if(n<=0){freeb(nb);return n;}
    nb->wp+=n;freeb(bp);*bpp=nb;return 0;
}

int
espctl_plain(Fs *f,Block *bp)
{
    int deny;
    if(!espready||f!=espnet->f)return 0;
    /* ipiput6 guarantees the IPv6 header is already contiguous. */
    if(BLEN(bp)<40)return 1;
    qlock(&esplock);
    deny=esp6_plain_forbidden(&control.table,bp->rp,BLEN(bp));
    qunlock(&esplock);
    return deny;
}

static int
readexact(void *pipe,void *data,int n)
{
    uchar *p=data;long r;
    while(n){r=pipecoreread(pipe,0,p,n);if(r<=0)return -1;p+=r;n-=r;}
    return 0;
}

static void
controlworker(void *pipe)
{
    uchar in[ESPCTL_HEADER+ESPCTL_BODY],out[ESPCTL_HEADER+ESPCTL_BODY];
    uint32_t last=0,id,n,op,status;size_t on;
    if(!waserror()){
        for(;;){
            if(readexact(pipe,in,ESPCTL_HEADER)<0)break;
            id=espctl_get32(in+8);n=espctl_get32(in+12);op=((unsigned)in[6]<<8)|in[7];
            if(espctl_get32(in)!=ESPCTL_MAGIC||in[4]||in[5]!=ESPCTL_VERSION||
               !op||(op&0x8000)||!id||last==0xffffffffU||id!=last+1||
               n>ESPCTL_BODY||espctl_get32(in+16)||espctl_get32(in+20))break;
            if(readexact(pipe,in+ESPCTL_HEADER,n)<0)break;
            last=id;memset(out,0,sizeof out);
            qlock(&esplock);
            if(waserror()){qunlock(&esplock);nexterror();}
            status=espcontrol_exec(&control,op,in+ESPCTL_HEADER,n,out+ESPCTL_HEADER,&on,nowsecs(),entropy,nil);
            poperror();qunlock(&esplock);
            espctl_put32(out,ESPCTL_MAGIC);out[5]=ESPCTL_VERSION;out[6]=(op>>8)|0x80;out[7]=op;
            espctl_put32(out+8,id);espctl_put32(out+12,on);espctl_put32(out+16,status);
            ike_wipe(in,sizeof in);
            if(pipecorewritequiet(pipe,0,out,ESPCTL_HEADER+on)!=ESPCTL_HEADER+on)break;
            ike_wipe(out,sizeof out);
        }
        poperror();
    }else poperror();
    ike_wipe(in,sizeof in);ike_wipe(out,sizeof out);
    qlock(&esplock);espcontrol_disconnect(&control);qunlock(&esplock);
    pipecoreclose(pipe,0);
    print("espctl: controller disconnected; ESP keys erased, policies retained\n");
    /* No automatic controller restart in v1. This parentless kernel worker
     * must not pexit (the canonical bootstrap treats that as a boot failure). */
    for(;;)tsleep(&up->sleep,return0,nil,60000);
}

void
espctl_bootstrap(Proc *p)
{
    void *pipe;int installed=0,h;char value[24],*enabled;
    enabled=getconf("espctl");
    if(!obootstrap(p)||enabled==nil||strcmp(enabled,"1"))return;
    qlock(&caplock);
    if(issued){qunlock(&caplock);return;}
    pipe=pipecorepair();
    if(waserror()){
        if(installed)pipeprocexec(p);else pipecoreclose(pipe,1);
        pipecoreclose(pipe,0);qunlock(&caplock);
        print("espctl: bootstrap capability unavailable\n");poperror();return;
    }
    h=pipekernelinstall(p,pipe,1);installed=1;
    snprint(value,sizeof value,"%d",h);envsetval(p->egrp,"espctl",value);
    kproc("*espctl*",controlworker,pipe);issued=1;
    poperror();qunlock(&caplock);
    print("espctl: private control capability granted to bootstrap\n");
}

static void
expireworker(void *arg)
{
    USED(arg);
    for(;;){
        tsleep(&up->sleep,return0,nil,1000);
        qlock(&esplock);espcontrol_expire(&control,nowsecs());qunlock(&esplock);
    }
}

int
espctl_init(void)
{
    int rc=-1,n;char *addr,*identity;uchar local[16];
    Ipv6Native *net=ipv6native_current();
    if(net==nil)return -1;
    qlock(&espstartlock);
    if(espready){qunlock(&espstartlock);return 0;}
    if(net->f->t2p[50]!=nil)goto done;
    ipmove(local,net->ll);addr=getconf("ikeaddr");
    if(addr!=nil&&(parseip(local,addr)<0||isv4(local)||ipismulticast(local)||!ipforme(net->f,local)))goto done;
    identity=getconf("ikeid");if(identity==nil)identity="plan2001";
    n=strlen(identity);if(n<1||n>64)goto done;
    memset(&espproto,0,sizeof espproto);espproto.name="esp6";
    espproto.ipproto=50;espproto.rcv=espinput;espproto.nc=1;
    if(Fsproto(net->f,&espproto)<0)goto done;
    qlock(&esplock);
    memmove(control.local,local,16);memmove(control.identity,identity,n);control.identityn=n;
    espnet=net;coherence();espready=1;
    qunlock(&esplock);
    kproc("*espexpire*",expireworker,nil);
    print("esp: IPv6 %I ESP 50 ready; IKE runs in userspace\n",local);rc=0;
done:
    qunlock(&espstartlock);return rc;
}
