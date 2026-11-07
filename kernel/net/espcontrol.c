/* Pure, bounded ESP control operations. Only a private Pipe holder can reach
 * this dispatcher. Its caller owns framing, session admission and locking. */
#include "espcontrol.h"
#include "../../runtime/ikecrypto/ikecrypto.h"
#include "../../runtime/ikecrypto/ikemem.h"
static int zero(const uint8_t *p,size_t n){unsigned v=0;while(n--)v|=*p++;return v==0;}
static int unicast(const uint8_t *p){return !zero(p,16)&&p[0]!=255&&!(zero(p,10)&&p[10]==255&&p[11]==255);}

void
espcontrol_disconnect(EspControl *c)
{
    for(unsigned i=0;i<ESPCTL_PEERS;i++){
        esp6_clear(&c->table.sa[i]);c->slotgen[i]=0;
    }
}

void
espcontrol_expire(EspControl *c,uint64_t now)
{
    for(unsigned i=0;i<ESPCTL_PEERS;i++){
        Esp6SA *a=&c->table.sa[i];
        if(a->active&&now>=a->expires){esp6_clear(a);c->slotgen[i]=0;}
    }
}

int
espcontrol_exec(EspControl *c,unsigned op,const uint8_t *in,size_t n,
    uint8_t *out,size_t *outn,uint64_t now,EspEntropy entropy,void *arg)
{
    unsigned slot,mode,life,i;uint32_t rxspi,txspi,active=0;Esp6SA next,*a;
    *outn=0;
    espcontrol_expire(c,now);
    switch(op){
    case ESPCTL_HELLO:
        if(n)break;
        if(!unicast(c->local)||!c->identityn)return ESPCTL_UNAVAILABLE;
        memset(out,0,84);memcpy(out,c->local,16);
        espctl_put32(out+16,c->identityn);memcpy(out+20,c->identity,c->identityn);*outn=84;
        return ESPCTL_OK;
    case ESPCTL_RANDOM:
        if(n!=4)break;
        i=espctl_get32(in);if(!i||i>256)break;
        if(!entropy||!entropy(arg,out,i)){ike_wipe(out,i);return ESPCTL_UNAVAILABLE;}
        *outn=i;return ESPCTL_OK;
    case ESPCTL_INSTALL:
        if(n!=96)break;
        slot=espctl_get32(in);mode=espctl_get32(in+4);life=espctl_get32(in+8);
        rxspi=espctl_get32(in+48);txspi=espctl_get32(in+52);
        if(slot>=ESPCTL_PEERS||mode>1||!life||life>ESPCTL_MAX_LIFE||espctl_get32(in+12)||
           !unicast(c->local)||memcmp(in+16,c->local,16)||!unicast(in+32)||
           !memcmp(in+16,in+32,16)||rxspi<=255||txspi<=255||zero(in+56,16)||zero(in+76,16))break;
        a=&c->table.sa[slot];
        if(a->policy&&(memcmp(a->local,in+16,16)||memcmp(a->remote,in+32,16)))break;
        for(i=0;i<ESPCTL_PEERS;i++){
            Esp6SA *other=&c->table.sa[i];
            if(other->active&&other->rxspi==rxspi)return ESPCTL_INVALID;
            if(i!=slot&&other->policy&&!memcmp(other->local,in+16,16)&&!memcmp(other->remote,in+32,16))return ESPCTL_INVALID;
        }
        if(c->generation==UINT64_MAX)return ESPCTL_LIMIT;
        memset(&next,0,sizeof next);next.policy=next.active=1;next.transport=mode;
        memcpy(next.local,in+16,16);memcpy(next.remote,in+32,16);
        next.rxspi=rxspi;next.txspi=txspi;memcpy(next.rxkey,in+56,20);memcpy(next.txkey,in+76,20);
        next.expires=now+life;next.last=now;
        esp6_clear(a);*a=next;c->slotgen[slot]=++c->generation;
        ike_wipe(&next,sizeof next);c->installed++;
        espctl_put64(out,c->slotgen[slot]);*outn=8;return ESPCTL_OK;
    case ESPCTL_DELETE:
        if(n!=16)break;
        slot=espctl_get32(in);
        if(slot>=ESPCTL_PEERS||espctl_get32(in+4)||!espctl_get64(in+8))break;
        if(c->slotgen[slot]!=espctl_get64(in+8)){c->stale++;return ESPCTL_STALE;}
        esp6_clear(&c->table.sa[slot]);c->slotgen[slot]=0;c->removed++;return ESPCTL_OK;
    case ESPCTL_STATUS:
        if(n)break;
        memset(out,0,272);espctl_put64(out,now);
        for(i=0;i<ESPCTL_PEERS;i++){
            if(c->table.sa[i].active)active|=1U<<i;
            espctl_put64(out+16+16*i,c->slotgen[i]);
            espctl_put64(out+24+16*i,c->table.sa[i].last);
        }
        espctl_put32(out+8,active);*outn=272;return ESPCTL_OK;
    default:break;
    }
    c->invalid++;return ESPCTL_INVALID;
}
