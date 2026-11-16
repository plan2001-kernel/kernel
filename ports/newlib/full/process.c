/* Unix process identities and signals are userland state. Native process
 * Handles authorize observation/control; an integer PID never opens one.
 * SPAWN is used for exec with a supervising process retaining the observable
 * identity. Both images expose the same Unix PID through the context.
 */
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <setjmp.h>
#include "process.h"
#include "project_fs.h"
#include "plan2001_full.h"
#include "obj.h"
#include "vobj.h"
#include "sysctl.h"
typedef unsigned long long uvlong;
#include "sys.h"
typedef uint64_t u64int;
typedef uint16_t u16int;
#include "ureg.h"

extern long objinvoke(int,void *,long), objclose(int);
extern long spawn(int,unsigned long long,char **,int,void *);
extern long notify(void (*)(void *,char *)), noted(int);
extern long p9_native_sleep_millis(long), p9_native_alarm_millis(long);
extern unsigned long long _nsec(void);
extern void _exits(char *);
extern int p9_native_getpid(void);
extern char **environ;
static int mailbox, parent_mailbox, unix_pid, parent_pid, active, proxy, parent_proxy;
static volatile sig_atomic_t interrupt_seen, dispatching;
static volatile unsigned ticks;
static struct Ureg *note_frame;
static volatile unsigned atomic_depth;
static volatile unsigned forwarding;
static unsigned long blocked, pending;
static struct sigaction actions[NSIG];
static P9Child children[P9_CHILDREN];
static P9Executable executables[P9_EXECUTABLES];
static uint64_t alarm_deadline;

static void forward_parent_signals(int);
static void stop_process(int);

long p9_process_call(int handle,unsigned op,void *out,
                     unsigned long long size,long long timeout)
{
    ObjCall call={0};
    call.version=OBJ_ABI_VERSION;
    call.op=op;
    call.out=out;
    call.outlen=size;
    call.arg0=(unsigned long long)timeout;
    return objinvoke(handle,&call,sizeof call);
}
static P9Context *box(int handle,int wait)
{
    long mapped;
    int tries=0;
    do {
        mapped=p9_full_region_map(handle,0);
        if(mapped>0) return (P9Context *)(uintptr_t)mapped;
        if(!wait || ++tries==2000) break;
        (void)p9_native_sleep_millis(1);
    } while(1);
    errno=EBUSY;
    return NULL;
}
static void unbox(P9Context *ctx)
{
    (void)p9_full_region_unmap((uintptr_t)ctx);
}
static unsigned long bit(int sig) { return 1UL<<(unsigned)sig; }
static int valid_signal(int sig) { return sig>0 && sig<NSIG; }
static unsigned long unmaskable(void) { return bit(SIGKILL)|bit(SIGSTOP); }

static int status_of(const ObjProcessInfo *info)
{
    unsigned a,b;
    if(info->cause==ObjProcCauseTerminated) return SIGKILL;
    if(info->cause!=ObjProcCauseExit) return SIGABRT;
    if(!info->msglen) return 0;
    if(info->msglen==10 && !memcmp(info->msg,"UXEXIT1:",8)) {
        a=(unsigned char)info->msg[8]; b=(unsigned char)info->msg[9];
        a=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:16;
        b=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:16;
        if(a<16 && b<16) return (int)((a*16+b)<<8);
    }
    if(info->msglen==10 && !memcmp(info->msg,"UXSIGN1:",8)) {
        a=(unsigned char)info->msg[8]; b=(unsigned char)info->msg[9];
        if(a>='0'&&a<='9' && b>='0'&&b<='9') {
            int sig=(int)((a-'0')*10+b-'0');
            if(valid_signal(sig)) return sig;
        }
    }
    /* Foreign native programs retain only their success/failure meaning. */
    return 1<<8;
}
static void observe_children(void)
{
    unsigned i;
    ++atomic_depth;
    for(i=0;i<P9_CHILDREN;++i) if(children[i].pid && !children[i].reported) {
        ObjProcessInfo info={0};
        if(p9_process_call(children[i].handle,ObjOpProcessInfo,&info,sizeof info,0)>=0 &&
           info.state==ObjProcCompleted) {
            children[i].status=status_of(&info);
            children[i].reported=1;
            pending|=bit(SIGCHLD);
        } else {
            P9Context *ctx=box(children[i].mailbox,0);
            if(ctx) {
                if(ctx->stop_signal && ctx->stop_count!=(unsigned)children[i].stop_notified) {
                    children[i].stop_notified=(int)ctx->stop_count;
                    if(!(actions[SIGCHLD].sa_flags&SA_NOCLDSTOP)) pending|=bit(SIGCHLD);
                }
                unbox(ctx);
            }
        }
    }
    --atomic_depth;
}
static void deliver(void)
{
    int sig;
    if(dispatching || atomic_depth) return;
    dispatching=1;
    for(sig=1;sig<NSIG;++sig) if((pending&~blocked)&bit(sig)) {
        struct sigaction action=actions[sig];
        unsigned long saved=blocked;
        pending&=~bit(sig);
        if(action.sa_handler==SIG_IGN) continue;
        if(action.sa_handler==SIG_DFL) {
            if(sig==SIGCHLD || sig==SIGCONT || sig==SIGURG || sig==SIGWINCH) continue;
            if(sig==SIGSTOP || sig==SIGTSTP || sig==SIGTTIN || sig==SIGTTOU) {
                stop_process(sig); continue;
            }
            p9_process_signal_exit(sig);
        }
        blocked|=(unsigned long)action.sa_mask|bit(sig);
        blocked&=~unmaskable();
        interrupt_seen=1;
        action.sa_handler(sig);
        blocked=saved;
    }
    dispatching=0;
}
void p9_signal_checkpoint(void)
{
    P9Context *ctx;
    if(!active || proxy || dispatching || atomic_depth) return;
    ctx=box(mailbox,0);
    if(ctx) {
        if(ctx->magic==P9_CONTEXT_MAGIC) {
            pending|=(unsigned long)ctx->pending;
            ctx->pending=0;
        }
        unbox(ctx);
    }
    if(alarm_deadline && _nsec()>=alarm_deadline) {
        alarm_deadline=0;
        pending|=bit(SIGALRM);
    }
    observe_children();
    deliver();
}
static void note_handler(void *registers,char *message)
{
    int saved=errno;
    note_frame=registers;
    if(!strcmp(message,"alarm")) {
        ++ticks;
        if(proxy) forward_parent_signals(0);
        else p9_signal_checkpoint();
        (void)p9_native_alarm_millis(10);
        errno=saved;
        note_frame=NULL;
        (void)noted(0); /* native NCONT */
    }
    /* Synchronous native faults enter the same disposition machinery. A
     * returning fault handler resumes at the fault; siglongjmp can recover. */
    {
        int sig=SIGABRT;
        if(strstr(message,"fault")) sig=SIGSEGV;
        else if(strstr(message,"divide")) sig=SIGFPE;
        else if(strstr(message,"invalid opcode")) sig=SIGILL;
        else if(strstr(message,"breakpoint") || strstr(message,"debug")) sig=SIGTRAP;
        if(blocked&bit(sig)) p9_process_signal_exit(sig);
        pending|=bit(sig);
        deliver();
        note_frame=NULL;
        (void)noted(0);
    }
    __builtin_unreachable();
}
extern _Noreturn void __real_longjmp(jmp_buf,int);
int p9_sigjmp_prepare(P9SigJmp *environment,int save)
{
    environment->save_mask=!!save;
    return save ? sigprocmask(SIG_SETMASK,NULL,&environment->mask) : 0;
}
_Noreturn void siglongjmp(sigjmp_buf environment,int value)
{
    if(environment[0].save_mask)
        (void)sigprocmask(SIG_SETMASK,&environment[0].mask,NULL);
    longjmp(environment[0].registers,value);
}
_Noreturn void __wrap_longjmp(jmp_buf environment,int value)
{
    struct Ureg *frame=note_frame;
    if(!value) value=1;
    dispatching=0;
    atomic_depth=0;
    if(frame) {
        /* Clear the kernel's outstanding note frame before leaving the
         * handler. Otherwise subsequent alarm/user notes stay suppressed.
         * Enter Newlib longjmp with SysV arguments on a synthetic frame
         * below the interrupted red zone and above the saved native Ureg. */
        frame->pc=(uintptr_t)__real_longjmp;
        frame->di=(uintptr_t)environment;
        frame->si=(unsigned)value;
        frame->sp=((frame->sp-128u)&~UINT64_C(15))-8u;
        *(uintptr_t *)(uintptr_t)frame->sp=0;
        note_frame=NULL;
        (void)p9_native_alarm_millis(10);
        (void)noted(0);
        __builtin_unreachable();
    }
    __real_longjmp(environment,value);
}
static int initialize(void)
{
    P9Context *ctx;
    if(active) return 0;
    unix_pid=p9_native_getpid();
    if(unix_pid<=0) return -1;
    mailbox=(int)p9_full_region_create((sizeof(P9Context)+4095u)&~4095u);
    if(mailbox<=0) { errno=ENOMEM; return -1; }
    ctx=box(mailbox,0);
    if(!ctx) { (void)objclose(mailbox); mailbox=0; return -1; }
    memset(ctx,0,sizeof(*ctx));
    ctx->magic=P9_CONTEXT_MAGIC;
    ctx->version=1;
    ctx->pid=unix_pid;
    ctx->ready=1;
    unbox(ctx);
    if(notify(note_handler)<0) { (void)objclose(mailbox); mailbox=0; errno=EIO; return -1; }
    active=1;
    (void)p9_native_alarm_millis(10);
    return 0;
}
int _getpid(void) { return active ? unix_pid : p9_native_getpid(); }
pid_t getppid(void) { return parent_pid; }
int p9_process_interrupted(void)
{
    int value;
    p9_signal_checkpoint();
    value=interrupt_seen;
    interrupt_seen=0;
    return value;
}
unsigned p9_process_tick(void) { return ticks; }
int p9_process_has_interrupt(void) { return interrupt_seen!=0; }
void p9_process_enter(void) { ++atomic_depth; }
void p9_process_leave(void)
{
    int saved=errno;
    if(atomic_depth) --atomic_depth;
    if(!atomic_depth) p9_signal_checkpoint();
    errno=saved;
}
int sigaction(int sig,const struct sigaction *action,struct sigaction *old)
{
    if(!valid_signal(sig) || (action&&(sig==SIGKILL||sig==SIGSTOP))) {
        errno=EINVAL; return -1;
    }
    if(action && (action->sa_flags&~SA_NOCLDSTOP)) { errno=ENOTSUP; return -1; }
    if(initialize()<0) return -1;
    ++atomic_depth;
    if(old) *old=actions[sig];
    if(action) actions[sig]=*action;
    --atomic_depth;
    return 0;
}
void (*signal(int sig,void (*handler)(int)))(int)
{
    struct sigaction action={0},old;
    action.sa_handler=handler;
    return sigaction(sig,&action,&old)<0 ? SIG_ERR : old.sa_handler;
}
#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember
int sigemptyset(sigset_t *set) { if(!set) { errno=EFAULT; return -1; } *set=0; return 0; }
int sigfillset(sigset_t *set) { if(!set) { errno=EFAULT; return -1; } *set=(sigset_t)~0UL; return 0; }
int sigaddset(sigset_t *set,int sig)
{
    if(!set || !valid_signal(sig)) { errno=EINVAL; return -1; }
    *set|=(sigset_t)bit(sig); return 0;
}
int sigdelset(sigset_t *set,int sig)
{
    if(!set || !valid_signal(sig)) { errno=EINVAL; return -1; }
    *set&=(sigset_t)~bit(sig); return 0;
}
int sigismember(const sigset_t *set,int sig)
{
    if(!set || !valid_signal(sig)) { errno=EINVAL; return -1; }
    return !!((unsigned long)*set&bit(sig));
}
int sigprocmask(int how,const sigset_t *set,sigset_t *old)
{
    if(set && how!=SIG_BLOCK && how!=SIG_UNBLOCK && how!=SIG_SETMASK) { errno=EINVAL; return -1; }
    if(initialize()<0) return -1;
    ++atomic_depth;
    if(old) *old=(sigset_t)blocked;
    if(set) {
        if(how==SIG_BLOCK) blocked|=(unsigned long)*set;
        else if(how==SIG_UNBLOCK) blocked&=~(unsigned long)*set;
        else blocked=(unsigned long)*set;
        blocked&=~unmaskable();
    }
    --atomic_depth;
    p9_signal_checkpoint();
    return 0;
}
int sigpending(sigset_t *set)
{
    if(!set) { errno=EFAULT; return -1; }
    if(initialize()<0) return -1;
    p9_signal_checkpoint();
    *set=(sigset_t)pending;
    return 0;
}
int sigsuspend(const sigset_t *set)
{
    unsigned long saved;
    if(!set) { errno=EFAULT; return -1; }
    if(initialize()<0) return -1;
    saved=blocked;
    blocked=(unsigned long)*set&~unmaskable();
    interrupt_seen=0;
    do { p9_signal_checkpoint(); if(!interrupt_seen) (void)p9_native_sleep_millis(10); }
    while(!interrupt_seen);
    blocked=saved;
    interrupt_seen=0;
    errno=EINTR;
    return -1;
}
unsigned alarm(unsigned seconds)
{
    uint64_t now=_nsec();
    unsigned previous=alarm_deadline>now ? (unsigned)((alarm_deadline-now+999999999)/1000000000) : 0;
    if(initialize()<0) return 0;
    alarm_deadline=seconds ? now+(uint64_t)seconds*1000000000 : 0;
    return previous;
}
int _kill(int pid,int sig)
{
    int target=0;
    unsigned i;
    P9Context *ctx;
    if(sig<0 || sig>=NSIG) { errno=EINVAL; return -1; }
    if(initialize()<0) return -1;
    if(pid==unix_pid) {
        if(!sig) return 0;
        pending|=bit(sig); deliver(); return 0;
    }
    if(pid<=0) { errno=ENOTSUP; return -1; }
    if(pid==parent_pid) target=parent_mailbox;
    for(i=0;i<P9_CHILDREN;++i) if(children[i].pid==pid) {
        ObjProcessInfo info={0};
        if(p9_process_call(children[i].handle,ObjOpProcessInfo,&info,sizeof info,0)<0 ||
           info.state==ObjProcCompleted) { errno=ESRCH; return -1; }
        target=children[i].mailbox;
        break;
    }
    if(!target && pid==parent_pid && parent_proxy) {
        if(!sig) return 0;
        ctx=box(mailbox,1);
        if(!ctx) return -1;
        ctx->parent_pending|=bit(sig);
        unbox(ctx);
        return 0;
    }
    if(!target) { errno=ESRCH; return -1; }
    if(!sig) return 0;
    ctx=box(target,1);
    if(!ctx) return -1;
    if(ctx->magic!=P9_CONTEXT_MAGIC) { unbox(ctx); errno=ESRCH; return -1; }
    ctx->pending|=bit(sig);
    unbox(ctx);
    return 0;
}
int raise(int sig) { return _kill(_getpid(),sig); }

pid_t waitpid(pid_t pid,int *status,int flags)
{
    unsigned i;
    int found;
    if(flags&~(WNOHANG|WUNTRACED)) { errno=ENOTSUP; return -1; }
    if(pid==0 || pid<-1) { errno=ENOTSUP; return -1; }
    (void)p9_process_interrupted();
    for(;;) {
        p9_signal_checkpoint();
        observe_children();
        p9_process_enter();
        found=0;
        for(i=0;i<P9_CHILDREN;++i) if(children[i].pid && (pid==-1||children[i].pid==pid)) {
            P9Child *child=&children[i];
            found=1;
            if(child->reported) {
                int result=child->pid;
                if(status) *status=child->status;
                (void)objclose(child->handle);
                (void)objclose(child->mailbox);
                memset(child,0,sizeof(*child));
                p9_process_leave();
                return result;
            }
            if(flags&WUNTRACED) {
                P9Context *ctx=box(child->mailbox,0);
                if(ctx) {
                    unsigned stopped=ctx->stop_signal,seq=ctx->stop_count;
                    unbox(ctx);
                    if(stopped && seq!=(unsigned)child->stop_seen) {
                        int result=child->pid;
                        child->stop_seen=(int)seq;
                        if(status) *status=(int)(stopped<<8)|0x7f;
                        p9_process_leave();
                        return result;
                    }
                }
            }
        }
        p9_process_leave();
        if(!found) { errno=ECHILD; return -1; }
        if(flags&WNOHANG) return 0;
        if(p9_process_interrupted()) { errno=EINTR; return -1; }
        (void)p9_native_sleep_millis(10);
    }
}
int _wait(int *status) { return (int)waitpid(-1,status,0); }
pid_t wait(int *status) { return waitpid(-1,status,0); }

static int fork_impl(void)
{
    P9Plan plan={0};
    P9FdImage image;
    struct Rforkex options={0};
    int childbox, parentindex, selfindex, handles[P9_GRANTS], oldpid;
    unsigned i,slot;
    long result;
    P9Context *ctx;
    if(initialize()<0) return -1;
    for(slot=0;slot<P9_CHILDREN;++slot) if(!children[slot].pid) break;
    if(slot==P9_CHILDREN) { errno=EAGAIN; return -1; }
    childbox=(int)p9_full_region_create((sizeof(P9Context)+4095u)&~4095u);
    if(childbox<=0) { errno=ENOMEM; return -1; }
    ctx=box(childbox,0);
    if(!ctx) { (void)objclose(childbox); return -1; }
    memset(ctx,0,sizeof(*ctx)); ctx->magic=P9_CONTEXT_MAGIC; ctx->version=1;
    unbox(ctx);
    selfindex=p9_plan_add(&plan,childbox,Vread|Vwrite|Vdelegate);
    parentindex=p9_plan_add(&plan,mailbox,Vread|Vwrite|Vdelegate);
    if(selfindex<0 || parentindex<0 || p9_fd_export(&image,&plan,0)<0) goto fail;
    for(i=0;i<P9_EXECUTABLES;++i) if(executables[i].volume>0 &&
       p9_plan_add(&plan,executables[i].volume,Vexecute|Vdelegate)<0) goto undo;
    options.version=RFORKEX_VERSION;
    options.count=plan.count;
    for(i=0;i<plan.count;++i) {
        options.h[i].handle=(unsigned)plan.grants[i].handle;
        options.h[i].rights=plan.grants[i].rights;
        options.h[i].dstslot=i+1;
    }
    oldpid=unix_pid;
    result=rforkex((1u<<4)|(1u<<1)|(1u<<3),&options,NULL);
    if(result<0) { errno=EAGAIN; goto undo; }
    if(!result) {
        if(p9_plan_resolve(handles,plan.count)<0) _exits("inherit handles");
        mailbox=handles[selfindex]; parent_mailbox=handles[parentindex];
        parent_pid=oldpid; unix_pid=p9_native_getpid(); parent_proxy=0;
        memset(children,0,sizeof children);
        pending=0; interrupt_seen=0; dispatching=0; atomic_depth=1; note_frame=NULL; alarm_deadline=0;
        for(i=0;i<P9_EXECUTABLES;++i) if(executables[i].volume>0) {
            unsigned j;
            for(j=0;j<plan.count;++j) if(plan.grants[j].handle==executables[i].volume) break;
            if(j==plan.count) _exits("inherit executable");
            executables[i].volume=handles[j];
        }
        if(p9_fd_import(&image,handles,plan.count)<0) _exits("inherit descriptors");
        ctx=box(mailbox,1);
        if(!ctx) _exits("inherit mailbox");
        ctx->pid=unix_pid; ctx->ppid=parent_pid; ctx->ready=1;
        unbox(ctx);
        (void)notify(note_handler); (void)p9_native_alarm_millis(10);
        return 0;
    }
    {
        ObjProcessInfo info={0};
        if(p9_process_call((int)result,ObjOpProcessInfo,&info,sizeof info,0)<0) {
            (void)p9_process_call((int)result,ObjOpProcessTerminate,NULL,0,0);
            (void)objclose((int)result); errno=EIO; goto undo;
        }
        children[slot]=(P9Child){.pid=(int)info.pid,.handle=(int)result,.mailbox=childbox};
        return (int)info.pid;
    }
undo:
    p9_fd_export_undo(&image);
fail:
    (void)objclose(childbox);
    return -1;
}
int _fork(void)
{
    int result;
    p9_process_enter(); result=fork_impl(); p9_process_leave();
    return result;
}
pid_t fork(void) { return (pid_t)_fork(); }

int p9_process_bind_executable(const char *path,int volume,uint64_t oid)
{
    unsigned i;
    if(!path || !*path || strlen(path)>=sizeof executables[0].path || volume<=0 || !oid) {
        errno=EINVAL; return -1;
    }
    for(i=0;i<P9_EXECUTABLES;++i) if(!executables[i].volume || !strcmp(path,executables[i].path)) {
        strcpy(executables[i].path,path); executables[i].volume=volume; executables[i].oid=oid;
        return 0;
    }
    errno=ENOSPC; return -1;
}

/* Resolve virtual applet names only to a registered native executable.
 * No suffix/prefix wildcard, new OID opening or native authority is involved.
 * The compiled table is immutable and reproduced in every exec image.
 */
int p9_process_executable(const char *path,uint64_t *oid)
{
    char normalized[4096];
    const char *target=path;
    unsigned i;
    if(!path) return 0;
    if(p9_project_root()) {
        if(p9_project_normalize(path,normalized)) return 0;
        target=normalized;
    }
    if(p9_multicall_resolve) {
        const char *alias=p9_multicall_resolve(target);
        if(alias) target=alias;
    }
    for(i=0;i<P9_EXECUTABLES;++i)
        if(executables[i].volume>0 && !strcmp(target,executables[i].path)) {
            if(oid) *oid=executables[i].oid;
            return (int)i+1;
        }
    return 0;
}

void p9_process_exit(int code)
{
    static const char hex[]="0123456789abcdef";
    char message[11]="UXEXIT1:00";
    unsigned value=(unsigned)code&255u;
    message[8]=hex[value>>4]; message[9]=hex[value&15];
    _exits(value ? message : "");
    __builtin_unreachable();
}
void p9_process_signal_exit(int sig)
{
    char message[11]="UXSIGN1:00";
    message[8]=(char)('0'+sig/10); message[9]=(char)('0'+sig%10);
    p9_full_close_descriptors();
    _exits(message);
    __builtin_unreachable();
}

static int exec_impl(const char *path,char *const argv[],char *const envp[])
{
    P9Plan plan={0};
    P9Context image,*ctx;
    struct {
        uint64_t cpu,membudget,flags,count;
        struct { uint64_t handle,rights; } grants[P9_GRANTS];
    } options={0};
    unsigned i,j,argc=0,envsize=0;
    long child;
    int selected=-1,saved;
    if(!path || !argv || !argv[0]) { errno=EFAULT; return -1; }
    if(initialize()<0) return -1;
    selected=p9_process_executable(path,NULL)-1;
    if(selected<0) { errno=ENOENT; return -1; }
    while(argv[argc]) if(++argc>64) { errno=E2BIG; return -1; }
    memset(&image,0,sizeof image);
    image.magic=P9_CONTEXT_MAGIC; image.version=1; image.exec=1;
    image.pid=unix_pid; image.ppid=parent_pid; image.blocked=blocked;
    image.alarm_deadline=alarm_deadline;
    image.self=p9_plan_add(&plan,mailbox,Vread|Vwrite|Vdelegate);
    /* The exec proxy forwards parent signals while it waits for the
     * replacement process, preserving the Unix process identity. */
    image.parent=-1;
    if(image.self<0) return -1;
    for(i=1;i<NSIG;++i) if(actions[i].sa_handler==SIG_IGN) image.ignored|=bit((int)i);
    for(i=0;envp && envp[i];++i) {
        size_t n=strlen(envp[i])+1;
        if(n>P9_ENV_BYTES-envsize) { errno=E2BIG; return -1; }
        memcpy(image.environment+envsize,envp[i],n); envsize+=(unsigned)n;
    }
    image.environment_size=envsize;
    if(p9_fd_export(&image.fds,&plan,1)<0) return -1;
    for(i=0;i<P9_EXECUTABLES;++i) if(executables[i].volume>0) {
        image.executables[i]=executables[i];
        image.executables[i].volume=p9_plan_add(&plan,executables[i].volume,Vexecute|Vdelegate);
        if(image.executables[i].volume<0) goto undo;
    }
    for(i=0;i<P9_CHILDREN;++i) if(children[i].pid) {
        image.children[i]=children[i];
        image.children[i].handle=p9_plan_add(&plan,children[i].handle,Vobserve|Vcontrol|Vmeta|Vdelegate);
        image.children[i].mailbox=p9_plan_add(&plan,children[i].mailbox,Vread|Vwrite|Vdelegate);
        if(image.children[i].handle<0 || image.children[i].mailbox<0) goto undo;
    }
    image.grants=(int)plan.count;
    ctx=box(mailbox,1);
    if(!ctx) goto undo;
    image.pending=ctx->pending|pending;
    image.stop_count=ctx->stop_count;
    *ctx=image;
    unbox(ctx);
    options.cpu=~UINT64_C(0); options.flags=2; options.count=plan.count;
    for(j=0;j<plan.count;++j) {
        options.grants[j].handle=(unsigned)plan.grants[j].handle;
        options.grants[j].rights=plan.grants[j].rights;
    }
    /* The caller remains intact until the native loader has validated and
     * published a real replacement. No descriptors close on SPAWN failure. */
    child=spawn(executables[selected].volume,executables[selected].oid,
                (char **)argv,(int)argc,&options);
    if(child<0) {
        ctx=box(mailbox,1);
        if(ctx) { ctx->exec=0; unbox(ctx); }
        errno=ENOEXEC; goto undo;
    }
    proxy=1;
    p9_full_close_descriptors();
    p9_process_leave();
    for(;;) {
        ObjProcessInfo info={0};
        forward_parent_signals(1);
        if(p9_process_call((int)child,ObjOpProcessWait,&info,sizeof info,100)<0) continue;
        if(info.state==ObjProcCompleted) {
            char message[OBJ_PROCESS_MSG_MAX+1];
            forward_parent_signals(1);
            if(info.cause==ObjProcCauseExit && info.msglen<=OBJ_PROCESS_MSG_MAX) {
                memcpy(message,info.msg,info.msglen); message[info.msglen]=0;
                _exits(message);
            }
            if(info.cause==ObjProcCauseTerminated) p9_process_signal_exit(SIGKILL);
            p9_process_signal_exit(SIGABRT);
        }
    }
undo:
    saved=errno;
    p9_fd_export_undo(&image.fds);
    errno=saved;
    return -1;
}
int execve(const char *path,char *const argv[],char *const envp[])
{
    int result;
    p9_process_enter(); result=exec_impl(path,argv,envp); p9_process_leave();
    return result;
}
int _execve(const char *path,char *const argv[],char *const envp[])
{ return execve(path,argv,envp); }

int p9_process_startup(void)
{
    VobjListRec records[P9_GRANTS];
    SysctlReq req={0};
    P9Context image;
    P9Context *mapped;
    int first=0,handles[P9_GRANTS];
    unsigned i;
    long n;
    req.version=SYSCTL_ABI_VERSION; req.data=records; req.datalen=sizeof records;
    n=sysctl(SysctlScopeOwnProcess,SysctlHandleList,&req,sizeof req);
    if(n<0 || n>P9_GRANTS) { errno=EACCES; return -1; }
    for(i=0;i<(unsigned)n;++i)
        if((records[i].handle&VhandleSlotMask)==1 && records[i].kind==VkindRegion &&
           records[i].rights==(Vread|Vwrite|Vdelegate)) first=(int)records[i].handle;
    if(!first) return 0;
    mapped=box(first,1);
    if(!mapped) return -1;
    if(mapped->magic!=P9_CONTEXT_MAGIC || mapped->version!=1 || !mapped->exec) {
        unbox(mapped); errno=EACCES; return -1;
    }
    image=*mapped; mapped->ready=1; unbox(mapped);
    if(image.grants!=(int)n || p9_plan_resolve(handles,(unsigned)n)<0 ||
       image.self!=0 || image.pid<=0 || image.parent<-1 || image.parent>=n ||
       image.environment_size>P9_ENV_BYTES) { errno=EACCES; return -1; }
    mailbox=first; unix_pid=image.pid; parent_pid=image.ppid;
    parent_mailbox=image.parent<0 ? 0 : handles[image.parent];
    parent_proxy=image.parent<0 && parent_pid>0;
    blocked=(unsigned long)image.blocked&~unmaskable();
    alarm_deadline=image.alarm_deadline;
    for(i=1;i<NSIG;++i) if(image.ignored&bit((int)i)) actions[i].sa_handler=SIG_IGN;
    for(i=0;i<P9_EXECUTABLES;++i) if(image.executables[i].oid) {
        int index=image.executables[i].volume;
        if(index<0 || index>=n || !memchr(image.executables[i].path,0,256)) return -1;
        executables[i]=image.executables[i]; executables[i].volume=handles[index];
    }
    for(i=0;i<P9_CHILDREN;++i) if(image.children[i].pid) {
        int h=image.children[i].handle,m=image.children[i].mailbox;
        if(h<0 || h>=n || m<0 || m>=n) return -1;
        children[i]=image.children[i]; children[i].handle=handles[h]; children[i].mailbox=handles[m];
    }
    if(p9_fd_import(&image.fds,handles,(unsigned)n)<0) return -1;
    /* Strings are bounded and copied before publishing environ. */
    {
        unsigned pos=0,count=0,j=0;
        char **env;
        char *storage;
        while(pos<image.environment_size) {
            char *end=memchr(image.environment+pos,0,image.environment_size-pos);
            if(!end) return -1;
            pos=(unsigned)(end-image.environment)+1; ++count;
        }
        env=calloc(count+1,sizeof(*env));
        storage=malloc(image.environment_size ? image.environment_size : 1);
        if(!env || !storage) { free(env); free(storage); errno=ENOMEM; return -1; }
        memcpy(storage,image.environment,image.environment_size);
        for(pos=0;pos<image.environment_size;pos+=(unsigned)strlen(storage+pos)+1) env[j++]=storage+pos;
        environ=env;
    }
    active=1;
    if(notify(note_handler)<0) return -1;
    (void)p9_native_alarm_millis(10);
    return 1;
}

static void forward_parent_signals(int wait)
{
    P9Context *source,*destination;
    if(!parent_mailbox || forwarding) return;
    forwarding=1;
    source=box(mailbox,wait);
    if(source) {
        if(source->parent_pending) {
            destination=box(parent_mailbox,wait);
            if(destination) {
                destination->pending|=source->parent_pending;
                source->parent_pending=0;
                unbox(destination);
            }
        }
        unbox(source);
    }
    forwarding=0;
}

static void stop_process(int sig)
{
    P9Context *ctx=box(mailbox,1);
    if(!ctx) return;
    ctx->stop_signal=(unsigned)sig; ++ctx->stop_count;
    unbox(ctx);
    for(;;) {
        ctx=box(mailbox,1);
        if(ctx) {
            pending|=(unsigned long)ctx->pending; ctx->pending=0;
            if(pending&bit(SIGKILL)) { unbox(ctx); p9_process_signal_exit(SIGKILL); }
            if(pending&bit(SIGCONT)) {
                ctx->stop_signal=0; unbox(ctx); break;
            }
            unbox(ctx);
        }
        (void)p9_native_sleep_millis(10);
    }
    pending&=~(bit(SIGSTOP)|bit(SIGTSTP)|bit(SIGTTIN)|bit(SIGTTOU));
}
