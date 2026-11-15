/* Same userland init/PVD code as the native Plan2001 F5 ELF.
 * Mocks only the future shell entry and its already-tested console writer.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "init.h"
#include "pvd_cli.h"
#include "libbb.h"

static unsigned int invoked;
static char written[2048];
static size_t position;

ssize_t full_write(int fd, const void *buffer, size_t count)
{
    assert(fd == 1 && position + count + 1 < sizeof(written));
    memcpy(written + position, buffer, count);
    position += count;
    written[position] = 0;
    return (ssize_t)count;
}

static int first_shell(Pvd *view, void *context)
{
    char current[PVD_PATH_MAX];
    assert(context == &invoked);
    ++invoked;
    assert(view->count == 9);
    assert(pvd_getcwd(view, current, sizeof(current)) == PVD_OK);
    assert(strcmp(current, "/") == 0);
    position = 0;
    written[0] = 0;
    assert(pvd_cli_run(view, "pwd") == 0);
    assert(strcmp(written, "/\n") == 0);
    position = 0;
    written[0] = 0;
    assert(pvd_cli_run(view, "ls /usr") == 0);
    assert(strcmp(written, "bin\n") == 0);
    assert(pvd_cli_run(view, "cd /usr/bin") == 0);
    assert(pvd_getcwd(view, current, sizeof(current)) == PVD_OK);
    assert(strcmp(current, "/usr/bin") == 0);
    return 73;
}

int main(void)
{
    Pvd view;
    char current[PVD_PATH_MAX];

    assert(p2001_unix_init(0, first_shell, &invoked) == PVD_EINVAL);
    assert(p2001_unix_init(&view, 0, &invoked) == PVD_EINVAL);
    /* Boot one enters root, shell changes cwd to a nested directory. */
    assert(p2001_unix_init(&view, first_shell, &invoked) == 73);
    assert(invoked == 1);
    assert(pvd_getcwd(&view, current, sizeof(current)) == PVD_OK);
    assert(strcmp(current, "/usr/bin") == 0);
    /* Second boot must rebuild the PVD; no old cwd survives. */
    assert(p2001_unix_init(&view, first_shell, &invoked) == 73);
    assert(invoked == 2);
    assert(view.count == 9);
    puts("PASS: unix-init bootstraps root before shell handoff, twice");
    return 0;
}
