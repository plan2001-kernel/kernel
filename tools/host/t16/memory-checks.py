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
    space.obind('t16role', oid, 1 | 32, sh)
    nh, sid, found, rights = space.olookup('t16role')
    assert found == oid and p.obj_read(nh, 6) == b'T16-GE'
    space.orevoke(nh)
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
    print('T16-PASS namespace unified handle and stale denial')
    other, other_oid = p.obj_create_ge(sh, b'T16-OTHER')
    p.obj_commit(sh)
    space.obind('t16view', oid, space.OREAD | space.ODELEGATE, sh)
    view = space.view(('t16view',))
    space.obind('t16view', other_oid, space.OREAD | space.ODELEGATE, sh)

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
    vh, _, found, _ = space.olookup('t16view')
    assert found == oid
    space.orevoke(vh)
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
    print('T16-PASS namespace rights and process isolation')
    for nr in (55, 59, 101, 102, 103, 105):
        try:
            p.sysraw(nr)
        except OSError:
            pass
        else:
            raise AssertionError('retired O Space syscall active')
    print('T16-PASS retired O Space and namespace calls denied')
    p.obj_close(rh)
    p.obj_close(ro)
    p.obj_close(gh)
    p.obj_close(sh)
    shared.n += 1
    return 1
