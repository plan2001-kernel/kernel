/* Conservative native AMD64 ELF admission, independent of names/mode bits.
 * The kernel performs its own authoritative validation again at SPAWN. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "program.h"
static uint64_t le(const unsigned char *p,unsigned n)
{uint64_t v=0;while(n--)v=(v<<8)|p[n];return v;}
int p9_program_validate(const void *bytes,size_t n)
{
    const unsigned char *p=bytes;
    uint64_t textend=0,data=0,entry,phoff,mem=0;
    unsigned text=0,writable=0,loads=0;
    if(!p || n<64 || n>64u*1024u*1024u || memcmp(p,"\177ELF\2\1\1",7) ||
       le(p+16,2)!=2 || le(p+18,2)!=62 || le(p+20,4)!=1 ||
       le(p+52,2)!=64 || le(p+54,2)!=56)goto bad;
    unsigned count=(unsigned)le(p+56,2);
    phoff=le(p+32,8);entry=le(p+24,8);
    if(!count || count>64 || phoff<64 || phoff>n || count>(n-phoff)/56)goto bad;
    for(unsigned i=0;i<count;i++){
        const unsigned char *h=p+phoff+56*i;
        unsigned type=(unsigned)le(h,4),flags=(unsigned)le(h+4,4);
        if(type==2 || type==3 || type==7)goto bad; /* no loader, dynamic or TLS */
        if(type!=1)continue;
        uint64_t off=le(h+8,8),va=le(h+16,8),size=le(h+32,8),span=le(h+40,8);
        if(++loads>2 || off>n || size>n-off || size>span ||
           va<0x200000 || va>=UINT64_C(0x7fff00000000) ||
           span>UINT64_C(0x7fff00000000)-va ||
           le(h+48,8)!=4096 || va%4096!=off%4096 || span>256u*1024u*1024u)goto bad;
        mem+=span;if(mem>256u*1024u*1024u)goto bad;
        if(flags==5){
            if(text++ || va!=0x200000 || !size || size!=span || entry<va || entry>=va+size)goto bad;
            textend=(va+size+4095)&~UINT64_C(4095);
        }else if(flags==6){
            if(writable++ || va%4096)goto bad;
            data=va;
        }else goto bad;
    }
    if(loads!=2 || text!=1 || writable!=1 || data<textend)goto bad;
    return 0;
bad:errno=ENOEXEC;return -1;
}
