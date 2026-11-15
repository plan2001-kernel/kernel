/* Native exec integration driver; every command runs in a fresh guest process.
 * The output pipe tests descriptor inheritance and upstream dispatch, never
 * calls applet_main() or ash builtins in the test driver.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include "process.h"
#include "plan2001_full.h"
#include "obj.h"
#include "vobj.h"
#include "sysctl.h"

#include "expected_applets.h"
extern long objopen(unsigned long long, void *, long, unsigned long long, void *);
extern long objinvoke(int, void *, long);
static int report;
static unsigned char covered[sizeof(expected_names)/sizeof(expected_names[0])];
static void emit(const char *s)
{
    ObjCall call = {0};
    call.version = OBJ_ABI_VERSION; call.op = ObjOpWrite;
    call.in = (void *)s; call.inlen = strlen(s);
    (void)objinvoke(report, &call, sizeof call);
}
static void check(int ok, const char *what)
{
    if (!ok) {
        emit("PLAN2001-MULTICALL-FAIL: "); emit(what); emit("\n");
        _exit(99);
    }
}
static void command(char *const args[], int code, const char *expected)
{
    int stream[2], child, status, got;
    char output[4096];
    unsigned used = 0;
    check(pipe(stream) == 0, "create output pipe");
    child = fork();
    check(child >= 0, "fork");
    if (!child) {
        char *environment[] = {"PATH=/bin:/usr/bin", "HOME=/", NULL};
        (void)close(stream[0]);
        if (dup2(stream[1], 1) < 0 || dup2(stream[1], 2) < 0) _exit(121);
        if (stream[1] != 1 && stream[1] != 2) (void)close(stream[1]);
        execve(strchr(args[0], '/') ? args[0] : "/bin/busybox", args, environment);
        _exit(122);
    }
    check(close(stream[1]) == 0, "drop parent writer");
    while ((got = read(stream[0], output + used, sizeof output - 1 - used)) != 0) {
        if (got < 0 && errno == EINTR) continue;
        check(got > 0, "read command output");
        used += (unsigned)got;
        check(used < sizeof output - 1, "bounded command output");
    }
    output[used] = 0;
    check(close(stream[0]) == 0, "close reader");
    do { got = waitpid(child, &status, 0); } while (got < 0 && errno == EINTR);
    if (got != child || !WIFEXITED(status) || WEXITSTATUS(status) != code ||
        (expected && strcmp(output, expected))) {
        emit("command: ");
        for (unsigned i = 0; args[i]; ++i) { emit(args[i]); emit(" "); }
        emit("\noutput: "); emit(output); emit("\n");
        char detail[80];
        snprintf(detail, sizeof detail, "status=%d expected=%d\n", status, code);
        emit(detail);
        check(0, "command status/output mismatch");
    }
    const char *name = strrchr(args[0], '/');
    name = name ? name+1 : args[0];
    if (!strcmp(name,"busybox") && args[1]) name=args[1];
    for(unsigned i=0;i<sizeof(covered);++i)
        if(!strcmp(name,expected_names[i])) covered[i]=1;
    emit("PASS: ");
    for (unsigned i = 0; args[i]; ++i) { emit(args[i]); emit(" "); }
    emit("\n");
}
/* A child can attenuate/revoke only capabilities explicitly delegated to it.
 * Failed exec must leave its image alive and publish no replacement process. */
static void denied_execution(int revoke)
{
    int child=fork(), status, got;
    check(child>=0,"fork capability-denial test");
    if(!child) {
        VobjListRec records[P9_GRANTS];
        SysctlReq query={0};
        int source=0, derived;
        query.version=SYSCTL_ABI_VERSION;
        query.data=records; query.datalen=sizeof records;
        long count=sysctl(SysctlScopeOwnProcess,SysctlHandleList,&query,sizeof query);
        if(count<0 || count>P9_GRANTS) _exit(111);
        for(long i=0;i<count;++i)
            if(records[i].kind==VkindGeVolume) source=(int)records[i].handle;
        if(!source) _exit(112);
        ObjOpenHandleReq request={source,0};
        derived=(int)objopen(ObjOpenHandle,&request,sizeof request,
                            revoke ? Vexecute|Vdelegate : Vdelegate,NULL);
        if(derived<=0 || p9_process_bind_executable("/bin/busybox",derived,
                              UINT64_C(0x4f45584543000005))) _exit(113);
        if(revoke) {
            ObjCall call={0};
            call.version=OBJ_ABI_VERSION; call.op=ObjOpCapRevoke;
            if(objinvoke(source,&call,sizeof call)<0) _exit(114);
        }
        char *args[]={"echo","MUST-NOT-EXECUTE",NULL};
        errno=0;
        if(execve("/bin/echo",args,NULL)!=-1 || errno!=ENOEXEC) _exit(115);
        _exit(0);
    }
    do { got=waitpid(child,&status,0); } while(got<0 && errno==EINTR);
    check(got==child && WIFEXITED(status) && WEXITSTATUS(status)==0,
          revoke ? "revoked executable alias denied" : "non-executable grant denied");
}
#define RUN(code, output, ...) do { char *args[] = {__VA_ARGS__, NULL}; \
    command(args, code, output); } while (0)
int main(void)
{
    static Pvd view;
    static UxService service;
    ObjOpenConsoleReq req = {1, 0};
    int volume;
    report = (int)objopen(ObjOpenConsole, &req, sizeof req, 0, NULL);
    check(report > 0, "authorized diagnostic console");
    volume = (int)objopen(ObjOpenVolume, NULL, 0, Vexecute | Vdelegate, NULL);
    check(volume > 0, "authorized executable volume");

    check(pvd_bootstrap(&view) == 0, "bootstrap view");
    uxservice_init(&service, &view);
    check(p9_full_bind_files(&service) == 0, "bind file service");
    {
        struct stat st, target;
        unsigned count=0;
        const char *path;
        check(stat("/bin/echo",&st)<0 && errno==ENOENT,
              "generated name without registration confers no authority");
        check(p9_process_bind_executable("/bin/busybox", volume,
              UINT64_C(0x4f45584543000005)) == 0, "bind one executable");
        check(stat("/bin/busybox",&target)==0 && S_ISREG(target.st_mode), "executable stat");
        for(unsigned i=0;(path=p9_multicall_path(i))!=NULL;++i) {
            check(stat(path,&st)==0 && lstat(path,&st)==0 &&
                  st.st_ino==target.st_ino && access(path,X_OK)==0,
                  "aliases share executable metadata");
            check(open(path,O_WRONLY|O_CREAT,0600)<0 && errno==EACCES,
                  "immutable executable cannot be replaced");
            ++count;
        }
        check(count>P9_EXECUTABLES, "more aliases than executable registry slots");
        count=0;
        DIR *dir=opendir("/bin");
        check(dir!=NULL,"enumerate executable projection");
        struct dirent *entry;
        while((entry=readdir(dir))!=NULL) if(entry->d_type==DT_REG) ++count;
        check(count>=6 && closedir(dir)==0,"directory contains virtual applets");
        check(stat("/bin/not-configured",&st)<0 && errno==ENOENT,"unknown alias absent");
        emit("PASS: authorized immutable aliases, stat/lstat/access and directory enumeration\n");
    }
    /* Native diagnostic console cannot delegate. Commands inherit pipe fds. */
    (void)close(0); (void)close(1); (void)close(2);
    RUN(0, EXPECTED_APPLETS, "busybox", "--list");
    RUN(0, EXPECTED_APPLETS_FULL, "busybox", "--list-full");
    RUN(0, "hello\n", "busybox", "echo", "hello");
    RUN(0, "alias\n", "/bin/echo", "alias");
    RUN(0, "", "busybox", "true");
    RUN(1, "", "busybox", "false");
    RUN(127, NULL, "busybox", "missing-applet");
    RUN(1, NULL, "busybox", "--install");
    RUN(1, NULL, "busybox", "busybox", "--install");
    RUN(0, "file\n", "busybox", "basename", "/tmp/file");
    RUN(0, "/tmp\n", "busybox", "dirname", "/tmp/file/");
    RUN(0, "/\n", "busybox", "dirname", "/file");
    RUN(0, ".\n", "busybox", "dirname", "file");
    RUN(0, "/\n", "busybox", "pwd");
    RUN(0, "guest:17\n", "busybox", "printf", "%s:%d\n", "guest", "17");
    RUN(0, "", "busybox", "test", "1", "-eq", "1");
    RUN(1, "", "busybox", "test", "1", "-eq", "2");
    RUN(0, "", "[", "yes", "=", "yes", "]");
    RUN(0, "ash-ok\n", "busybox", "ash", "-c", "echo ash-ok");
    RUN(0, "sh-ok\n", "busybox", "sh", "-c", "echo sh-ok");
    RUN(0, "path\nabsolute\n", "/bin/sh", "-c",
        "basename /tmp/path; /bin/busybox echo absolute");
    RUN(0, "external\n", "/bin/sh", "-c", "/bin/echo external");
    RUN(0, "relative\n", "/bin/sh", "-c", "cd /bin; ./echo relative");
    RUN(0, "/tmp\n", "/usr/bin/dirname", "/tmp/file");
    RUN(1, "", "/bin/false");
    RUN(0, "", "/bin/true");
    RUN(0, "file-data\n", "/bin/sh", "-c",
        "/bin/echo file-data > /tmp/native; /bin/cat /tmp/native");
    RUN(0, "match\n", "/bin/sh", "-c", "/bin/echo match | /bin/grep '^ma'");
    RUN(0, "file-data\n", "/bin/grep", "^file-[a-z]*$", "/tmp/native");
    RUN(1, "", "/bin/grep", "absent", "/tmp/native");
    RUN(2, NULL, "/bin/grep", "[", "/tmp/native");
    RUN(0, "file-data\n", "/bin/egrep", "file-(data|other)", "/tmp/native");
    RUN(0, "file-data\n", "/bin/fgrep", "file-data", "/tmp/native");
    RUN(0, "native\n", "/bin/ls", "-1", "/tmp");
    RUN(1, NULL, "/bin/ls", "/missing");
    RUN(0, "", "/bin/sh", "-c", "/bin/ls /bin > /tmp/list; test -s /tmp/list");
    RUN(0, "file-data\n", "/bin/cat", "/tmp/native");
    RUN(1, NULL, "/bin/cat", "/missing");
    for(unsigned i=0;i<sizeof(covered);++i) {
        if(!covered[i]) { emit(expected_names[i]); check(0,"enabled applet lacks guest command test"); }
    }
    denied_execution(0);
    denied_execution(1);
    emit("PASS: attenuated and revoked executable grants denied by native loader\n");
    RUN(0,"parent-still-authorized\n","/bin/echo","parent-still-authorized");
    emit("PLAN2001-MULTICALL-PASS\n");
    for (;;) sleep(60);
}
