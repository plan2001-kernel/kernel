/* Pure decoder regression: same source as the kernel keyboard worker. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char uchar;
#define nil NULL
#include "../../../kernel/core/consolekbd.h"
static void reset(int us) {
    kbd_us=us;
    kbd_shiftl=kbd_shiftr=kbd_ctll=kbd_ctlr=kbd_caps=kbd_num=kbd_altgr=0;
    kbd_e0=kbd_skip=kbd_alt=kbd_capsdown=kbd_numdown=npend=ipend=0;
}
static void text(int sc, const char *want) {
    char got[9]; int n=0, c=keydecode(sc);
    assert(c >= 0);
    got[n++]=c;
    while(ipend<npend) got[n++]=pend[ipend++];
    got[n]=0;
    assert(strcmp(got,want)==0);
}
int main(void) {
    reset(0);
    text(0x28,"ä"); text(0x27,"ö"); text(0x1a,"å");
    keydecode(0x3a); keydecode(0x3a); text(0x28,"Ä"); keydecode(0xba);
    keydecode(0x2a); text(0x28,"ä"); keydecode(0xaa);
    keydecode(0x3a); keydecode(0xba);
    keydecode(0xe0); keydecode(0x38);
    text(0x03,"@"); text(0x08,"{"); text(0x0c,"\\");
    text(0x12,"€"); text(0x56,"|");
    keydecode(0xe0); keydecode(0xb8);
    text(0x1b,"¨"); text(0x0d,"´");
    keydecode(0x2a); keydecode(0x36); keydecode(0xaa);
    text(0x1e,"A"); keydecode(0xb6); text(0x1e,"a");
    keydecode(0xe0); text(0x4b,"\033[D");
    keydecode(0xe0); text(0x53,"\033[3~");
    keydecode(0x1d); keydecode(0xe0); keydecode(0x1d);
    keydecode(0x9d); assert(keydecode(0x2e)==3);
    keydecode(0xe0); keydecode(0x9d);
    reset(1);
    text(0x1a,"["); text(0x28,"'"); text(0x35,"/");
    keydecode(0x2a); text(0x03,"@"); text(0x1a,"{");
    keydecode(0xaa);
    keydecode(0x1d); text(0x1a,"\033");
    reset(0);
    keydecode(0xe1);
    for(int i=0;i<5;i++) assert(keydecode(0x1d)==-1);
    text(0x1e,"a");
    puts("PASS: fi/us, UTF-8, AltGr, navigation, independent modifiers");
}
