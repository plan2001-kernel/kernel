#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "uxobject.h"
int main(void)
{
    UxMeta m,restored;unsigned n;unsigned char out[4096],copy[4096];
    uxmeta_init(&m,"/tmp/kernel_trap.c",2,1234567890123ULL);
    m.uid=501;m.gid=20;m.mode=0100640;m.atime_ns=-100;
    assert(uxobject_metadata(&m,0,0,out,sizeof out,&n)==0);
    uxmeta_init(&restored,"/else/renamed.c",2,0);
    assert(uxobject_decode(out,n,&restored)==0);
    assert(!strcmp(restored.name,"kernel_trap.c") && restored.birth_ns==m.birth_ns);
    assert(restored.uid==501 && restored.gid==20 && restored.mode==0100640 && restored.atime_ns==-100);
    /* Preserve opaque top-level and nested Unix fields, including arbitrary
     * bytes used for xattrs/ACLs by a producer with a different schema. */
    const unsigned char old[]={0xa2,0x61,'x',0x43,0,0xff,1,0x64,'u','n','i','x',0xa1,0x63,'a','c','l',0x42,0xff,0};
    assert(uxobject_metadata(&m,old,sizeof old,out,sizeof out,&n)==0);
    unsigned n2;assert(uxobject_metadata(&m,out,n,copy,sizeof copy,&n2)==0);
    assert(n==n2 && !memcmp(out,copy,n));
    int foundx=0,foundacl=0;
    for(unsigned i=0;i+5<n;i++){
        if(!memcmp(out+i,"\x61x\x43\0\xff\1",6))foundx=1;
        if(i+7<n && !memcmp(out+i,"\x63" "acl\x42\xff\0",7))foundacl=1;
    }
    assert(foundx && foundacl);
    const unsigned char created[]={0xa1,0x67,'c','r','e','a','t','e','d',0x61,'?'};
    m.known &= ~UXMETA_BIRTH;
    assert(uxobject_metadata(&m,created,sizeof created,out,sizeof out,&n)==0);
    int preserved=0;for(unsigned i=0;i+10<=n;i++)if(!memcmp(out+i,created+1,10))preserved=1;
    assert(preserved);
    const unsigned char opaque[]={0xff,0,0xff};
    assert(uxobject_metadata(&m,opaque,sizeof opaque,out,sizeof out,&n)==0 && n==sizeof opaque && !memcmp(out,opaque,n));
    assert(uxobject_metadata(&m,0,0,out,1,&n)<0);
    puts("PASS: CBOR original name, stat timestamps/identity metadata, unknown fields and opaque preservation");
}
