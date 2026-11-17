#ifndef PLAN2001_BUSYBOX_PVD_CLI_H
#define PLAN2001_BUSYBOX_PVD_CLI_H
#include "../unix/pvd.h"
/* Interactive phase-2 builtins, not upstream BusyBox ash or POSIX tools. */
int pvd_cli_run(Pvd *view, const char *line);
#endif
