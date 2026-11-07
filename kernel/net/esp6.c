/* RFC 4106 AES-GCM-16 ESP; RFC 9333 minimal host-to-host IPv6 profile.
 * No ESN, TFC, NAT encapsulation, arbitrary tunnels or dynamic allocation.
 * The caller serializes this table with IKE SA installation/removal. */
#include "esp6.h"
#include "../../runtime/ikecrypto/ikecrypto.h"
#include "../../runtime/ikecrypto/ikemem.h"

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p, uint32_t x) { p[0]=x>>24; p[1]=x>>16; p[2]=x>>8; p[3]=x; }
static size_t iplen(const uint8_t *p) { return 40+((size_t)p[4]<<8|p[5]); }
static int valid(const uint8_t *p, size_t n) { return n>=40 && n<=ESP6_PACKET && p[0]>>4==6 && iplen(p)==n; }
static int upper(int nh) { return nh==6 || nh==17 || nh==58 || nh==253; }

void
esp6_clear(Esp6SA *sa)
{
    /* Retain the fail-closed selector. */
    ike_wipe(sa->rxkey, sizeof sa->rxkey);
    ike_wipe(sa->txkey, sizeof sa->txkey);
    sa->active=0; sa->rxspi=sa->txspi=0;
    sa->rxwindow=0; sa->rxhigh=sa->txseq=0;
}

int
esp6_management(const uint8_t *p, size_t n)
{
    if(n<40) return 0;
    if(p[6]==50) return 1;
    /* IKE is never sent through the child it is managing. */
    if(p[6]==17 && n>=48 && p[40]==1 && p[41]==244 && p[42]==1 && p[43]==244) return 1;
    /* IPv6 neighbor/router discovery cannot depend on a resolved ESP path. */
    return p[6]==58 && p[7]==255 && n>=48 && p[40]>=133 && p[40]<=137;
}

int
esp6_plain_forbidden(Esp6Table *t, const uint8_t *p, size_t n)
{
    int i;
    if(n<40 || esp6_management(p,n)) return 0;
    for(i=0;i<ESP6_PEERS;i++)
        if(t->sa[i].policy && !memcmp(p+8,t->sa[i].remote,16) && !memcmp(p+24,t->sa[i].local,16)) return 1;
    return 0;
}

int
esp6_seal(Esp6Table *t, const uint8_t *p, size_t n, uint8_t out[ESP6_PACKET], uint64_t now)
{
    Esp6SA *sa=0;
    uint8_t *esp, *data;
    size_t plen, pad, total, j;
    uint32_t seq;
    int i;
    if(n<40 || esp6_management(p,n)) return 0;
    for(i=0;i<ESP6_PEERS;i++)
        if(t->sa[i].policy && !memcmp(p+8,t->sa[i].local,16) && !memcmp(p+24,t->sa[i].remote,16)){ sa=&t->sa[i]; break; }
    if(!sa) return 0;
    if(!sa->active || now>=sa->expires || sa->txseq>=0xfffffff0U || !valid(p,n) || !upper(p[6])) goto reject;
    plen=sa->transport ? n-40 : n;
    pad=(4-((plen+2)&3))&3;
    total=40+8+8+plen+pad+2+16;
    if(total>ESP6_PACKET) goto reject;
    memcpy(out,p,40); out[6]=50;
    out[4]=(total-40)>>8; out[5]=total-40;
    esp=out+40; data=esp+16;
    seq=++sa->txseq;
    put32(esp,sa->txspi); put32(esp+4,seq);
    /* Per-key explicit IVs are monotonic 64-bit encodings of the sequence. */
    memset(esp+8,0,4); put32(esp+12,seq);
    memcpy(data,p+(sa->transport?40:0),plen);
    for(j=0;j<pad;j++) data[plen+j]=j+1;
    data[plen+pad]=pad; data[plen+pad+1]=sa->transport?p[6]:41;
    ike_gcm(1,sa->txkey,esp+8,esp,8,data,plen+pad+2,data+plen+pad+2);
    t->sent++;
    return (int)total;
reject:
    t->rejected++;
    return -1;
}

int
esp6_open(Esp6Table *t, const uint8_t *p, size_t n, uint8_t out[ESP6_PACKET], uint64_t now)
{
    Esp6SA *sa=0;
    const uint8_t *esp;
    uint8_t *data;
    uint32_t spi, seq, delta;
    size_t cn, plen, pad, j, total;
    int i, nh;
    if(!valid(p,n) || p[6]!=50 || n<40+8+8+4+16) goto reject;
    esp=p+40; spi=get32(esp); seq=get32(esp+4);
    for(i=0;i<ESP6_PEERS;i++)
        if(t->sa[i].active && t->sa[i].rxspi==spi && !memcmp(p+8,t->sa[i].remote,16) && !memcmp(p+24,t->sa[i].local,16)){sa=&t->sa[i];break;}
    if(!sa || now>=sa->expires || !seq) goto reject;
    if(seq<=sa->rxhigh){
        delta=sa->rxhigh-seq;
        if(delta>=64 || (sa->rxwindow & (UINT64_C(1)<<delta))){t->replay++;return -1;}
    }
    cn=n-40-16-16;
    data=out+40;
    memcpy(data,esp+16,cn);
    if(!ike_gcm(0,sa->rxkey,esp+8,esp,8,data,cn,(uint8_t*)p+n-16)){ t->badtag++; return -1; }
    pad=data[cn-2]; nh=data[cn-1];
    if(pad>cn-2) goto scrub;
    plen=cn-pad-2;
    for(j=0;j<pad;j++) if(data[plen+j]!=j+1) goto scrub;
    if(sa->transport){
        if(!upper(nh)) goto scrub;
        memcpy(out,p,40); out[6]=nh; out[4]=plen>>8; out[5]=plen;
        total=40+plen;
    }else{
        if(nh!=41 || !valid(data,plen) || !upper(data[6])) goto scrub;
        memmove(out,data,plen); total=plen;
    }
    /* Authenticated traffic must still match the negotiated IPv6 selectors. */
    if(memcmp(out+8,sa->remote,16) || memcmp(out+24,sa->local,16) || esp6_management(out,total)) goto scrub;
    if(seq>sa->rxhigh){
        delta=seq-sa->rxhigh;
        sa->rxwindow=delta>=64?1:(sa->rxwindow<<delta)|1;
        sa->rxhigh=seq;
    }else sa->rxwindow |= UINT64_C(1)<<(sa->rxhigh-seq);
    sa->last=now; t->accepted++;
    return (int)total;
scrub:
    ike_wipe(out,ESP6_PACKET);
reject:
    t->rejected++;
    return -1;
}
