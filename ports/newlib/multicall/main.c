/* Native initialization before upstream BusyBox's genuine applet dispatcher.
 * No path/OID confers executable authority here. Installation is separate.
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "../full/process.h"
#include "../full/plan2001_full.h"
extern int p9_busybox_main(int, char **);

int main(int argc, char **argv)
{
    static Pvd view;
    static UxService service;
    if (!p9_full_file_service()) {
        if (pvd_bootstrap(&view)) return 125;
        uxservice_init(&service, &view);
        if (p9_full_bind_files(&service)) return 125;
    }
    if (setenv("HOME", "/", 0) ||
        setenv("PATH", "/bin:/usr/bin:/sbin:/usr/sbin", 0) ||
        setenv("PS1", "ash$ ", 0)) return 125;
    if (argc == 2 && !strcmp(argv[0], "unix-child")) {
        char *end;
        uint64_t oid;
        int volume = p9_full_executable_volume();
        errno = 0;
        oid = strtoull(argv[1], &end, 16);
        /* The OID selects content within an explicitly delegated Vexecute
         * volume. It never opens a volume or grants rights by itself. */
        if (!volume || errno || !oid || !*argv[1] || *end ||
            p9_process_bind_executable("/bin/busybox", volume, oid)) return 125;
        if (dup2(1, 2) < 0) return 125;
        char *shell[] = {"sh", "-i", NULL};
        puts("PLAN2001-MULTICALL-READY (volatile files; external applets)");
        fflush(stdout);
        return p9_busybox_main(2, shell);
    }
    return p9_busybox_main(argc, argv);
}
