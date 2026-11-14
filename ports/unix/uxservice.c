/* One authoritative UXFS/PVD instance shared by the shell evaluator and
 * in-process Newlib FD tables and the separate uxrpc child processes.
 * Every durable mutation uses the
 * checked GE/O manifest + commit; no raw OID ever grants authority.
 */
#include "uxservice.h"

void uxservice_init(UxService *s, Pvd *view)
{
    if (!s) return;
    s->manifest=0;
    s->persistent=0;
    s->store.context=0;
    s->store.create=0;
    s->store.put=0;
    s->store.get=0;
    s->store.commit=0;
    uxfs_init(&s->fs, view);
}
int uxservice_bind(UxService *s, const UxStore *store, UxOid manifest,
                   int restore)
{
    if (!s || !s->fs.view || !store || !store->context ||
        !store->create || !store->put || !store->get || !store->commit ||
        !manifest || s->persistent)
        return PVD_EINVAL;
    if (restore && uxfs_restore(&s->fs, store, manifest) != PVD_OK)
        return PVD_EIO;
    s->store=*store;
    s->manifest=manifest;
    s->persistent=1;
    return PVD_OK;
}
int uxservice_sync(UxService *s)
{
    if (!s) return PVD_EINVAL;
    if (!s->persistent || !s->fs.dirty) return PVD_OK;
    return uxfs_save(&s->fs, &s->store, s->manifest);
}
int uxservice_command(UxService *s, const char *command, const UxShellIO *io)
{
    int rc;
    if (!s) return PVD_EINVAL;
    rc=uxshell_eval(&s->fs,command,io);
    if (rc != PVD_OK) return rc;
    return uxservice_sync(s);
}
