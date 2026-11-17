/* POSIX command streams use the existing bounded native process projection.
 * There is no host shell, vfork/shared address space, or new kernel transport.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include "process.h"
extern char **environ;
typedef struct { FILE *file; pid_t pid; } CommandStream;
static CommandStream streams[P9_CHILDREN];
static int reap(pid_t child)
{
    int status, rc;
    do { rc=(int)waitpid(child,&status,0); } while(rc<0 && errno==EINTR);
    return rc<0 ? -1 : status;
}
static void shell(const char *command)
{
    char *args[]={"sh","-c",(char *)command,NULL};
    execve("/bin/sh",args,environ);
    _exit(127);
}
int system(const char *command)
{
    struct sigaction ignore={0},oldint,oldquit;
    sigset_t block,oldmask;
    int status,saved;
    pid_t child;
    if(!command) return p9_process_executable("/bin/sh",NULL)!=0;
    ignore.sa_handler=SIG_IGN;
    if(sigaction(SIGINT,&ignore,&oldint)<0) return -1;
    if(sigaction(SIGQUIT,&ignore,&oldquit)<0) {
        saved=errno; (void)sigaction(SIGINT,&oldint,NULL); errno=saved; return -1;
    }
    sigemptyset(&block); sigaddset(&block,SIGCHLD);
    if(sigprocmask(SIG_BLOCK,&block,&oldmask)<0) {
        saved=errno; (void)sigaction(SIGINT,&oldint,NULL);
        (void)sigaction(SIGQUIT,&oldquit,NULL); errno=saved; return -1;
    }
    child=fork();
    if(!child) {
        if(oldint.sa_handler!=SIG_IGN) { ignore.sa_handler=SIG_DFL; (void)sigaction(SIGINT,&ignore,NULL); }
        if(oldquit.sa_handler!=SIG_IGN) { ignore.sa_handler=SIG_DFL; (void)sigaction(SIGQUIT,&ignore,NULL); }
        (void)sigprocmask(SIG_SETMASK,&oldmask,NULL);
        shell(command);
    }
    status=child<0 ? -1 : reap(child);
    saved=errno;
    (void)sigaction(SIGINT,&oldint,NULL); (void)sigaction(SIGQUIT,&oldquit,NULL);
    (void)sigprocmask(SIG_SETMASK,&oldmask,NULL);
    errno=saved;
    return status;
}
int _system_r(struct _reent *reent,const char *command)
{ (void)reent; return system(command); }
FILE *popen(const char *command,const char *mode)
{
    int fds[2],reading,cloexec,saved;
    unsigned slot;
    FILE *file;
    pid_t child;
    if(!command || !mode || (mode[0]!='r' && mode[0]!='w') ||
       (mode[1] && (mode[1]!='e' || mode[2]))) { errno=EINVAL; return NULL; }
    reading=mode[0]=='r'; cloexec=mode[1]=='e';
    for(slot=0;slot<P9_CHILDREN && streams[slot].file;++slot) {}
    if(slot==P9_CHILDREN) { errno=EMFILE; return NULL; }
    if(pipe(fds)<0) return NULL;
    file=fdopen(fds[reading ? 0 : 1],reading ? "r" : "w");
    if(!file) { saved=errno; close(fds[0]); close(fds[1]); errno=saved; return NULL; }
    if(cloexec && fcntl(fileno(file),F_SETFD,FD_CLOEXEC)<0) {
        saved=errno; close(fds[reading ? 1 : 0]); fclose(file); errno=saved; return NULL;
    }
    child=fork();
    if(!child) {
        int source=fds[reading ? 1 : 0],target=reading ? 1 : 0;
        for(unsigned i=0;i<P9_CHILDREN;++i)
            if(streams[i].file) close(fileno(streams[i].file));
        close(fds[reading ? 0 : 1]);
        if(dup2(source,target)<0) _exit(127);
        if(source!=target) close(source);
        shell(command);
    }
    saved=errno;
    close(fds[reading ? 1 : 0]);
    if(child<0) { fclose(file); errno=saved; return NULL; }
    streams[slot]=(CommandStream){file,child};
    return file;
}
int pclose(FILE *file)
{
    unsigned slot;
    int failed,saved,status;
    pid_t child;
    for(slot=0;slot<P9_CHILDREN && streams[slot].file!=file;++slot) {}
    if(!file || slot==P9_CHILDREN) { errno=EINVAL; return -1; }
    child=streams[slot].pid;
    streams[slot]=(CommandStream){0};
    failed=fclose(file)<0; saved=errno;
    status=reap(child);
    if(failed && status>=0) { errno=saved; return -1; }
    return status;
}
