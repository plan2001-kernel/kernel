/* Native userland unix-init, before a POSIX exec/descriptor layer exists.
 * Boot manifest starts this executable directly. Init constructs a private
 * PVD and passes it to the initial same-process shell entry point.
 */
#ifndef PLAN2001_UNIX_INIT_H
#define PLAN2001_UNIX_INIT_H
#include "pvd.h"

typedef int (*P2001UnixShellEntry)(Pvd *view, void *context);
int p2001_unix_init(Pvd *view, P2001UnixShellEntry shell, void *context);
#endif
