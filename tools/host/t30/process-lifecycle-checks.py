"""Executable Pgrp/OContext lifecycle regression (issue #30).

The seeded init runs once on a fresh O image.  Every assertion prints only
after it passes; the runner requires a strictly sequential T30-FINISH.

All namespace mutation happens in the boot owner.  Forked or spawned children
receive explicitly delegated Handles; they never open an O Space by name,
because bootstrap Space authority is intentionally limited to the boot owner.
"""

BASE = b'T30-BASE'
LATER = b'T30-LATER'
CHILD_ADD = b'T30-CHILD-ADD'
ISO = b'T30-ISO'
ISO_CHILD = b'T30-ISO-CHILD'
NESTED = b'T30-NESTED'
REVOKE = b'T30-REVOKE'
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

    def denied(call, name):
        try:
            call()
        except OSError:
            return
        raise AssertionError('expected rejection: ' + name)

    def find_slot(slot, kind=p.VKIND_PIPE):
        for h in p.obj_list():
            if (h & 0xff) != slot:
                continue
            try:
                i = p.obj_info(h)
            except OSError:
                continue
            if i[8] == kind:
                return h
        return 0

    def visible(oid):
        for record in space.visible():
            if record[2] == oid and record[3] & space.OREAD:
                return True
        return False

    def openable(role):
        try:
            h = p.obj_open_binding(role)
        except OSError:
            return False
        p.obj_close(h)
        return True

    def read_role(role, size):
        h = p.obj_open_binding(role)
        try:
            return p.obj_read(h, size)
        finally:
            p.obj_close(h)

    def read_all(h):
        out = b''
        while True:
            part = p.piperead(h, 128)
            if not part:
                return out
            out += part

    sh = p.obj_open_space('init')

    def make_ge(data):
        h, oid = p.obj_create_ge(sh, data)
        p.obj_commit(sh)
        p.obj_close(h)
        return oid

    def geh(oid):
        return p.obj_open_ge_space(sh, oid)

    def bind_role(oid, role, rights=None):
        if rights is None:
            rights = p.VREAD | p.VMETA | p.VDELEGATE
        h = geh(oid)
        p.obj_bind(h, role, rights)
        p.obj_close(h)

    def unbind(role):
        p.obj_bind(0, role, 0, p.OBJ_BIND_REMOVE)

    base_oid = make_ge(BASE)
    later_oid = make_ge(LATER)
    later_gh = geh(later_oid)

    # --- RFPROC shares the exact Pgrp/OContext identity ---------------------
    # A NamespaceControl Handle is bound to one Pgrp pointer.  Detach is a
    # no-output control operation, so an inherited control Handle can prove
    # identity without a user buffer or an extra Handle inheritance.
    bind_role(base_oid, 'life')
    probe_oid = make_ge(b'T30-PROBE')
    bind_role(probe_oid, 'probe')
    probe = space.view(('probe',))
    control = p.obj_open_namespace_current()

    token = space.mount(probe, space.MREPL)
    pid, ch = p.rforkex(p.RFPROC, ((control, p.VCONTROL | p.VMETA, 1),))
    if pid == 0:
        h = find_slot(1, p.VKIND_NAMESPACE_CONTROL)
        if not h:
            p.exits('t30-noslot')
        try:
            n = p.obj_invoke(h, p.OBJ_OP_NAMESPACE_DETACH, token)
        except OSError:
            p.exits('t30-detach:' + p.errstr())
        p.exits('' if n == 0 else 't30-detach-result')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'RFPROC shares the exact Pgrp (inherited NamespaceControl operates) '
       'status=%r' % (status,))
    ok(openable('life'),
       'the shared child detach leaves the unrelated parent role intact')

    bind_role(probe_oid, 'probe')
    token = space.mount(probe, space.MREPL)
    pid, ch = p.rforkex(p.RFPROC | p.RFNAMEG,
                        ((control, p.VCONTROL | p.VMETA, 1),))
    if pid == 0:
        h = find_slot(1, p.VKIND_NAMESPACE_CONTROL)
        good = False
        if h:
            try:
                p.obj_invoke(h, p.OBJ_OP_NAMESPACE_DETACH, token)
            except OSError:
                good = True
        p.exits('' if good else 't30-copied-control')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'RFNAMEG copies the Pgrp (parent NamespaceControl rejected) '
       'status=%r' % (status,))
    space.unmount(token)
    p.obj_close(probe)
    p.obj_close(control)

    # --- Shared mutation: child adds, then observes a later parent role -----
    child_oid = make_ge(CHILD_ADD)
    child_gh = geh(child_oid)
    a, w = p.pipenew()
    pid, ch = p.rforkex(p.RFPROC,
                        ((a, p.VREAD | p.VMETA, 1),
                         (child_gh, p.VREAD | p.VMETA | p.VDELEGATE, 2)))
    if pid == 0:
        cr = find_slot(1, p.VKIND_PIPE)
        cg = find_slot(2, p.VKIND_GE)
        if not (cr and cg):
            p.exits('t30-share-handles')
        try:
            life = p.obj_open_binding('life')
            p.obj_close(life)
        except OSError:
            p.exits('t30-share-life-open')
        if not visible(base_oid):
            p.exits('t30-share-life-visible')
        p.obj_bind(cg, 'from_child',
                   p.VREAD | p.VMETA | p.VDELEGATE)
        if p.piperead(cr, 3) != b'go\n':
            p.exits('t30-share-signal')
        if not visible(later_oid):
            p.exits('t30-share-later')
        p.exits('')
    p.obj_bind(later_gh, 'laterrole',
               p.VREAD | p.VMETA | p.VDELEGATE)
    p.pipewrite(w, b'go\n')
    p.pipeclose(w)
    waited, status = p.wait()
    p.pipeclose(a)
    ok(waited == pid and status == '',
       'RFPROC child observes a later parent role mutation status=%r' % (status,))
    ok(read_role('from_child', len(CHILD_ADD)) == CHILD_ADD,
       'RFPROC child role mutation is visible to the parent')
    p.obj_close(child_gh)

    # --- RFNAMEG: initial roles equivalent, later edits isolated -----------
    iso_oid = make_ge(ISO)
    iso_child_oid = make_ge(ISO_CHILD)
    bind_role(iso_oid, 'iso')
    iso_gh = geh(iso_child_oid)
    pid, ch = p.rforkex(p.RFPROC | p.RFNAMEG,
                        ((iso_gh, p.VREAD | p.VMETA | p.VDELEGATE, 1),))
    if pid == 0:
        cg = find_slot(1, p.VKIND_GE)
        good = bool(cg and visible(iso_oid) and not openable('iso_parent_only'))
        if good:
            p.obj_bind(cg, 'iso_child',
                       p.VREAD | p.VMETA | p.VDELEGATE)
            p.obj_bind(0, 'iso', 0, p.OBJ_BIND_REMOVE)
            good = not openable('iso')
        p.exits('' if good else 't30-iso')
    p.obj_bind(0, 'iso', 0, p.OBJ_BIND_REMOVE)
    p.obj_bind(later_gh, 'iso_parent_only',
               p.VREAD | p.VMETA | p.VDELEGATE)
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'RFNAMEG child starts with equivalent authorized role bindings')
    ok(openable('iso_parent_only') and not openable('iso_child'),
       'RFNAMEG parent and child later edits are isolated')
    p.obj_close(iso_gh)
    unbind('iso_parent_only')
    denied(lambda: p.rforkex(p.RFPROC | p.RFNAMEG, ((0, p.VREAD),)),
           'RFNAMEG with an invalid Handle plan')
    ok(visible(base_oid) and openable('life'),
       'failed RFNAMEG plan leaves the parent Pgrp and roles unchanged')

    # --- RFCNAMEG: no inherited roles, no ambient rights --------------------
    bind_role(later_oid, 'empty_seed')
    pid = p.rfork(p.RFPROC | p.RFCNAMEG)
    if pid == 0:
        good = (len(space.visible()) == 0 and
                not openable('empty_seed') and
                not openable('life'))
        p.exits('' if good else 't30-empty')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'RFCNAMEG child starts with an empty role view')
    ok(visible(later_oid),
       'RFCNAMEG does not remove the parent role bindings')
    unbind('empty_seed')

    # --- RFNAMEG/RFCNAMEG without RFPROC: current Pgrp pointer only --------
    bind_role(base_oid, 'rollback')
    control0 = p.obj_open_namespace_current()
    probe = space.view(('rollback',))
    denied(lambda: p.rfork(p.RFNAMEG | p.RFCNAMEG),
           'RFNAMEG together with RFCNAMEG')
    denied(lambda: p.rfork(p.RFNOWAIT),
           'RFNOWAIT without RFPROC')
    ok(visible(base_oid) and openable('rollback'),
       'invalid non-RFPROC rfork preserves the prior namespace')

    pid = p.rfork(p.RFNAMEG)
    ok(pid == 0, 'RFNAMEG without RFPROC returns zero in the caller')
    denied(lambda: p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_ATTACH,
                                probe, space.MREPL),
           'old Pgrp NamespaceControl after RFNAMEG')
    control1 = p.obj_open_namespace_current()
    ok(visible(base_oid) and openable('rollback'),
       'RFNAMEG without RFPROC copies the authorized role bindings')
    token = p.obj_invoke(control1, p.OBJ_OP_NAMESPACE_ATTACH,
                         probe, space.MREPL)
    p.obj_invoke(control1, p.OBJ_OP_NAMESPACE_DETACH, token)
    ok(token > 0,
       'RFNAMEG without RFPROC installs only a new Pgrp pointer')

    pid = p.rfork(p.RFCNAMEG)
    ok(pid == 0, 'RFCNAMEG without RFPROC returns zero in the caller')
    denied(lambda: p.obj_invoke(control1, p.OBJ_OP_NAMESPACE_ATTACH,
                                probe, space.MREPL),
           'old Pgrp NamespaceControl after RFCNAMEG')
    control2 = p.obj_open_namespace_current()
    token = p.obj_invoke(control2, p.OBJ_OP_NAMESPACE_ATTACH,
                         probe, space.MREPL)
    p.obj_invoke(control2, p.OBJ_OP_NAMESPACE_DETACH, token)
    ok(not openable('rollback') and token > 0,
       'RFCNAMEG without RFPROC produces an empty role view')
    p.obj_close(probe)
    p.obj_close(control0)
    p.obj_close(control1)
    p.obj_close(control2)

    # --- SPAWN COPY: independent roles, unselected roles unavailable -------
    bind_role(base_oid, 'spawn_copy')
    bind_role(later_oid, 'spawn_other')
    a, w = p.pipenew()
    child = ("import plan9 as p\n"
             "p.sleep(200)\n"
             "h=p.obj_open_binding('spawn_copy')\n"
             "assert p.obj_read(h,8)==b'T30-BASE'\n"
             "try: p.obj_open_binding('spawn_other')\n"
             "except OSError: pass\n"
             "else: raise AssertionError('unselected role available')\n"
             "print('T30-COPY-OK')\n")
    pid = space.spawn(OID, handle=sh, argv=('micropython', '-c', child),
                      view=1, roles=('spawn_copy',),
                      pipes=((w, 2),), stdout=w)
    p.obj_bind(0, 'spawn_copy', 0, p.OBJ_BIND_REMOVE)
    p.pipeclose(w)
    out = read_all(a)
    ok(b'T30-COPY-OK' in out,
       'SPAWN COPY roles are independent of later parent unbinding')
    waited, status = space.wait()
    ok(waited == pid and status == '',
       'SPAWN COPY child exits cleanly and reaps')
    p.pipeclose(a)
    unbind('spawn_other')

    # --- Earlier opened Handle survives role removal and close -------------
    rev_oid = make_ge(REVOKE)
    rev_gh = geh(rev_oid)
    p.obj_bind(rev_gh, 'revrole',
               p.VREAD | p.VMETA | p.VDELEGATE)
    keep1 = p.obj_open_binding('revrole')
    keep2 = p.obj_open_binding('revrole')
    independent = geh(rev_oid)
    p.obj_close(keep1)
    ok(p.obj_read(keep2, len(REVOKE)) == REVOKE,
       'closing one role Handle preserves the other')
    p.obj_bind(0, 'revrole', 0, p.OBJ_BIND_REMOVE)
    ok(p.obj_read(keep2, len(REVOKE)) == REVOKE,
       'a previously opened Handle remains usable after role removal')
    derived = p.obj_open_handle(keep2, p.VREAD | p.VMETA)
    p.obj_revoke(keep2)
    denied(lambda: p.obj_read(derived, len(REVOKE)),
           'derived authority after explicit revocation')
    ok(p.obj_read(independent, len(REVOKE)) == REVOKE,
       'explicit revocation does not invalidate an independent grant')
    p.obj_close(derived)
    p.obj_close(keep2)
    p.obj_close(independent)
    p.obj_close(rev_gh)

    # --- SPAWN SHARE: later parent edits, invalid SHARE has no effect ------
    pid = p.rfork(p.RFCNAMEG)
    ok(pid == 0, 'reset to an empty Pgrp for the SHARE scenario')
    bind_role(base_oid, 'share_base')
    bind_role(later_oid, 'share_two')
    denied(lambda: space.spawn(OID, handle=sh,
                               argv=('micropython', '-c', 'pass'),
                               view=2, roles=('share_base',)),
           'SHARE without every effective role')
    ok(openable('share_base') and openable('share_two'),
       'rejected SHARE leaves the parent roles intact')
    denied(lambda: space.wait(),
           'rejected SHARE leaves no live child')
    unbind('share_two')

    a, w = p.pipenew()
    child = ("import plan9 as p\n"
             "p.sleep(300)\n"
             "h=p.obj_open_binding('share_later')\n"
             "assert p.obj_read(h,9)==b'T30-LATER'\n"
             "try: p.obj_open_binding('share_base')\n"
             "except OSError: pass\n"
             "else: raise AssertionError('shared unbind not observed')\n"
             "print('T30-SHARE-OK')\n")
    pid = space.spawn(OID, handle=sh, argv=('micropython', '-c', child),
                      view=2, roles=('share_base',),
                      pipes=((w, 2),), stdout=w)
    p.obj_bind(0, 'share_base', 0, p.OBJ_BIND_REMOVE)
    p.obj_bind(later_gh, 'share_later',
               p.VREAD | p.VMETA | p.VDELEGATE)
    p.pipeclose(w)
    out = read_all(a)
    ok(b'T30-SHARE-OK' in out,
       'SPAWN SHARE observes later parent role bind and unbind')
    waited, status = space.wait()
    ok(waited == pid and status == '',
       'SPAWN SHARE child exits cleanly and reaps')
    p.pipeclose(a)

    # The child's fresh Pgrp is already replaced when the ambiguous stdout
    # mapping fails.  The abort path must release exactly the acquired share.
    a, w = p.pipenew()
    denied(lambda: space.spawn(OID, handle=sh,
                               argv=('micropython', '-c', 'pass'),
                               view=2, roles=('share_later',),
                               pipes=((w, 2), (w, 2)), stdout=w),
           'late SHARE failure after the Pgrp transfer')
    ok(openable('share_later'),
       'late SHARE failure restores the parent Pgrp reference')
    denied(lambda: space.wait(),
           'late SHARE failure leaves no live child')
    p.pipeclose(a)
    p.pipeclose(w)

    # --- Pgrp survives the exit of its parent while a live child owns it ---
    nested_oid = make_ge(NESTED)
    nested_gh = geh(nested_oid)
    sig_r, sig_w = p.pipenew()
    pid_a, ch = p.rforkex(p.RFPROC | p.RFNAMEG,
                          ((nested_gh, p.VREAD | p.VMETA | p.VDELEGATE, 1),
                           (sig_w, p.VWRITE | p.VMETA | p.VDELEGATE, 2)))
    if pid_a == 0:
        cg = find_slot(1, p.VKIND_GE)
        cw = find_slot(2, p.VKIND_PIPE)
        if not (cg and cw):
            p.exits('A-handles')
        try:
            p.obj_bind(cg, 'nested',
                       p.VREAD | p.VMETA | p.VDELEGATE)
        except OSError:
            p.exits('A-bind:' + p.errstr())
        try:
            pid_b, chb = p.rforkex(
                p.RFPROC | p.RFNOWAIT,
                ((cw, p.VWRITE | p.VMETA | p.VDELEGATE, 1),))
        except OSError:
            p.exits('A-fork:' + p.errstr())
        if pid_b == 0:
            p.sleep(400)
            bw = find_slot(1, p.VKIND_PIPE)
            good = False
            try:
                good = read_role('nested', len(NESTED)) == NESTED
            except OSError:
                p.exits('B-read:' + p.errstr())
            if bw:
                p.pipewrite(bw, b'BOK' if good else b'BAD')
            else:
                p.exits('B-nopipe')
            p.exits('')
        p.exits('')       # the intermediate parent exits while B is live
    p.pipeclose(sig_w)
    val = p.piperead(sig_r, 3)
    waited, status = p.wait()
    p.pipeclose(sig_r)
    ok(waited == pid_a and status == '',
       'nested parent exits cleanly while its child keeps the Pgrp status=%r' % (status,))
    ok(val == b'BOK',
       'the last live child still resolves the shared Pgrp role')
    ok(not openable('nested'),
       'the RFNAMEG nested Pgrp never affects the boot namespace')
    p.obj_close(nested_gh)

    # --- Repeated create/exit/wait cycles release and retain exactly once -
    bind_role(base_oid, 'cycle')
    ok(visible(base_oid), 'cycle role is visible before repeated forks')
    for i in range(50):
        pid = p.rfork(p.RFPROC | p.RFNAMEG)
        if pid == 0:
            good = visible(base_oid) and openable('cycle')
            p.exits('' if good else 't30-cycle-copy')
        waited, status = p.wait()
        if not (waited == pid and status == ''):
            raise AssertionError('copy cycle %d waited=%r status=%r' %
                                 (i, waited, status))
    for i in range(20):
        pid = p.rfork(p.RFPROC)
        if pid == 0:
            p.exits('' if visible(base_oid) else 't30-cycle-share')
        waited, status = p.wait()
        if not (waited == pid and status == ''):
            raise AssertionError('share cycle %d waited=%r status=%r' %
                                 (i, waited, status))
    ok(visible(base_oid) and openable('cycle'),
       'repeated copy/share create-exit-wait cycles preserve the survivor')
    unbind('cycle')

    # --- NamespaceControl ownership does not outlive or cycle the Pgrp -----
    bind_role(base_oid, 'ctlprobe')
    probe = space.view(('ctlprobe',))
    control = p.obj_open_namespace_current()
    pid, ch = p.rforkex(p.RFPROC, ((control, p.VCONTROL | p.VMETA, 1),))
    if pid == 0:
        h = find_slot(1, p.VKIND_NAMESPACE_CONTROL)
        p.exits('' if h else 't30-control-noslot')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'child holding an inherited NamespaceControl exits cleanly status=%r'
       % (status,))
    token = p.obj_invoke(control, p.OBJ_OP_NAMESPACE_ATTACH,
                         probe, space.MREPL)
    p.obj_invoke(control, p.OBJ_OP_NAMESPACE_DETACH, token)
    ok(token > 0,
       'child exit does not leave a dangling or extra Pgrp reference')
    p.obj_close(control)
    control2 = p.obj_open_namespace_current()
    token = p.obj_invoke(control2, p.OBJ_OP_NAMESPACE_ATTACH,
                         probe, space.MREPL)
    p.obj_invoke(control2, p.OBJ_OP_NAMESPACE_DETACH, token)
    ok(token > 0,
       'closing the last NamespaceControl leaves the process Pgrp intact')
    p.obj_close(control2)
    p.obj_close(probe)
    ok(True, 'no ownership cycle between NamespaceControl and Pgrp')

    # --- RFNOMNT stays a last-step extension denial, unchanged -------------
    pid = p.rfork(p.RFNOMNT)
    ok(pid == 0, 'RFNOMNT without RFPROC updates the caller Pgrp')
    gh = geh(later_oid)
    denied(lambda: p.obj_bind(gh, 'nomnt',
                              p.VREAD | p.VMETA | p.VDELEGATE),
           'RFNOMNT denies namespace extension')
    ok(p.obj_read(gh, len(LATER)) == LATER,
       'RFNOMNT denial leaves the already opened Handle valid')
    p.obj_close(gh)

    p.obj_close(later_gh)
    p.obj_close(sh)
    print('T30-FINISH', n[0], 'PASS')
    return 1
