/* Uses kernel-native SPAWN and Process Handle completion, not Linux exec.
 * Streams must be capability regions created/mapped by the caller; the
 * child maps and validates inherited handles, including EOF itself.
 */
#include "uxspawn.h"
#include "obj.h"
#include "vobj.h"
/* Packed native Ospawn followed by Oinheritv (flags=2), all u64.
 * Kernel ABI layout is specified in runtime/abi/sys.h.
 */
struct SpawnGrants {
    unsigned long long cpu, membudget, flags;
    unsigned long long nhandles;
    struct { unsigned long long handle, rights; } grant[8];
};
int uxspawn_start(const UxLaunch *launch, char **argv, int argc)
{
    struct SpawnGrants opts = {0};
    unsigned int i;
    if (!launch || launch->executable_volume <= 0 || !launch->oid ||
        !argv || argc <= 0 || launch->grant_count > 8)
        return -1;
    opts.cpu = ~0ULL;
    opts.membudget = 0;
    opts.flags = 2;             /* native Oinheritv */
    opts.nhandles = launch->grant_count;
    for (i = 0; i < launch->grant_count; ++i) {
        UxRights rights = launch->grant[i].rights & ~(UxRights)Vdelegate;
        UxRights stream = Vread|Vwrite|Vmeta;
        UxRights storage = Vread|Vwrite|Vcreate|Vcommit|Vmeta;
        UxRights execution = Vexecute|Vmeta;
        if (launch->grant[i].handle <= 0 || !rights ||
            (rights & Vcontrol) != 0)
            return -1;
        /* Attenuated stream, complete storage transaction or executable
         * authority. The kernel independently checks the source Handle's
         * actual provider kind, rights and Vdelegate on every grant. */
        if (!((rights & ~stream) == 0 &&
              (rights & (Vread|Vwrite)) != 0) &&
            rights != storage &&
            !(rights == Vexecute || rights == execution))
            return -1;
        opts.grant[i].handle = (unsigned long long)launch->grant[i].handle;
        opts.grant[i].rights = launch->grant[i].rights;
    }
    return (int)obj_ospawn(launch->executable_volume, launch->oid,
                           argv, argc, (struct Ospawn *)&opts);
}
int uxspawn_wait(int process, long long timeout, int *exited_ok)
{
    ObjProcessInfo state = {0};
    long result;
    if (process <= 0 || !exited_ok) return -1;
    result = obj_process_wait(process, &state, timeout);
    if (result < 0) return -1;
    if (state.state != ObjProcCompleted) return 1;
    *exited_ok = state.cause == ObjProcCauseExit && state.msglen == 0;
    return 0;
}
