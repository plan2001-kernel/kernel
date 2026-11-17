/* First Unix init: a process-local bootstrap and explicit shell handoff.
 * This intentionally does not create a privileged ObjectSpace root, forge
 * GE/O capabilities, spawn a child process, or manage kernel consoles.
 * The caller must already own the native session stdin/stdout grants.
 */
#include "init.h"

int p2001_unix_init(Pvd *view, P2001UnixShellEntry shell, void *context)
{
    char cwd[2];
    int status;

    if (!view || !shell)
        return PVD_EINVAL;
    status = pvd_bootstrap(view);
    if (status != PVD_OK)
        return status;
    status = pvd_getcwd(view, cwd, sizeof(cwd));
    if (status != PVD_OK || cwd[0] != '/' || cwd[1] != 0)
        return PVD_EIO;
    /* The first shell receives exactly this initialized process-local view.
     * Fork/exec inheritance and separate shell processes come later.
     */
    return shell(view, context);
}
