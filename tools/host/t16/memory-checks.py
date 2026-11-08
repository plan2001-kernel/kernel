"""Userland regression for the native segment and semaphore ABI."""

class Counter:
    def __init__(self, n=0):
        self.n = n

shared = Counter()
alias = shared

def init():
    shared.n = 0


def run():
    import plan9 as p
    addr = p.seg_attach('memory', 8192)
    assert addr > 0
    print('T16-PASS segment attach', hex(addr))
    p.seg_brk(addr, addr + 4096)
    print('T16-PASS segment break')
    p.seg_flush(addr, 4096)
    print('T16-PASS segment flush')
    p.seg_free(addr, 4096)
    print('T16-PASS segment free')
    p.seg_detach(addr)
    print('T16-PASS segment flush free detach')
    word = bytearray(8)
    assert p.sem_acquire(word, 0) == 0
    assert p.sem_release(word, 1) >= 0
    assert p.sem_acquire(word, 0) == 1
    assert p.sem_timed_acquire(word, 1) == 0
    print('T16-PASS semaphore release acquire timeout')
    import space
    sh = p.obj_open_space('init')
    if hasattr(shared, 'previous_ge_oid'):
        previous = p.obj_open_ge_space(sh, shared.previous_ge_oid)
        assert p.obj_read(previous, 11) == b'T16-UPDATED'
        p.obj_close(previous)
        print('T16-PASS GE restart reopen')
    fresh, fresh_oid = p.obj_create_ge(sh, b'T16-DRAFT')
    p.obj_commit(sh)
    writer = p.obj_open_ge_space(sh, fresh_oid)
    assert p.obj_write(writer, b'T16-UPDATED') == 11
    p.obj_commit(writer)
    p.obj_close(writer)
    reopened = p.obj_open_ge_space(sh, fresh_oid)
    assert p.obj_read(reopened, 11) == b'T16-UPDATED'
    p.obj_close(reopened)
    p.obj_close(fresh)
    shared.previous_ge_oid = fresh_oid
    print('T16-PASS GE write commit and reopen')
    gh, oid = p.obj_create_ge(sh, b'T16-GE')
    p.obj_commit(sh)
    assert p.obj_read(gh, 6) == b'T16-GE'
    print('T16-PASS GE create read commit')
    ro = p.obj_grant(sh, p.VREAD | p.VMETA | p.VDELEGATE)
    rh = p.obj_open_ge_space(ro, oid)
    assert p.obj_read(rh, 6) == b'T16-GE'
    try:
        p.obj_create_ge(ro, b'denied')
    except OSError:
        pass
    else:
        raise AssertionError('attenuated Space created GE object')
    print('T16-PASS GE attenuation')
    child = ("import plan9 as p\n"
             "hs=p.obj_list()\n"
             "g=0\n"
             "for h in hs:\n"
             "    if p.obj_info(h)[8]==p.VKIND_GE: g=h\n"
             "assert g and p.obj_read(g,6)==b'T16-GE'\n"
             "try: p.obj_commit(g)\n"
             "except OSError: pass\n"
             "else: raise AssertionError('child gained commit')\n"
             "try: p.obj_read(" + str(gh) + ",6)\n"
             "except OSError: pass\n"
             "else: raise AssertionError('parent handle crossed process')\n")
    cid = 0x4f45584543000003
    pid = space.spawn(cid, handle=sh, argv=('micropython', '-c', child),
                      vhandles=((gh, p.VREAD | p.VMETA),))
    waited, status = space.wait()
    assert waited == pid and status == ''
    print('T16-PASS child GE rights and process isolation')
    source = p.obj_open_ge_space(sh, oid)
    p.obj_bind(source, 't16role', p.VREAD | p.VMETA | p.VDELEGATE)
    nh = p.obj_open_binding('t16role')
    assert p.obj_info(nh)[2] == oid and p.obj_read(nh, 6) == b'T16-GE'
    p.obj_revoke(nh)
    try:
        p.obj_open_binding('t16role')
    except OSError:
        pass
    else:
        raise AssertionError('revoked role reopened')
    try:
        p.obj_read(nh, 6)
    except OSError:
        pass
    else:
        raise AssertionError('revoked namespace handle read')
    p.obj_close(nh)
    try:
        p.obj_read(nh, 6)
    except OSError:
        pass
    else:
        raise AssertionError('stale namespace handle read')
    assert p.obj_read(gh, 6) == b'T16-GE'
    p.obj_close(source)
    print('T16-PASS namespace unified handle and stale denial')
    other, other_oid = p.obj_create_ge(sh, b'T16-OTHER')
    p.obj_commit(sh)
    view_source = p.obj_open_ge_space(sh, oid)
    p.obj_bind(view_source, 't16view', p.VREAD | p.VMETA | p.VDELEGATE)
    view = space.view(('t16view',))
    other_source = p.obj_open_ge_space(sh, other_oid)
    p.obj_bind(other_source, 't16view', p.VREAD | p.VMETA | p.VDELEGATE)

    def visible(target):
        return any(record[2] == target and record[3] & space.OREAD
                   for record in space.visible())

    assert visible(other_oid) and not visible(oid)
    for mode, expected in ((space.MAFTER, other_oid),
                           (space.MBEFORE, oid), (space.MREPL, oid)):
        token = space.mount(view, mode)
        assert token > 0 and visible(expected)
        if expected == oid:
            assert not visible(other_oid)
        else:
            assert not visible(oid)
        space.unmount(token)
        if mode != space.MREPL:
            assert visible(other_oid) and not visible(oid)
    assert not visible(oid) and not visible(other_oid)
    print('T16-PASS sealed view, attach precedence, detach and visible enumeration')

    token = space.mount(view, space.MAFTER)
    vh = p.obj_open_binding('t16view')
    assert p.obj_info(vh)[2] == oid
    p.obj_revoke(vh)
    p.obj_close(vh)
    assert not visible(oid)
    space.unmount(token)
    try:
        space.mount(view, space.MAFTER)
    except OSError:
        pass
    else:
        raise AssertionError('revoked sealed view attached')
    print('T16-PASS view revocation lineage')

    control = p.obj_open_namespace_current()
    delegated_control = p.obj_open_handle(control, p.VCONTROL | p.VDELEGATE)
    for handle, role in ((control, 't16control'),
                         (delegated_control, 't16controlcopy')):
        try:
            p.obj_bind(handle, role, p.VCONTROL | p.VDELEGATE)
        except OSError:
            pass
        else:
            raise AssertionError('namespace control bound into its namespace')
        try:
            p.obj_open_binding(role)
        except OSError:
            pass
        else:
            raise AssertionError('rejected namespace control role published')
        assert p.obj_invoke(handle, p.OBJ_OP_NAMESPACE_VISIBLE) >= 0
    p.obj_close(delegated_control)
    print('T16-PASS namespace control binding cycle denied')
    weak = p.obj_open_handle(control, p.VDELEGATE)
    try:
        p.obj_invoke(weak, p.OBJ_OP_NAMESPACE_VISIBLE)
    except OSError:
        pass
    else:
        raise AssertionError('attenuated namespace control enumerated')
    p.obj_close(weak)
    child = ("import plan9 as p, space\n"
             "control=0\n"
             "for h in p.obj_list():\n"
             "    if p.obj_info(h)[8]==p.VKIND_NAMESPACE_CONTROL: control=h\n"
             "assert control\n"
             "try: p.obj_invoke(control, p.OBJ_OP_NAMESPACE_VISIBLE)\n"
             "except OSError: pass\n"
             "else: raise AssertionError('parent namespace control used by child')\n"
             "assert not space.visible()\n"
             "try: space.view(('t16view',))\n"
             "except OSError: pass\n"
             "else: raise AssertionError('child exported unauthorized role')\n")
    pid = space.spawn(cid, handle=sh, argv=('micropython', '-c', child),
                      vhandles=((control, p.VCONTROL | p.VMETA),))
    waited, status = space.wait()
    assert waited == pid and status == ''
    p.obj_close(control)
    p.obj_close(view)
    p.obj_close(other)
    p.obj_close(view_source)
    p.obj_close(other_source)
    print('T16-PASS namespace rights and process isolation')

    def denied(action):
        try:
            action()
        except OSError:
            return
        raise AssertionError('unauthorized object operation succeeded')

    mask = p.VREAD | p.VWRITE | p.VCOMMIT | p.VMETA | p.VDELEGATE
    authority = p.obj_open_ge_space(sh, oid)
    independent = p.obj_open_ge_space(sh, oid)
    p.obj_bind(authority, 't16binding', mask)
    first = p.obj_open_binding('t16binding')
    held = p.obj_open_binding('t16binding')
    p.obj_close(first)
    assert p.obj_read(held, 6) == b'T16-GE'
    print('T16-PASS close preserves second Handle')

    child = ("import plan9 as p\n"
             "h=p.obj_open_binding('t16binding')\n"
             "assert p.obj_read(h,6)==b'T16-GE'\n")
    pid = space.spawn(cid, handle=sh, argv=('micropython', '-c', child),
                      view=1, roles=('t16binding',))
    waited, status = space.wait()
    assert waited == pid and status == ''
    print('T16-PASS role binding inherited by child')

    denied(lambda: p.obj_bind(held, 't16binding', 0, p.OBJ_BIND_REMOVE))
    denied(lambda: p.obj_bind(0, 't16binding', 1, p.OBJ_BIND_REMOVE))
    p.obj_bind(0, 't16binding', 0, p.OBJ_BIND_REMOVE)
    denied(lambda: p.obj_open_binding('t16binding'))
    assert p.obj_read(held, 6) == b'T16-GE'
    replacement = p.obj_open_ge_space(sh, other_oid)
    p.obj_bind(replacement, 't16binding', p.VREAD | p.VMETA | p.VDELEGATE)
    newest = p.obj_open_binding('t16binding')
    assert p.obj_info(newest)[2] == other_oid
    assert p.obj_read(newest, 9) == b'T16-OTHER'
    assert p.obj_read(held, 6) == b'T16-GE'
    p.obj_bind(0, 't16binding', 0, p.OBJ_BIND_REMOVE)
    p.obj_close(newest)
    p.obj_close(replacement)
    print('T16-PASS unbind and rebind preserve prior Handle')

    payload = p.obj_create(b'T16-VOLATILE')
    p.obj_bind(payload, 't16bytes', p.VREAD | p.VMETA | p.VDELEGATE)
    opened_payload = p.obj_open_binding('t16bytes')
    assert p.obj_read(opened_payload, 12) == b'T16-VOLATILE'
    child = ("import plan9 as p\n"
             "h=p.obj_open_binding('t16bytes')\n"
             "assert p.obj_read(h,12)==b'T16-VOLATILE'\n")
    pid = space.spawn(cid, handle=sh, argv=('micropython', '-c', child),
                      view=1, roles=('t16bytes',))
    waited, status = space.wait()
    assert waited == pid and status == ''
    p.obj_bind(0, 't16bytes', 0, p.OBJ_BIND_REMOVE)
    assert p.obj_read(opened_payload, 12) == b'T16-VOLATILE'
    p.obj_close(opened_payload)
    p.obj_close(payload)
    print('T16-PASS volatile object resolves through role binding')

    descendant = p.obj_open_handle(held, mask)
    p.obj_bind(authority, 't16sibling', mask)
    sibling = p.obj_open_binding('t16sibling')
    weak = p.obj_open_handle(held, p.VREAD | p.VMETA)
    denied(lambda: p.obj_revoke(weak))
    denied(lambda: p.obj_open_handle(weak, mask))
    denied(lambda: p.obj_revoke(oid))
    assert p.obj_read(independent, 6) == b'T16-GE'
    p.obj_revoke(held)
    denied(lambda: p.obj_read(descendant, 6))
    denied(lambda: p.obj_read(sibling, 6))
    denied(lambda: p.obj_write(descendant, b'change'))
    denied(lambda: p.obj_commit(descendant))
    denied(lambda: p.obj_open_handle(descendant, mask))
    assert p.obj_info(independent)[2] == oid
    assert p.obj_read(independent, 6) == b'T16-GE'
    p.obj_bind(0, 't16sibling', 0, p.OBJ_BIND_REMOVE)
    for handle in (weak, descendant, sibling, held, authority, independent):
        p.obj_close(handle)
    print('T16-PASS GE revoke lineage, attenuation and independent authority')

    fifo = p.obj_fifocreate()
    p.obj_bind(fifo, 't16fifo', p.VSUBSCRIBE | p.VMETA | p.VCONTROL)
    opened_fifo = p.obj_open_binding('t16fifo')
    sub = p.obj_subscribe(opened_fifo, p.VRECV | p.VMETA)
    p.obj_revoke(fifo)
    assert p.obj_stat(fifo)[5] and p.obj_stat(sub)[6]
    denied(lambda: p.obj_receive(sub, 0))
    p.obj_bind(0, 't16fifo', 0, p.OBJ_BIND_REMOVE)
    p.obj_close(opened_fifo)
    p.obj_close(sub)
    p.obj_close(fifo)
    print('T16-PASS FIFO revoke closes subscriptions')
    for nr in (55, 59):
        try:
            p.sysraw(nr)
        except OSError:
            pass
        else:
            raise AssertionError('retired O Space syscall active')
    for nr in (97, 98, 99, 101, 102, 103, 104, 105, 106):
        child = "import plan9 as p\np.sysraw(" + str(nr) + ")\n"
        pid = space.spawn(cid, handle=sh, argv=('micropython', '-c', child))
        waited, status = space.wait()
        assert waited == pid and status != ''
    print('T16-PASS retired O Space, binding and namespace calls denied')
    p.obj_close(rh)
    p.obj_close(ro)
    p.obj_close(gh)
    p.obj_close(sh)
    shared.n += 1
    return 1
