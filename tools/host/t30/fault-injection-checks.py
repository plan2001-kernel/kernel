"""Deterministic post-allocation rollback checks (issue #34).

This module runs only on a PLAN2001_LIFECYCLE_DIAG kernel.  The kernel
consumes the test-only PLAN2001_FAULT environment value at one named safe
point, after temporary resources have been allocated but before the caller
observes a committed result.  The canonical kernel compiles the hook out, so
these failure paths are never reachable in production builds.

Every assertion prints only after it passes; the runner requires a strictly
sequential T30-FINISH.
"""

BASE = b'T30F-BASE'
OID = 0x4f45584543000003


class Counter:
    def __init__(self):
        self.n = 0


shared = Counter()
alias = shared


def init():
    shared.n = 0


def run():
    import plan9 as p
    import space

    n = [0]

    def ok(value, name):
        assert value, name
        n[0] += 1
        print('T30-PASS', n[0], name)

    def mark(name):
        print('T30-LIFE', name)

    def denied(call, name):
        try:
            call()
        except OSError:
            return
        raise AssertionError('expected rejection: ' + name)

    def openable(role):
        try:
            h = p.obj_open_binding(role)
        except OSError:
            return False
        p.obj_close(h)
        return True

    sh = p.obj_open_space('init')
    gh, base_oid = p.obj_create_ge(sh, BASE)
    p.obj_commit(sh)
    p.obj_close(gh)
    h = p.obj_open_ge_space(sh, base_oid)
    p.obj_bind(h, 'fault_keep', p.VREAD | p.VMETA | p.VDELEGATE)
    p.obj_bind(h, 'fault_probe', p.VREAD | p.VMETA | p.VDELEGATE)
    p.obj_close(h)
    mark('base')

    # --- Non-RFPROC group switch: error after the new Pgrp/OContext exists
    # The test child gets a private environment copy so the fault token never
    # touches another process, while its Pgrp exercises the real switch.
    pid = p.rfork(p.RFPROC | p.RFENVG)
    if pid == 0:
        try:
            control0 = p.obj_open_namespace_current()
            probe = space.view(('fault_probe',))
            p.envset('PLAN2001_FAULT', b'fork-current-rollback')
            try:
                p.rfork(p.RFNAMEG)
            except OSError:
                pass
            else:
                p.exits('fault-not-taken')
            # The rejected switch must leave the original Pgrp current and
            # fully usable, not merely replace it with an equal copy.
            if not openable('fault_keep'):
                p.exits('fault-old-role')
            token = p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_ATTACH,
                                 probe, space.MREPL)
            p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_DETACH, token)
            # A later legitimate switch still works and invalidates the old
            # NamespaceControl, proving the rollback left no half-commit.
            if p.rfork(p.RFNAMEG) != 0:
                p.exits('fault-switch')
            try:
                p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_ATTACH,
                             probe, space.MREPL)
            except OSError:
                pass
            else:
                p.exits('fault-old-control')
            p.obj_close(probe)
            p.obj_close(control0)
        except OSError:
            p.exits('fault-child-os:' + p.errstr())
        p.exits('')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'injected failure after current-group allocation rolls the caller '
       'back to its original Pgrp status=%r' % (status,))
    ok(openable('fault_keep'),
       'the rolled-back Pgrp still authorizes its unchanged role')
    mark('current-rollback')

    # --- SPAWN: abort after the child is fully built and linked -----------
    # Run in the boot owner so the aborted child has a real parent and the
    # single wait record is reaped deterministically, not by a sleep.
    plan = p.obj_open_binding('fault_keep')
    planholds = p.obj_info(plan)[4]
    shholds = p.obj_info(sh)[4]
    p.envset('PLAN2001_FAULT', b'spawn-child-late-abort')
    denied(lambda: space.spawn(
               OID, handle=sh, argv=('micropython', '-c', 'pass'),
               vhandles=((plan, p.VREAD | p.VMETA),)),
           'late SPAWN child abort')
    abortpid, abortstatus = space.wait()
    ok(abortpid > 0 and 'fork aborted' in abortstatus,
       'the late-aborted SPAWN is reaped exactly once with the abort status '
       '(%r)' % (abortstatus,))
    ok(p.obj_info(plan)[4] == planholds and p.obj_info(sh)[4] == shholds,
       'the late abort releases every committed inherited Handle reference')
    ok(openable('fault_keep'),
       'the late abort leaves the parent Pgrp and its role intact')
    denied(lambda: p.wait(),
           'the late abort leaves no extra wait record')
    p.obj_close(plan)
    mark('child-abort')

    p.obj_bind(0, 'fault_keep', 0, p.OBJ_BIND_REMOVE)
    try:
        p.obj_bind(0, 'fault_probe', 0, p.OBJ_BIND_REMOVE)
    except OSError:
        pass
    p.obj_close(sh)
    mark('end')
    print('T30-FINISH', n[0], 'PASS')
    return 1
