/* Minimal Plan2001 multicall dispatcher for a pinned subset of unmodified
 * BusyBox 1.36.1 applets. No Linux appletlib or Linux CRT is linked.
 * General BusyBox / POSIX support is explicitly out of scope.
 */
#include "libbb.h"
#ifdef PLAN2001_BUSYBOX_F5
#include "../unix/init.h"
#include "f5_shell.h"
#include "../unix/supervisor.h"
#include "../unix/uxrpc_child.h"
#include "vobj.h"
#include "obj.h"
#endif

extern int true_main(int argc, char **argv);
extern int false_main(int argc, char **argv);
extern int echo_main(int argc, char **argv);
extern int p2001_ls_main(int argc, char **argv);
extern int p2001_cat_main(int argc, char **argv);
extern int p2001_uxpipe_send_main(int argc, char **argv);
extern int p2001_uxpipe_recv_main(int argc, char **argv);
extern int strcmp(const char *a, const char *b);
extern long sleep(long millis);
extern void exits(char *);

static const char *last_component(const char *name)
{
    const char *part = name;
    while (*name) {
        if (*name++ == '/')
            part = name;
    }
    return part;
}

int plan2001_busybox_dispatch(int argc, char **argv)
{
    const char *name;
    if (argc < 1 || !argv || !argv[0])
        return 64;
    name = last_component(argv[0]);
    if (strcmp(name, "busybox") == 0) {
        if (argc < 2 || !argv[1])
            return 64;
        argc--;
        argv++;
        name = last_component(argv[0]);
    }
    if (strcmp(name, "true") == 0)
        return true_main(argc, argv);
    if (strcmp(name, "false") == 0)
        return false_main(argc, argv);
    if (strcmp(name, "echo") == 0)
        return echo_main(argc, argv);
    if (strcmp(name, "ls") == 0)
        return p2001_ls_main(argc, argv);
    if (strcmp(name, "cat") == 0)
        return p2001_cat_main(argc, argv);
    if (strcmp(name, "uxpipe-send") == 0)
        return p2001_uxpipe_send_main(argc, argv);
    if (strcmp(name, "uxpipe-recv") == 0)
        return p2001_uxpipe_recv_main(argc, argv);
    return 127;
}

#ifdef PLAN2001_BUSYBOX_F5
/* The kernel boot manifest starts this native ELF directly in slot 4/F5.
 * This main is the unix-init entry, not a standalone ash/exec launcher.
 * The first shell is a linked same-process callback until SPAWN/FD work.
 */
int main(int argc, char **argv)
{
    /* The same audited static ELF is both the supervised Unix-init child
     * and a restricted BusyBox multicall executable. For an explicit
     * applet request do NOT require F5 console grants: the child gets its
     * own attenuated RO listing and RW stdout regions from its launcher.
     */
    if (argc >= 1 && argv && argv[0] &&
        strcmp(argv[0],"uxrpc-client")==0)
        return p2001_uxrpc_child_main(argc,argv);
    if (argc >= 2 && argv && argv[0] &&
        strcmp(argv[0],"busybox")==0)
    {
        int applet_status=plan2001_busybox_dispatch(argc,argv);
        /* Legacy ls uses two region *frames* in the inverse grant
         * direction. Do not probe it as UXPIPE, or contaminate its exit. */
        if(strcmp(argv[1],"echo")==0 || strcmp(argv[1],"cat")==0)
            p2001_session_finish();
        return applet_status;
    }
    static Pvd view;
    UxF5Storage storage = {0};
    UxF5Storage *context = 0;
    unsigned long long oid=0;
    VobjListRec grants[16];
    long n;
    int exec_count=0, storage_count=0, status;
    unsigned int i;

    if (p2001_session_probe() != 1)
        exits("unix-init requires explicitly delegated console streams");

    if (argc >= 1 && argv && argv[0] &&
        strcmp(argv[0],"unix-child")==0) {
        if ((argc!=2 && argc!=3) ||
            uxsupervisor_parse_oid(argv[1],&oid)!=0)
            exits("unix-child: invalid executable selector");
        storage.executable_oid=oid;
        if (argc==3 &&
            (uxsupervisor_parse_oid(argv[2],&storage.manifest)!=0 ||
             storage.manifest==oid))
            exits("unix-child: invalid manifest selector");
        n=obj_list(grants,16);
        if (n<0 || n>16) exits("unix-child: missing inherited grants");
        for(i=0;i<(unsigned int)n;++i) {
            if(grants[i].kind!=VkindGeVolume) continue;
            if((grants[i].rights&~Vdelegate)==Vexecute) {
                ++exec_count;
                storage.executable_volume=(int)grants[i].handle;
            }else if(grants[i].rights==
                     (Vread|Vwrite|Vcreate|Vcommit|Vmeta)) {
                ++storage_count;
                storage.volume=(int)grants[i].handle;
            }else exits("unix-child: excessive GE/O grant");
        }
        if(exec_count!=1 || (storage.manifest ? storage_count!=1 :
                             storage_count!=0))
            exits("unix-child: incomplete GE/O grant contract");
        storage.restore=storage.manifest!=0;
        context=&storage;
    }
    status = p2001_unix_init(&view, p2001_f5_shell, context);
    if (status < 0)
        exits("unix-init or shell failed");
    exits("unix-init shell exited");
    return 1;
}
#elif defined(PLAN2001_BUSYBOX_GUEST_SMOKE)
/* Dedicated *guest* fixture. It calls the actual linked upstream applets
 * and proves stdout and return values. Kernel boot-owner cannot exit yet,
 * so retain it alive only AFTER the success marker was emitted.
 */
static void fail_guest(void)
{
    static const char msg[] = "PLAN2001-BUSYBOX-GUEST-FAIL\n";
    (void)full_write(1, msg, sizeof(msg) - 1);
    exits("busybox smoke failed");
    for (;;)
        (void)sleep(1000);
}

int main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
    char *echo_args[] = {
        "busybox", "echo", "PLAN2001-BUSYBOX-ECHO-HELLO", 0
    };
    char *echo_alias[] = {
        "/bin/echo", "PLAN2001-BUSYBOX-ALIAS-OK", 0
    };
    char *true_args[] = { "busybox", "true", 0 };
    char *false_args[] = { "busybox", "false", 0 };
    char *unknown_args[] = { "busybox", "unknown", 0 };
    static const char passed[] = "PLAN2001-BUSYBOX-GUEST-PASS\n";

    if (plan2001_busybox_dispatch(3, echo_args) != 0)
        fail_guest();
    if (plan2001_busybox_dispatch(2, echo_alias) != 0)
        fail_guest();
    if (plan2001_busybox_dispatch(2, true_args) != 0)
        fail_guest();
    if (plan2001_busybox_dispatch(2, false_args) != 1)
        fail_guest();
    if (plan2001_busybox_dispatch(2, unknown_args) != 127)
        fail_guest();
    if (full_write(1, passed, sizeof(passed) - 1)
            != sizeof(passed) - 1)
        fail_guest();
    for (;;)
        (void)sleep(1000);
}
#else
int main(int argc, char **argv)
{
    return plan2001_busybox_dispatch(argc, argv);
}
#endif
