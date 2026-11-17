#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "program.h"
static unsigned char original[8192],elf[8192];
static void put(unsigned char *p,uint64_t v,unsigned n){while(n--){*p++=(unsigned char)v;v>>=8;}}
static void valid(void)
{
    memset(elf,0,sizeof elf);memcpy(elf,"\177ELF\2\1\1",7);
    put(elf+16,2,2);put(elf+18,62,2);put(elf+20,1,4);
    put(elf+24,0x200100,8);put(elf+32,64,8);put(elf+52,64,2);put(elf+54,56,2);put(elf+56,2,2);
    unsigned char *text=elf+64,*data=text+56;
    put(text,1,4);put(text+4,5,4);put(text+16,0x200000,8);put(text+32,512,8);put(text+40,512,8);put(text+48,4096,8);
    put(data,1,4);put(data+4,6,4);put(data+8,4096,8);put(data+16,0x201000,8);put(data+32,16,8);put(data+40,4096,8);put(data+48,4096,8);
}
static void reject(size_t offset,uint64_t value,unsigned n)
{
    memcpy(elf,original,sizeof elf);put(elf+offset,value,n);errno=0;
    assert(p9_program_validate(elf,sizeof elf)<0 && errno==ENOEXEC);
}
int main(void)
{
    valid();assert(p9_program_validate(elf,sizeof elf)==0);memcpy(original,elf,sizeof elf);
    assert(p9_program_validate(NULL,0)<0);
    assert(p9_program_validate(elf,63)<0);
    reject(16,3,2);reject(18,183,2);reject(32,UINT64_MAX,8);reject(56,65,2);
    reject(64+4,7,4);reject(64+16,0x201000,8);reject(64+32,UINT64_MAX,8);
    reject(64+56+16,0x200000,8);reject(64+56+16,0x201001,8);
    reject(64+56+40,UINT64_MAX,8);reject(64+56+40,257u*1024u*1024u,8);
    reject(64+56+48,8192,8);reject(64+56,3,4);reject(64+56,2,4);reject(64+56,7,4);
    puts("PASS: native ELF admission rejects architecture/loader/WX/bounds/alignment violations");
    return 0;
}
