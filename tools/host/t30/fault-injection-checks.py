"""Deterministic prepublication rollback checks (issue #34).

This module runs only on a PLAN2001_LIFECYCLE_DIAG kernel.  The kernel
consumes the test-only PLAN2001_FAULT environment value at named safe
points, after temporary resources have been allocated but before the caller
observes a committed result.  The canonical kernel compiles the hooks out.

SPAWN has one publication boundary: after all construction, Handle-plan
commit and Pgrp COPY/SHARE processing, the Proc is linked to its parent and
becomes waitable.  Every injected failure here is before that boundary, so
the caller must see no child and must not reap an abort record.

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

    def nochild(name):
        try:
            space.wait()
        except OSError:
            ok(True, name)
            return
        raise AssertionError(name)

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
    p.obj_close(h)
    mark('base')

    # --- Non-RFPROC RFNAMEG and RFCNAMEG: fail after the new Pgrp exists --
    def switch_rollback(flag, name):
        pid = p.rfork(p.RFPROC | p.RFENVG)
        if pid == 0:
            try:
                control0 = p.obj_open_namespace_current()
                probe = space.view(('fault_keep',))
                p.envset('PLAN2001_FAULT', b'fork-current-rollback')
                try:
                    p.rfork(flag)
                except OSError:
                    pass
                else:
                    p.exits('switch-not-taken-' + name)
                # The original Pgrp and its NamespaceControl remain usable.
                token = p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_ATTACH,
                                     probe, space.MAFTER)
                p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_DETACH, token)
                if not openable('fault_keep'):
                    p.exits('switch-old-role-' + name)
                # The token was consumed: a later valid switch succeeds and
                # invalidates the old NamespaceControl.
                if p.rfork(flag) != 0:
                    p.exits('switch-valid-' + name)
                try:
                    p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_ATTACH,
                                 probe, space.MAFTER)
                except OSError:
                    pass
                else:
                    p.exits('switch-old-control-' + name)
                p.obj_close(probe)
                p.obj_close(control0)
            except OSError:
                p.exits('switch-os-' + name + ':' + p.errstr())
            p.exits('')
        waited, status = p.wait()
        ok(waited == pid and status == '',
           'injected %s failure rolls the caller back to its original Pgrp '
           'status=%r' % (name, status))
        ok(openable('fault_keep'),
           '%s rollback keeps the original role and NamespaceControl usable'
           % (name,))

    switch_rollback(p.RFNAMEG, 'RFNAMEG')
    mark('fork-nam')
    switch_rollback(p.RFCNAMEG, 'RFCNAMEG')
    mark('fork-empty')

    # --- SPAWN failure at the last prepublication point, with SHARE --------
    # ocontextspawn() has already transferred the parent's Pgrp reference.
    shholds = p.obj_info(sh)[4]
    p.envset('PLAN2001_FAULT', b'spawn-child-late-abort')
    denied(lambda: space.spawn(OID, handle=sh,
                               argv=('micropython', '-c', 'pass'),
                               view=2, roles=('fault_keep',)),
           'injected SPAWN failure before publication')
    nochild('failed SPAWN leaves no waitable child or abort record')
    ok(p.obj_info(sh)[4] == shholds,
       'failed SHARE SPAWN releases the transferred Space reference')
    ok(openable('fault_keep'),
       'failed SHARE SPAWN leaves the parent Pgrp role intact')
    h = p.obj_open_binding('fault_keep')
    p.obj_bind(h, 'fault_after', p.VREAD | p.VMETA | p.VDELEGATE)
    p.obj_close(h)
    ok(openable('fault_after'),
       'failed SHARE SPAWN leaves the parent Pgrp mutable')
    p.obj_bind(0, 'fault_after', 0, p.OBJ_BIND_REMOVE)
    # A successful SPAWN after the failure still produces one wait record.
    pid = space.spawn(OID, handle=sh, argv=('micropython', '-c', 'pass'))
    waited, status = space.wait()
    ok(waited == pid and status == '',
       'ordinary SPAWN after a prepublication failure reaps exactly once '
       'status=%r' % (status,))
    nochild('successful SPAWN leaves no extra wait record')
    mark('spawn-share-abort')

    # --- Delegated Space ownership boundaries ------------------------------
    # Each case fails with the source Handle plan intact and no child.
    geh = p.obj_open_ge_space(sh, base_oid)
    gehholds = p.obj_info(geh)[4]
    gehrights = p.obj_info(geh)[10]
    shholds = p.obj_info(sh)[4]
    shrights = p.obj_info(sh)[10]

    def failed_forkex(token, plan, name):
        p.envset('PLAN2001_FAULT', token)
        denied(lambda: p.rforkex(p.RFPROC, plan),
               'injected ' + name)
        nochild('failed ' + name + ' leaves no waitable child')
        ok(p.obj_info(geh)[4] == gehholds and
           p.obj_info(sh)[4] == shholds and
           p.obj_info(geh)[10] == gehrights and
           p.obj_info(sh)[10] == shrights,
           name + ' releases every partial Ohand/Vobj/Vhand reference')

    failed_forkex(b'inherit-space-ohand',
                  ((sh, 8192 | p.VMETA, 1),), 'delegated Ohand failure')
    mark('inherit-ohand')
    failed_forkex(b'inherit-space-vobj',
                  ((geh, p.VREAD | p.VMETA | p.VDELEGATE, 1),
                   (sh, 8192 | p.VMETA, 2)),
                  'delegated Vobj failure after an earlier entry installed')
    mark('inherit-multi')
    failed_forkex(b'inherit-space-vhand',
                  ((sh, 8192 | p.VMETA, 1),), 'delegated Vhand failure')
    mark('inherit-vhand')

    # The fault token is consumed; the same RFORKEX now succeeds.
    pid, _ch = p.rforkex(p.RFPROC, ((sh, 8192 | p.VMETA, 1),))
    if pid == 0:
        p.exits('')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'a valid RFORKEX after the injected failures still publishes one child '
       'status=%r' % (status,))
    nochild('valid RFORKEX leaves no extra wait record')

    # SPAWN uses the same commit path; fail after its earlier GE entry was
    # installed, before the child Vhand for the delegated Space exists.
    p.envset('PLAN2001_FAULT', b'inherit-space-vhand')
    denied(lambda: space.spawn(
               OID, handle=sh, argv=('micropython', '-c', 'pass'),
               vhandles=((geh, p.VREAD | p.VMETA | p.VDELEGATE),
                         (sh, 8192 | p.VMETA))),
           'injected SPAWN Handle-plan failure')
    nochild('failed SPAWN Handle plan leaves no waitable child')
    ok(p.obj_info(geh)[4] == gehholds and
       p.obj_info(sh)[4] == shholds and
       p.obj_info(geh)[10] == gehrights and
       p.obj_info(sh)[10] == shrights,
       'failed SPAWN Handle plan releases every partial inherited reference')
    mark('spawn-inherit')

    p.obj_close(geh)
    p.obj_bind(0, 'fault_keep', 0, p.OBJ_BIND_REMOVE)
    p.obj_close(sh)
    mark('end')
    print('T30-FINISH', n[0], 'PASS')
    return 1
