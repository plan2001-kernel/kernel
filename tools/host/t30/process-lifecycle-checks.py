"""Executable Pgrp/OContext lifecycle regression (issue #30).

The seeded init runs once on a fresh O image.  Every assertion prints only
after it passes; the runner requires a strictly sequential T30-FINISH.

All namespace mutation happens in the boot owner.  Forked or spawned children
receive explicitly delegated Handles; they never open an O Space by name,
because bootstrap Space authority is intentionally limited to the boot owner.

Every scenario ends with a T30-LIFE marker after all expected owners and
Handles are gone.  A diag build (PLAN2001_LIFECYCLE_DIAG) emits T30LIFE live
Pgrp/OContext counters; run-lifecycle-diag.sh checks that every marker
returns to the baseline captured by the first marker.
"""

BASE = b'T30-BASE'
LATER = b'T30-LATER'
CHILD_ADD = b'T30-CHILD-ADD'
ISO = b'T30-ISO'
ISO_CHILD = b'T30-ISO-CHILD'
NESTED = b'T30-NESTED'
REVOKE = b'T30-REVOKE'
OID = 0x4f45584543000003

# SPAWN's Pipe plan grants only read/write rights, not Vmeta, so the child
# identifies handshake ends by their planned low-byte slot and does not call
# obj_info on them.
SLOT = ("import plan9 as p\n"
        "def slot(n):\n"
        " for h in p.obj_list():\n"
        "  if (h&0xff)==n: return h\n"
        " return 0\n")

COPY = (SLOT +
        "p.pipewrite(slot(1), b'R')\n"
        "p.piperead(slot(2), 1)\n"
        "h=p.obj_open_binding('spawn_copy')\n"
        "assert p.obj_read(h,8)==b'T30-BASE'\n"
        "try: p.obj_open_binding('spawn_other')\n"
        "except OSError: pass\n"
        "else: raise AssertionError('unselected role available')\n"
        "print('T30-COPY-OK')\n")

SHARE = (SLOT +
         "p.pipewrite(slot(1), b'R')\n"
         "p.piperead(slot(2), 1)\n"
         "h=p.obj_open_binding('share_later')\n"
         "assert p.obj_read(h,9)==b'T30-LATER'\n"
         "try: p.obj_open_binding('share_base')\n"
         "except OSError: pass\n"
         "else: raise AssertionError('shared unbind not observed')\n"
         "print('T30-SHARE-OK')\n")

PLAIN = ("import plan9 as p\n"
         "import space\n"
         "assert len(p.obj_list())==0\n"
         "assert space.visible()==[]\n")


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

    roles = []

    def bind_role(oid, role, rights=None):
        if rights is None:
            rights = p.VREAD | p.VMETA | p.VDELEGATE
        h = geh(oid)
        p.obj_bind(h, role, rights)
        p.obj_close(h)
        if role not in roles:
            roles.append(role)

    def unbind(role):
        p.obj_bind(0, role, 0, p.OBJ_BIND_REMOVE)
        if role in roles:
            roles.remove(role)

    def forget(role):
        if role in roles:
            roles.remove(role)

    def clear_roles():
        for role in list(roles):
            unbind(role)

    def handshake_spawn(child, roles, view):
        """Ready -> parent edit -> continue; returns (pid, ready_r, go_w, out_r).

        Handles are delegated only through the SPAWN plan.  The parent keeps
        the read ends and the continue writer; both sides close everything
        they do not need so read_all sees EOF only after the child exits.
        """
        ready_r, ready_w = p.pipenew()
        go_r, go_w = p.pipenew()
        out_r, out_w = p.pipenew()
        pid = space.spawn(OID, handle=sh,
                          argv=('micropython', '-c', child),
                          view=view, roles=roles,
                          pipes=((ready_w, 2), (go_r, 1), (out_w, 2)),
                          stdout=out_w)
        p.pipeclose(ready_w)
        p.pipeclose(go_r)
        p.pipeclose(out_w)
        return pid, ready_r, go_w, out_r

    base_oid = make_ge(BASE)
    later_oid = make_ge(LATER)
    mark('base')

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
    forget('probe')
    p.obj_close(probe)
    p.obj_close(control)
    mark('identity')

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
        if p.piperead(cr, 1) != b'G':
            p.exits('t30-share-signal')
        if not visible(later_oid):
            p.exits('t30-share-later')
        p.exits('')
    bind_role(later_oid, 'laterrole')
    p.pipewrite(w, b'G')
    p.pipeclose(w)
    waited, status = p.wait()
    p.pipeclose(a)
    ok(waited == pid and status == '',
       'RFPROC child observes a later parent role mutation status=%r' % (status,))
    ok(read_role('from_child', len(CHILD_ADD)) == CHILD_ADD,
       'RFPROC child role mutation is visible to the parent')
    unbind('from_child')
    p.obj_close(child_gh)
    mark('share-mutation')

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
    unbind('iso')
    bind_role(later_oid, 'iso_parent_only')
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
    mark('copy-isolation')

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
    mark('empty')

    # --- RFNAMEG/RFCNAMEG without RFPROC: current Pgrp pointer only --------
    # Run this in a child so the boot process keeps sharing the permanent
    # kernel anchor group.  The child leaves that group locally, and its exit
    # must then release the split Pgrp/OContext back to the marker baseline.
    bind_role(base_oid, 'rollback')
    pid = p.rfork(p.RFPROC)
    if pid == 0:
        try:
            denied(lambda: p.rfork(p.RFNAMEG | p.RFCNAMEG),
                   'RFNAMEG together with RFCNAMEG')
            denied(lambda: p.rfork(p.RFNOWAIT),
                   'RFNOWAIT without RFPROC')
            assert visible(base_oid) and openable('rollback')
            control0 = p.obj_open_namespace_current()
            probe = space.view(('rollback',))
            assert p.rfork(p.RFNAMEG) == 0
            denied(lambda: p.obj_invoke(control0, p.OBJ_OP_NAMESPACE_ATTACH,
                                        probe, space.MREPL),
                   'old Pgrp NamespaceControl after RFNAMEG')
            control1 = p.obj_open_namespace_current()
            assert visible(base_oid) and openable('rollback')
            token = p.obj_invoke(control1, p.OBJ_OP_NAMESPACE_ATTACH,
                                 probe, space.MREPL)
            p.obj_invoke(control1, p.OBJ_OP_NAMESPACE_DETACH, token)
            assert token > 0
            assert p.rfork(p.RFCNAMEG) == 0
            denied(lambda: p.obj_invoke(control1, p.OBJ_OP_NAMESPACE_ATTACH,
                                        probe, space.MREPL),
                   'old Pgrp NamespaceControl after RFCNAMEG')
            control2 = p.obj_open_namespace_current()
            token = p.obj_invoke(control2, p.OBJ_OP_NAMESPACE_ATTACH,
                                 probe, space.MREPL)
            p.obj_invoke(control2, p.OBJ_OP_NAMESPACE_DETACH, token)
            assert not openable('rollback') and token > 0
            p.obj_close(probe)
            p.obj_close(control0)
            p.obj_close(control1)
            p.obj_close(control2)
        except OSError:
            p.exits('nonrfork-os:' + p.errstr())
        except AssertionError as e:
            p.exits('nonrfork-assert:' + str(e))
        p.exits('')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'RFNAMEG/RFCNAMEG without RFPROC switch only the caller Pgrp and '
       'release it on exit status=%r' % (status,))
    unbind('rollback')
    mark('current-group')

    # --- Ordinary SPAWN has no ambient roles or inherited Handles ----------
    pid = space.spawn(OID, handle=sh, argv=('micropython', '-c', PLAIN))
    waited, status = space.wait()
    ok(waited == pid and status == '',
       'ordinary SPAWN starts with no roles and no object Handles status=%r'
       % (status,))
    mark('spawn-plain')

    # --- SPAWN COPY: independent roles, unselected roles unavailable -------
    bind_role(base_oid, 'spawn_copy')
    bind_role(later_oid, 'spawn_other')
    pid, ready_r, go_w, out_r = handshake_spawn(COPY, ('spawn_copy',), 1)
    ok(p.piperead(ready_r, 1) == b'R',
       'SPAWN COPY child synchronizes readiness before the parent edit')
    unbind('spawn_copy')
    p.pipewrite(go_w, b'G')
    p.pipeclose(go_w)
    out = read_all(out_r)
    ok(b'T30-COPY-OK' in out,
       'SPAWN COPY roles are independent of later parent unbinding')
    waited, status = space.wait()
    ok(waited == pid and status == '',
       'SPAWN COPY child exits cleanly and reaps')
    p.pipeclose(ready_r)
    p.pipeclose(out_r)
    unbind('spawn_other')
    mark('spawn-copy')

    # --- Earlier opened Handle survives role removal and close -------------
    rev_oid = make_ge(REVOKE)
    bind_role(rev_oid, 'revrole')
    keep1 = p.obj_open_binding('revrole')
    keep2 = p.obj_open_binding('revrole')
    independent = geh(rev_oid)
    p.obj_close(keep1)
    ok(p.obj_read(keep2, len(REVOKE)) == REVOKE,
       'closing one role Handle preserves the other')
    unbind('revrole')
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
    mark('handle-lifetime')

    # --- SPAWN SHARE: later parent edits, invalid SHARE has no effect ------
    clear_roles()
    ok(len(space.visible()) == 0, 'cleared role view for the SHARE scenario')
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

    pid, ready_r, go_w, out_r = handshake_spawn(SHARE, ('share_base',), 2)
    ok(p.piperead(ready_r, 1) == b'R',
       'SPAWN SHARE child synchronizes readiness before the parent edit')
    unbind('share_base')
    bind_role(later_oid, 'share_later')
    p.pipewrite(go_w, b'G')
    p.pipeclose(go_w)
    out = read_all(out_r)
    ok(b'T30-SHARE-OK' in out,
       'SPAWN SHARE observes later parent role bind and unbind')
    waited, status = space.wait()
    ok(waited == pid and status == '',
       'SPAWN SHARE child exits cleanly and reaps')
    p.pipeclose(ready_r)
    p.pipeclose(out_r)

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
    mark('spawn-share')

    # --- Pgrp survives the exit of its parent while a live child owns it ---
    # The result Pipe's write end is also bound as a role in the survivor
    # Pgrp, so read_all() reaches EOF only when the Pgrp's last owner exits.
    nested_oid = make_ge(NESTED)
    nested_gh = geh(nested_oid)
    go_r, go_w = p.pipenew()
    res_r, res_w = p.pipenew()
    pid_a, ch = p.rforkex(
        p.RFPROC | p.RFNAMEG,
        ((nested_gh, p.VREAD | p.VMETA | p.VDELEGATE, 1),
         (go_r, p.VREAD | p.VMETA | p.VDELEGATE, 2),
         (res_w, p.VWRITE | p.VMETA | p.VDELEGATE, 3)))
    if pid_a == 0:
        cg = find_slot(1, p.VKIND_GE)
        cgo = find_slot(2, p.VKIND_PIPE)
        cres = find_slot(3, p.VKIND_PIPE)
        if not (cg and cgo and cres):
            p.exits('A-handles')
        try:
            p.obj_bind(cg, 'nested',
                       p.VREAD | p.VMETA | p.VDELEGATE)
        except OSError:
            p.exits('A-bind:' + p.errstr())
        try:
            pid_b, chb = p.rforkex(
                p.RFPROC | p.RFNOWAIT,
                ((cgo, p.VREAD | p.VMETA | p.VDELEGATE, 1),
                 (cres, p.VWRITE | p.VMETA | p.VDELEGATE, 2)))
        except OSError:
            p.exits('A-fork:' + p.errstr())
        if pid_b == 0:
            bgo = find_slot(1, p.VKIND_PIPE)
            bres = find_slot(2, p.VKIND_PIPE)
            if not (bgo and bres):
                p.exits('B-handles')
            if p.piperead(bgo, 1) != b'G':
                p.exits('B-signal')
            good = False
            try:
                good = read_role('nested', len(NESTED)) == NESTED
            except OSError:
                p.exits('B-read:' + p.errstr())
            try:
                p.obj_bind(bres, 'notify',
                           p.VWRITE | p.VMETA | p.VDELEGATE)
            except OSError:
                p.exits('B-bind:' + p.errstr())
            p.pipewrite(bres, b'BOK' if good else b'BAD')
            p.exits('')
        p.exits('')       # the intermediate parent exits while B is live
    p.pipeclose(go_r)
    p.pipeclose(res_w)
    waited, status = p.wait()
    ok(waited == pid_a and status == '',
       'nested parent exits cleanly while its child keeps the Pgrp status=%r'
       % (status,))
    p.pipewrite(go_w, b'G')
    p.pipeclose(go_w)
    val = read_all(res_r)
    p.pipeclose(res_r)
    ok(val == b'BOK',
       'the last live child still resolves the shared Pgrp role')
    ok(not openable('nested'),
       'the RFNAMEG nested Pgrp never affects the boot namespace')
    p.obj_close(nested_gh)
    mark('nested')

    # --- Repeated create/exit/wait cycles release and retain exactly once --
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
    mark('cycles')

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
    forget('ctlprobe')
    bind_role(base_oid, 'mutprobe')
    ok(openable('mutprobe'),
       'the process Pgrp remains mutable after the last control closes')
    unbind('mutprobe')
    mark('control')

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

    p.obj_close(sh)
    mark('end')
    print('T30-FINISH', n[0], 'PASS')
    return 1
