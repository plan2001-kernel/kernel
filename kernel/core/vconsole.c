/*
 * Kernel-owned boot console sessions.  The existing capability-region ring
 * is the only transport: kernel publishes input, each process publishes
 * stdout and diagnostics.  No init process owns a console or the framebuffer.
 */
#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../core/vobjint.h"
#include "../../runtime/abi/obj.h"

enum {
    Consoles = 8,
    History = 16384,
    Input = 0,
    Output = 1,
    Diagnostic = 2,
};
typedef struct Vconsole Vconsole;
struct Vconsole {
    char name[32];
    Vobj *region[3];
    uvlong iseq, oseq, dseq;
    char history[History];
    int nhistory;
    int defined, ready, alive;
};
static Vconsole sessions[Consoles+1];
static int nsessions, current, ready;
static int keyboardclaimed;

/* Configured once before the first manifest root is started. */
void
vconsoledefine(int slot, char *name)
{
    Vconsole *c;
    if(slot < 1 || slot > Consoles || name == nil || strlen(name) >= 32)
        panic("console: invalid boot session");
    c = &sessions[slot];
    if(c->defined)
        panic("console: duplicate boot session");
    strcpy(c->name, name);
    c->defined = 1;
    if(slot > nsessions)
        nsessions = slot;
}

static void
historyappend(Vconsole *c, char *buf, ulong n)
{
    if(n >= History){
        buf += n - History;
        n = History;
        c->nhistory = 0;
    }
    if(c->nhistory + n > History){
        int cut = c->nhistory + n - History;
        memmove(c->history, c->history + cut, c->nhistory - cut);
        c->nhistory -= cut;
    }
    memmove(c->history + c->nhistory, buf, n);
    c->nhistory += n;
}

static void
selectview(int slot)
{
    Vconsole *c;
    if(slot < 0 || slot > nsessions)
        return;
    if(slot != 0 && (!sessions[slot].ready || !sessions[slot].alive))
        return;
    current = slot;
    if(slot == 0){
        fbconsoleselect(0, kmesg.buf, kmesg.n);
        print("console: focus SystemConsole\n");
        return;
    }
    c = &sessions[slot];
    fbconsoleselect(slot, c->history, c->nhistory);
    print("console: focus %s slot=%d\n", c->name, slot);
}

/* Scan-code set 1, minimal US layout.  Raw bytes never go to a second
 * session and switching chords are swallowed by the kernel. */
static uchar keys[128] = {
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4', [0x06]='5',
    [0x07]='6', [0x08]='7', [0x09]='8', [0x0a]='9', [0x0b]='0',
    [0x0c]='-', [0x0d]='=', [0x0e]='\b', [0x0f]='\t',
    [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t',
    [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p',
    [0x1a]='[', [0x1b]=']', [0x1c]='\r',
    [0x1e]='a', [0x1f]='s', [0x20]='d', [0x21]='f', [0x22]='g',
    [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l',
    [0x27]=';', [0x28]='\'', [0x29]='`', [0x2b]='\\',
    [0x2c]='z', [0x2d]='x', [0x2e]='c', [0x2f]='v',
    [0x30]='b', [0x31]='n', [0x32]='m', [0x33]=',', [0x34]='.',
    [0x35]='/', [0x39]=' ',
};
static uchar shifted[128] = {
    [0x02]='!', [0x03]='@', [0x04]='#', [0x05]='$', [0x06]='%',
    [0x07]='^', [0x08]='&', [0x09]='*', [0x0a]='(', [0x0b]=')',
    [0x0c]='_', [0x0d]='+', [0x1a]='{', [0x1b]='}',
    [0x27]=':', [0x28]='"', [0x29]='~', [0x2b]='|',
    [0x33]='<', [0x34]='>', [0x35]='?',
};

static void
keyboardproc(void *arg)
{
    uchar sc, b;
    int shift = 0, ctl = 0, alt = 0, target, n;
    Vconsole *c;
    USED(arg);
    for(;;){
        n = kbdscancoderead(&sc, 1);
        if(n <= 0){
            tsleep(&up->sleep, return0, nil, 10);
            continue;
        }
        if((sc & 0x7f) == 0x2a || (sc & 0x7f) == 0x36){
            shift = (sc & 0x80) == 0;
            continue;
        }
        if((sc & 0x7f) == 0x1d){
            ctl = (sc & 0x80) == 0;
            continue;
        }
        if((sc & 0x7f) == 0x38){
            alt = (sc & 0x80) == 0;
            continue;
        }
        if(sc & 0x80)
            continue;
        if(ctl && alt && sc >= 0x3b && sc <= 0x43){
            target = sc - 0x3b;
            if(target <= nsessions)
                selectview(target);
            continue;
        }
        target = current;
        if(target == 0 || !sessions[target].alive)
            continue;
        c = &sessions[target];
        b = keys[sc];
        if(shift){
            if(shifted[sc] != 0)
                b = shifted[sc];
            else if(b >= 'a' && b <= 'z')
                b -= 'a' - 'A';
        }
        if(ctl && sc == 0x2e)
            b = 3;
        else if(ctl && sc == 0x20)
            b = 4;
        if(b != 0 && vobjregionringpublish(c->region[Input], &c->iseq, &b, 1) < 0)
            print("console: input publish failed slot=%d\n", target);
    }
}

static void
drain(Vconsole *c, int which, uvlong *seq)
{
    uvlong tail, n, s;
    char payload[OBJ_RING_DATA];
    Vobj *r = c->region[which];

    tail = vobjregionringload(r, OBJ_RING_PUBOFF);
    /* A bounded ring may overwrite oldest records under sustained output. */
    if(tail > *seq + OBJ_RING_SLOTS)
        *seq = tail - OBJ_RING_SLOTS;
    while(*seq < tail){
        s = ++*seq;
        if(!vobjregionringconsume(r, s, payload, sizeof payload, &n))
            continue;
        historyappend(c, payload, n);
        if(current == (int)(c - sessions))
            fbconsoleappend(current, payload, n);
        vobjregionringack(r, s);
    }
}

/* Permanent kernel worker owns the session objects and the framebuffer
 * routing.  They survive every user process exit. */
void
vconsolemanager(void *arg)
{
    Vconsole *c;
    int i, k;
    USED(arg);
    for(i=1; i<=nsessions; i++){
        c = &sessions[i];
        for(k=0; k<3; k++){
            c->region[k] = vobjregionalloc(OBJ_RING_BYTES);
            if(c->region[k] == nil || vobjregionringinit(c->region[k]) < 0)
                panic("console: cannot allocate session region");
        }
        if(vobjregionwriterhold(c->region[Input]) < 0)
            panic("console: cannot hold input publisher");
        coherence();
        c->ready = 1;
    }
    if(kbdscancodeacquire() == 0){
        keyboardclaimed = 1;
        kproc("*consolekbd*", keyboardproc, nil);
    }else
        print("console: PS/2 unavailable; SystemConsole remains visible\n");
    coherence();
    ready = 1;
    print("console: SystemConsole ready, %d independent sessions\n", nsessions);
    for(;;){
        for(i=1; i<=nsessions; i++){
            c = &sessions[i];
            if(c->ready){
                drain(c, Output, &c->oseq);
                drain(c, Diagnostic, &c->dseq);
            }
        }
        tsleep(&up->sleep, return0, nil, 20);
    }
}

void
vconsolebind(Proc *p, int slot)
{
    if(p == nil || slot < 1 || slot > nsessions || !sessions[slot].defined)
        panic("console: invalid process binding");
    while(!ready)
        tsleep(&p->sleep, return0, nil, 10);
    p->console_slot = slot;
    sessions[slot].alive = 1;
    print("console: session %s pid=%lud slot=%d\n",
          sessions[slot].name, p->pid, slot);
}

/* Exec drops loader Handles.  Install three fresh attenuated region grants
 * only after that destructive transition; no Handle grants framebuffer access. */
void
vconsoleexec(Proc *p)
{
    Vconsole *c;
    if(p == nil || p->console_slot == 0)
        return;
    c = &sessions[p->console_slot];
    if(!c->ready)
        panic("console: session not ready at exec");
    vobjinstallobj(c->region[Input], Vread|Vmeta);
    vobjinstallobj(c->region[Output], Vwrite|Vmeta);
    vobjinstallobj(c->region[Diagnostic], Vwrite|Vmeta);
    p->iomask = 0;
}

void
vconsoleexit(Proc *p)
{
    Vconsole *c;
    if(p == nil || p->console_slot == 0)
        return;
    c = &sessions[p->console_slot];
    c->alive = 0;
    vobjregionwriterrelease(c->region[Input]);
    if(current == p->console_slot)
        selectview(0);
    print("console: session %s pid=%lud exited\n", c->name, p->pid);
    p->console_slot = 0;
}
