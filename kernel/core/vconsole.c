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
    Cmdline = 128,
    Kconshistory = 4096,
};
typedef struct Vconsole Vconsole;
struct Vconsole {
    char name[32];
    Vobj *region[3];
    uvlong iseq, oseq, dseq;
    char history[History];
    int nhistory;
    ulong pid;
    int defined, ready, alive, bound;
};
static Vconsole sessions[Consoles+1];
static int nsessions, current, consolemanagerready;
/* Only the manager switches the framebuffer or mutates the history. */
static volatile int requestedview = -1;
static int keyboardclaimed;
/* SystemConsole command prompt: kernel-owned, independent of all processes. */
static char cmdline[Cmdline];
static int ncmdline;
static char kconshistory[Kconshistory];
static int nkconshistory;
static Lock kconslock;

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

/* SystemConsole command transcript.  It is separate from kmesg so that
 * kernel diagnostics stay a clean boot/error log and shell-like command
 * output does not recursively appear in its own `dmesg` result. */
static void
kconshistoryappend(char *buf, int n)
{
    int cut;

    if(n <= 0)
        return;
    lock(&kconslock);
    if(n >= Kconshistory){
        buf += n - Kconshistory;
        n = Kconshistory;
        nkconshistory = 0;
    }
    if(nkconshistory + n > Kconshistory){
        cut = nkconshistory + n - Kconshistory;
        memmove(kconshistory, kconshistory + cut, nkconshistory - cut);
        nkconshistory -= cut;
    }
    memmove(kconshistory + nkconshistory, buf, n);
    nkconshistory += n;
    unlock(&kconslock);
}

static void
kconsbytes(char *buf, int n)
{
    if(n <= 0)
        return;
    kconshistoryappend(buf, n);
    fbconsoleappend(0, buf, n);
}

static void
kconsputs(char *s)
{
    if(s != nil)
        kconsbytes(s, strlen(s));
}

static void
kconsprintf(char *fmt, ...)
{
    char buf[256];
    va_list arg;
    int n;

    va_start(arg, fmt);
    n = vseprint(buf, buf+sizeof buf, fmt, arg) - buf;
    va_end(arg);
    kconsbytes(buf, n);
}

static void
cmdhelp(void)
{
    kconsputs("commands:\n"
              "  help      this command list\n"
              "  status    kernel and boot status\n"
              "  sessions  console slot, name, pid and state\n"
              "  dmesg     kernel diagnostics\n");
}

static void
cmdstatus(void)
{
    char buf[512];
    int i, live, n;

    n = bootstatustext(buf, sizeof buf);
    if(n > 0)
        kconsbytes(buf, n);
    live = 0;
    for(i=1; i<=nsessions; i++)
        if(sessions[i].alive)
            live++;
    kconsprintf("console: %d sessions, %d live\n", nsessions, live);
}

static void
cmdsessions(void)
{
    Vconsole *c;
    int i;

    kconsprintf("console sessions: %d\n", nsessions);
    for(i=1; i<=nsessions; i++){
        c = &sessions[i];
        kconsprintf("  F%d slot=%d name=%s pid=%lud %s\n",
                    i+1, i, c->name, c->pid,
                    c->alive ? "live" : c->bound ? "exited" : "starting");
    }
}

static void
cmddmesg(void)
{
    char buf[256];
    long off, n;

    for(off=0; (n=kmesgreadat(buf, sizeof buf, off)) > 0; off+=n)
        kconsbytes(buf, (int)n);
}

static void
cmdexecute(char *line)
{
    char *p, *tok, *end;

    for(p=line; *p == ' ' || *p == '\t'; p++)
        ;
    tok = p;
    while(*p != 0 && *p != ' ' && *p != '\t')
        p++;
    end = p;
    if(end == tok)
        return;
    *end = 0;
    if(strcmp(tok, "help") == 0)
        cmdhelp();
    else if(strcmp(tok, "status") == 0)
        cmdstatus();
    else if(strcmp(tok, "sessions") == 0)
        cmdsessions();
    else if(strcmp(tok, "dmesg") == 0)
        cmddmesg();
    else{
        kconsprintf("kernel: unknown command: %s\n", tok);
        kconsputs("try 'help'\n");
    }
}

/* Bounded line editor for the kernel-owned SystemConsole.  It never
 * delegates to a user process and never accepts control sequences. */
static void
kconskey(int c)
{
    char ch;

    if(c == '\r' || c == '\n'){
        kconsputs("\n");
        cmdline[ncmdline] = 0;
        cmdexecute(cmdline);
        ncmdline = 0;
        kconsputs("kernel> ");
        return;
    }
    if(c == '\b'){
        if(ncmdline > 0){
            ncmdline--;
            kconsputs("\b \b");
        }
        return;
    }
    if(c < 0x20 || c > 0x7e)
        return;
    if(ncmdline >= (int)sizeof cmdline - 1)
        return;
    cmdline[ncmdline++] = c;
    ch = c;
    kconsbytes(&ch, 1);
}

/* Render the SystemConsole as kmesg followed by the command transcript.
 * Live print() output still appears immediately, but a periodic redraw puts
 * the always-returning prompt back below newly arrived diagnostics. */
static void
showsystem(int announce)
{
    fbconsoleselect(0, kmesg.buf, kmesg.n);
    if(announce)
        print("console: focus SystemConsole\n");
    lock(&kconslock);
    if(nkconshistory > 0)
        fbconsoleappend(0, kconshistory, nkconshistory);
    unlock(&kconslock);
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
        showsystem(1);
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
            if(target <= nsessions){
                requestedview = target;
                coherence();
            }
            continue;
        }
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
        target = current;
        if(target == 0){
            /* Slot 0 is the kernel command prompt, never a user process. */
            if(!ctl && !alt)
                kconskey(b);
            continue;
        }
        if(target < 0 || target > nsessions || !sessions[target].alive)
            continue;
        c = &sessions[target];
        if(b != 0 && vobjregionringpublish(c->region[Input], &c->iseq, &b, 1) < 0)
            print("console: input publish failed slot=%d\n", target);
    }
}

static void
drain(Vconsole *c, int which, uvlong *seq)
{
    uvlong tail, s;
    ulong n;
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
    uvlong lastkmesg;
    int i, k, refreshwait;
    USED(arg);
    lastkmesg = 0;
    refreshwait = 0;
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
    consolemanagerready = 1;
    print("console: SystemConsole ready, %d independent sessions\n", nsessions);
    kconsputs("kernel> ");
    for(;;){
        if(requestedview >= 0){
            k = requestedview;
            requestedview = -1;
            selectview(k);
            lastkmesg = kmesg.n;
        }
        if(current > 0 && !sessions[current].alive){
            selectview(0);
            lastkmesg = kmesg.n;
        }
        for(i=1; i<=nsessions; i++){
            c = &sessions[i];
            if(c->ready){
                drain(c, Output, &c->oseq);
                drain(c, Diagnostic, &c->dseq);
            }
        }
        /* New kernel diagnostics would otherwise leave the prompt above the
         * final log line.  Redraw at a bounded rate while SystemConsole is
         * selected; always finish with the command prompt and current line. */
        if(current == 0 && kmesg.n != lastkmesg){
            if(++refreshwait >= 10){
                showsystem(0);
                lastkmesg = kmesg.n;
                refreshwait = 0;
            }
        }else
            refreshwait = 0;
        tsleep(&up->sleep, return0, nil, 20);
    }
}

void
vconsolebind(Proc *p, int slot)
{
    if(p == nil || slot < 1 || slot > nsessions || !sessions[slot].defined)
        panic("console: invalid process binding");
    while(!consolemanagerready)
        tsleep(&p->sleep, return0, nil, 10);
    p->console_slot = slot;
    sessions[slot].alive = 1;
    sessions[slot].bound = 1;
    sessions[slot].pid = p->pid;
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
    /* The manager does the fallback in its own context, avoiding a
     * framebuffer/history race against a simultaneous output drain. */
    print("console: session %s pid=%lud exited\n", c->name, p->pid);
    p->console_slot = 0;
}
