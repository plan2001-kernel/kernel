/* Runs as a native boot ELF and exec replacement on a disposable volume. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include "process.h"
#include "plan2001_full.h"
#include "obj.h"
#include "vobj.h"
extern long objopen(unsigned long long,void *,long,unsigned long long,void *);
extern long objinvoke(int,void *,long);
static int report;
static volatile sig_atomic_t caught;
static sigjmp_buf escape;
static void handler(int sig) { caught=sig; }
static void jumping_handler(int sig) { (void)sig; siglongjmp(escape,1); }
static void emit(const char *s)
{
    ObjCall c={0};
    c.version=OBJ_ABI_VERSION; c.op=ObjOpWrite;
    c.in=(void *)s; c.inlen=strlen(s);
    (void)objinvoke(report,&c,sizeof c);
}
static void check(int test,const char *message)
{
    if(!test) { emit("PLAN2001-PROCESS-FAIL: "); emit(message); emit("\n"); _exit(99); }
}
static int reap(int pid,int *status)
{
    int result;
    do { result=(int)waitpid(pid,status,0); } while(result<0 && errno==EINTR);
    return result;
}
static void read_exact(int fd,char *buf,unsigned count)
{
    unsigned pos=0;
    while(pos<count) {
        int n=read(fd,buf+pos,count-pos);
        if(n<0 && errno==EINTR) continue;
        check(n>0,"pipe read"); pos+=(unsigned)n;
    }
}
int main(int argc,char **argv)
{
    int streams[2],pid,status,volume,extra,file;
    char buf[64],pidarg[24],extraarg[24];
    ObjOpenConsoleReq req={1,0};
    if(argc>1 && !strcmp(argv[1],"--exec-child")) {
        sigset_t mask;
        struct sigaction action;
        if(argc!=4 || getpid()!=atoi(argv[2]) || !getenv("PROCESS_EXEC_TEST") ||
           strcmp(getenv("PROCESS_EXEC_TEST"),"preserved")) _exit(90);
        errno=0;
        if(fcntl(atoi(argv[3]),F_GETFD)!=-1 || errno!=EBADF) _exit(91);
        if(sigaction(SIGUSR1,NULL,&action) || action.sa_handler!=SIG_DFL) _exit(92);
        if(sigaction(SIGUSR2,NULL,&action) || action.sa_handler!=SIG_IGN) _exit(93);
        if(sigprocmask(SIG_BLOCK,NULL,&mask) || !sigismember(&mask,SIGUSR1)) _exit(94);
        if(write(1,"exec-ok",7)!=7) _exit(95);
        _exit(37);
    }
    if(argc>1 && !strcmp(argv[1],"--exec-file")) {
        int fd=atoi(argv[2]);
        if(lseek(fd,0,SEEK_SET)!=0 || write(fd,"exec",4)!=4) _exit(65);
        _exit(41);
    }
    if(argc>1 && !strcmp(argv[1],"--exec-stdio")) {
        char byte;
        if(read(0,&byte,1)!=1 || byte!='i' || write(1,"out",3)!=3 ||
           write(2,"err",3)!=3 || kill(getppid(),SIGUSR1)<0) _exit(66);
        _exit(43);
    }
    report=(int)objopen(ObjOpenConsole,&req,sizeof req,0,NULL);
    check(report>0,"authorized report console");
    volume=(int)objopen(ObjOpenVolume,NULL,0,Vexecute|Vdelegate,NULL);
    check(volume>0,"explicit boot executable volume");
    check(p9_process_bind_executable("/bin/probe",volume,UINT64_C(0x4f45584543000003))==0,"register executable");
    /* Native console Handles cannot delegate. This test deliberately keeps
     * its report Handle outside the Unix descriptor projection. */
    (void)close(0); check(close(1)==0 && close(2)==0,"close native std descriptors");
    check(pipe(streams)==0,"pipe before fork");
    pid=(int)fork(); check(pid>=0,"real RFORKEX");
    if(!pid) {
        char *args[5],*environment[]={"PROCESS_EXEC_TEST=preserved",NULL};
        sigset_t mask;
        (void)close(streams[0]);
        if(dup2(streams[1],1)<0) _exit(80);
        if(streams[1]!=1) (void)close(streams[1]);
        extra=fcntl(1,F_DUPFD,10);
        if(extra<0 || fcntl(extra,F_SETFD,FD_CLOEXEC)<0) _exit(81);
        if(signal(SIGUSR1,handler)==SIG_ERR || signal(SIGUSR2,SIG_IGN)==SIG_ERR) _exit(82);
        sigemptyset(&mask); sigaddset(&mask,SIGUSR1);
        if(sigprocmask(SIG_BLOCK,&mask,NULL)<0) _exit(83);
        snprintf(pidarg,sizeof pidarg,"%d",(int)getpid());
        snprintf(extraarg,sizeof extraarg,"%d",extra);
        args[0]="probe"; args[1]="--exec-child"; args[2]=pidarg; args[3]=extraarg; args[4]=NULL;
        errno=0;
        if(execve("/missing",args,environment)!=-1 || errno!=ENOENT ||
           fcntl(extra,F_GETFD)!=FD_CLOEXEC) _exit(85);
        execve("/bin/probe",args,environment);
        _exit(84);
    }
    check(close(streams[1])==0,"parent drops writer");
    read_exact(streams[0],buf,7);
    check(!memcmp(buf,"exec-ok",7),"exec argv/env/pid/CLOEXEC/dispositions");
    check(reap(pid,&status)==pid && WIFEXITED(status) && WEXITSTATUS(status)==37,
          "waitpid preserves exact exec exit code");
    check(read(streams[0],buf,1)==0,"last inherited writer publishes EOF");
    check(close(streams[0])==0,"close inherited pipe");
    errno=0; check(waitpid(pid,NULL,WNOHANG)==-1 && errno==ECHILD,"completion consumed once");
    emit("PLAN2001-PROCESS-FORK-EXEC-WAIT-PASS\n");
    check(pipe(streams)==0,"signal test pipe");
    pid=(int)fork(); check(pid>=0,"signal child fork");
    if(!pid) {
        sigset_t mask,empty;
        (void)close(streams[0]);
        if(signal(SIGUSR1,handler)==SIG_ERR || signal(SIGCONT,handler)==SIG_ERR) _exit(70);
        if(write(streams[1],"r",1)!=1) _exit(71);
        while(!caught) { __asm__ volatile("" ::: "memory"); }
        if(caught!=SIGUSR1) _exit(72);
        sigemptyset(&mask); sigaddset(&mask,SIGUSR1);
        if(sigprocmask(SIG_BLOCK,&mask,NULL)<0) _exit(73);
        caught=0;
        if(raise(SIGUSR1) || sigpending(&empty) || !sigismember(&empty,SIGUSR1)) _exit(74);
        sigemptyset(&empty);
        if(sigsuspend(&empty)!=-1 || errno!=EINTR || caught!=SIGUSR1) _exit(75);
        if(write(streams[1],"s",1)!=1) _exit(76);
        while(caught!=SIGCONT) { __asm__ volatile("" ::: "memory"); }
        if(write(streams[1],"c",1)!=1) _exit(77);
        for(;;) { __asm__ volatile("" ::: "memory"); }
    }
    check(close(streams[1])==0,"signal parent drops writer");
    read_exact(streams[0],buf,1);
    check(kill(pid,SIGUSR1)==0,"capability-authorized signal delivery");
    read_exact(streams[0],buf,1); check(buf[0]=='s',"pending/mask/sigsuspend");
    check(kill(pid,SIGSTOP)==0,"unmaskable stop");
    do { extra=(int)waitpid(pid,&status,WUNTRACED); } while(extra<0 && errno==EINTR);
    check(extra==pid && WIFSTOPPED(status) && WSTOPSIG(status)==SIGSTOP,"waitpid stop state");
    check(waitpid(pid,NULL,WUNTRACED|WNOHANG)==0,"stop reported once without reaping");
    check(kill(pid,SIGCONT)==0,"continue stopped child");
    read_exact(streams[0],buf,1); check(buf[0]=='c',"continued child runs handler and user code");
    check(kill(pid,SIGKILL)==0,"unmaskable SIGKILL mailbox");
    check(reap(pid,&status)==pid && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,
          "waitpid reports signal death");
    check(read(streams[0],buf,1)==0,"signal exit releases writer");
    check(close(streams[0])==0,"signal pipe cleanup");
    errno=0; check(kill(123456,SIGTERM)==-1 && errno==ESRCH,"PID does not mint authority");
    emit("PLAN2001-PROCESS-SIGNALS-PASS\n");
    check(pipe(streams)==0 && close(streams[0])==0,"pipe with disconnected reader");
    pid=(int)fork(); check(pid>=0,"SIGPIPE fork");
    if(!pid) { (void)write(streams[1],"x",1); _exit(69); }
    check(reap(pid,&status)==pid && WIFSIGNALED(status) && WTERMSIG(status)==SIGPIPE,
          "default SIGPIPE terminates writer");
    pid=(int)fork(); check(pid>=0,"ignored SIGPIPE fork");
    if(!pid) {
        if(signal(SIGPIPE,SIG_IGN)==SIG_ERR) _exit(69);
        errno=0;
        if(write(streams[1],"x",1)!=-1 || errno!=EPIPE) _exit(69);
        _exit(0);
    }
    check(reap(pid,&status)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"ignored SIGPIPE returns EPIPE");
    check(close(streams[1])==0,"disconnected pipe cleanup");
    {
        int io[3][2];
        unsigned i;
        for(i=0;i<3;++i) check(pipe(io[i])==0,"three stdio pipe pairs");
        caught=0;
        check(signal(SIGUSR1,handler)!=SIG_ERR,"parent signal handler");
        pid=(int)fork(); check(pid>=0,"stdio fork");
        if(!pid) {
            char *args[]={"probe","--exec-stdio",NULL};
            (void)close(io[0][1]); (void)close(io[1][0]); (void)close(io[2][0]);
            if(dup2(io[0][0],0)<0 || dup2(io[1][1],1)<0 || dup2(io[2][1],2)<0) _exit(67);
            for(i=0;i<3;++i) {
                int endpoint=io[i][i ? 1 : 0];
                if(endpoint>2) (void)close(endpoint);
            }
            execve("/bin/probe",args,NULL); _exit(68);
        }
        check(close(io[0][0])==0 && close(io[1][1])==0 && close(io[2][1])==0,"parent closes unused stdio ends");
        check(write(io[0][1],"i",1)==1 && close(io[0][1])==0,"exec stdin");
        read_exact(io[1][0],buf,3); check(!memcmp(buf,"out",3),"exec stdout");
        read_exact(io[2][0],buf,3); check(!memcmp(buf,"err",3),"exec stderr");
        check(reap(pid,&status)==pid && WIFEXITED(status) && WEXITSTATUS(status)==43,"three-stream exec status");
        p9_signal_checkpoint();
        check(caught==SIGUSR1,"exec child signals parent through retained proxy authority");
        check(close(io[1][0])==0 && close(io[2][0])==0,"stdio cleanup");
        emit("PLAN2001-PROCESS-STDIO-PASS\n");
    }
    {
        volatile unsigned round;
        jmp_buf ordinary;
        int jumped=setjmp(ordinary);
        if(!jumped) longjmp(ordinary,0);
        check(jumped==1,"longjmp zero value becomes one");
        check(signal(SIGALRM,jumping_handler)!=SIG_ERR,"siglongjmp handler");
        for(round=0;round<2;++round) {
            if(!sigsetjmp(escape,1)) {
                (void)alarm(1);
                for(;;) { __asm__ volatile("" ::: "memory"); }
            }
        }
        emit("PLAN2001-PROCESS-SIGLONGJMP-PASS\n");
    }
    {
        extern unsigned long long _nsec(void);
        unsigned long long before=_nsec();
        check(sleep(1)==0 && _nsec()-before>=500000000ULL,"private note ticks do not interrupt POSIX sleep");
    }
    {
        static Pvd view;
        static UxService service;
        check(pvd_bootstrap(&view)==0,"file test PVD");
        uxservice_init(&service,&view);
        check(p9_full_bind_files(&service)==0,"file test explicit service");
    }
    file=open("/tmp/inherited",O_CREAT|O_TRUNC|O_RDWR,0600);
    check(file>=0 && write(file,"abc",3)==3 && lseek(file,0,SEEK_SET)==0,"file before fork");
    pid=(int)fork(); check(pid>=0,"file descriptor fork");
    if(!pid) {
        if(read(file,buf,1)!=1 || buf[0]!='a' || write(file,"Z",1)!=1) _exit(60);
        if(mkdir("/tmp/child",0700)<0 || chdir("/tmp/child")<0) _exit(61);
        _exit(39);
    }
    check(reap(pid,&status)==pid && WIFEXITED(status) && WEXITSTATUS(status)==39,"file child status");
    check(read(file,buf,1)==1 && buf[0]=='c',"fork shares file position");
    check(lseek(file,0,SEEK_SET)==0 && read(file,buf,3)==3 && !memcmp(buf,"aZc",3),
          "child file writes visible to parent");
    check(getcwd(buf,sizeof buf)!=NULL && !strcmp(buf,"/"),"cwd remains process local");
    check(chdir("/tmp/child")==0 && chdir("/")==0,"directory topology shared");
    pid=(int)fork(); check(pid>=0,"file exec fork");
    if(!pid) {
        char *args[]={"probe","--exec-file",extraarg,NULL};
        snprintf(extraarg,sizeof extraarg,"%d",file);
        execve("/bin/probe",args,NULL); _exit(64);
    }
    check(reap(pid,&status)==pid && WIFEXITED(status) && WEXITSTATUS(status)==41,"file exec status");
    check(lseek(file,0,SEEK_SET)==0 && read(file,buf,4)==4 && !memcmp(buf,"exec",4),
          "exec preserves shared file description");
    check(close(file)==0,"close last shared file descriptor");
    emit("PLAN2001-PROCESS-FILES-PASS\nPLAN2001-PROCESS-PASS\n");
    for(;;) (void)sleep(1);
}
