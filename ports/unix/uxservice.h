/* Single-owner trusted Unix file service. No ambient GE/O authority:
 * durable operations are enabled only with a caller-provided UxStore whose
 * context holds an authorized native volume capability. A UxService is
 * process-local, but ports/unix/uxrpc.[ch] exposes its single authoritative
 * instance to untrusted processes via bounded capability-only UXPIPE RPC.
 */
#ifndef PLAN2001_UXSERVICE_H
#define PLAN2001_UXSERVICE_H
#include "uxshell.h"
typedef struct {
    UxFs fs;
    UxStore store;
    UxOid manifest;
    int persistent;
} UxService;
void uxservice_init(UxService *s, Pvd *view);
int uxservice_bind(UxService *s, const UxStore *store, UxOid manifest,
                   int restore);
int uxservice_sync(UxService *s);
int uxservice_command(UxService *s, const char *command, const UxShellIO *io);
#endif
