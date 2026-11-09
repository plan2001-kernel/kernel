"""Userland regression for atomic process creation and Handle inheritance.

The seeded init module runs once on a fresh O image.  Every assertion prints
only after it passes; the runner requires a strictly sequential T21-FINISH.
"""


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

    OID = 0x4f45584543000003
    n = [0]

    def ok(value, name):
        assert value, name
        n[0] += 1
        print('T21-PASS', n[0], name)

    def denied(call, name):
        try:
            call()
        except OSError:
            return
        raise AssertionError('expected rejection: ' + name)

    def find_bytes(data):
        for h in p.obj_list():
            try:
                i = p.obj_info(h)
            except OSError:
                continue
            if i[8] != p.VKIND_BYTES or i[3] != len(data):
                continue
            if p.obj_read(h, len(data)) == data:
                return h
        return 0

    def find_slot(slot):
        for h in p.obj_list():
            if (h & 0xff) != slot:
                continue
            try:
                i = p.obj_info(h)
            except OSError:
                continue
            if i[8] == p.VKIND_PIPE:
                return h
        return 0

    sh = p.obj_open_space('init')

    # --- RFORKEX: one Handle, rights attenuation, fresh child Handle ------
    one = b'T21-ONE'
    b = p.obj_create(one)
    base = p.obj_info(b)[4]
    pid, ch = p.rforkex(p.RFPROC, ((b, p.VREAD | p.VMETA),))
    if pid == 0:
        f = find_bytes(one)
        good = f != 0 and p.obj_info(f)[10] == (p.VREAD | p.VMETA)
        if good:
            try:
                p.obj_write(f, b'x')
            except OSError:
                pass
            else:
                good = False
        p.exits('' if good else 't21-one')
    waited, status = p.wait()
    ok(waited == pid and status == '' and len(ch) == 1,
       'RFORKEX inherits one Handle and child exits cleanly '
       '(status=%r)' % (status,))
    ok(ch[0] != b and (ch[0] >> 8) > (b >> 8),
       'RFORKEX assigns a fresh child-local generation')
    ok(p.obj_info(b)[4] == base and p.obj_read(b, len(one)) == one,
       'child exit releases only the inherited reference')

    # --- RFORKEX: multiple Handles with explicit destination slots -------
    m1 = b'T21-SLOT-1'
    m2 = b'T21-SLOT-2'
    b1 = p.obj_create(m1)
    b2 = p.obj_create(m2)
    pid, ch = p.rforkex(p.RFPROC,
                        ((b1, p.VREAD | p.VMETA, 1),
                         (b2, p.VREAD | p.VMETA | p.VSEND, 2)))
    if pid == 0:
        f1 = find_bytes(m1)
        f2 = find_bytes(m2)
        good = (f1 and f2 and (f1 & 0xff) == 1 and (f2 & 0xff) == 2 and
                (f1 >> 8) != (f2 >> 8) and
                p.obj_info(f1)[10] == (p.VREAD | p.VMETA) and
                p.obj_info(f2)[10] == (p.VREAD | p.VMETA | p.VSEND))
        p.exits('' if good else 't21-multi')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'RFORKEX child sees multiple Handles with attenuated rights')
    ok(len(ch) == 2 and (ch[0] & 0xff) == 1 and (ch[1] & 0xff) == 2 and
       (ch[0] >> 8) != (ch[1] >> 8),
       'RFORKEX reports explicit destination-slot mapping')

    # --- Ordinary RFORK copies no object Handle ---------------------------
    pid = p.rfork(p.RFPROC)
    if pid == 0:
        p.exits('' if len(p.obj_list()) == 0 else 't21-rfork')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'ordinary RFORK inherits no object Handles')

    # --- RFORKEX rejects invalid plans before creating a child ------------
    tmp = p.obj_create(b'T21-CLOSED')
    p.obj_close(tmp)
    no_delegate = p.obj_open_handle(b1, p.VREAD)
    holds = p.obj_info(b1)[4]
    denied(lambda: p.rforkex(p.RFPROC, ((0, p.VREAD),)),
           'invalid Handle')
    denied(lambda: p.rforkex(p.RFPROC, ((0x7fffffff, p.VREAD),)),
           'stale numeric Handle')
    denied(lambda: p.rforkex(p.RFPROC, ((tmp, 0),)),
           'closed Handle')
    stale = p.obj_create(b'T21-STALE')
    p.obj_close(stale)
    reused = p.obj_create(b'T21-REUSED')
    denied(lambda: p.rforkex(p.RFPROC, ((stale, 0),)),
           'stale generation after slot reuse')
    denied(lambda: p.rforkex(p.RFPROC, ((no_delegate, 0),)),
           'Handle without Vdelegate')
    denied(lambda: p.rforkex(p.RFPROC, ((b1, 0xffff),)),
           'excessive rights')
    denied(lambda: p.rforkex(p.RFPROC,
                             ((b1, p.VREAD, 1), (b2, p.VREAD, 1))),
           'duplicate destination slot')
    denied(lambda: p.rforkex(p.RFPROC, ((b1, p.VREAD, 1000),)),
           'destination slot out of range')
    denied(lambda: p.rforkex(p.RFPROC, ((b1, p.VREAD),), 99),
           'unsupported options version')
    denied(lambda: p.rforkex(p.RFPROC, ((b1, p.VREAD),), None, 17),
           'excessive options entry count')
    denied(lambda: p.rforkex(0, ((b1, p.VREAD),)),
           'inheritance without RFPROC')
    ok(p.obj_info(b1)[4] == holds and p.obj_read(b1, len(m1)) == m1,
       'rejected RFORKEX plans leave the parent unchanged')
    p.obj_close(no_delegate)

    # --- RFORKEX: bidirectional Pipe communication ------------------------
    # Two one-writer/one-reader pairs avoid two readers racing on one queue.
    req_r, req_w = p.pipenew()
    rsp_r, rsp_w = p.pipenew()
    pid, ch = p.rforkex(p.RFPROC,
                        ((req_r, p.VREAD | p.VMETA, 1),
                         (rsp_w, p.VWRITE | p.VMETA, 2)))
    if pid == 0:
        cr = find_slot(1)
        cw = find_slot(2)
        if not (cr and cw):
            p.exits('t21-pipe-find')
        d1 = p.piperead(cr, 4)
        if d1 != b'ping':
            p.exits('t21-pipe-read1')
        p.pipewrite(cw, b'pong')
        d2 = p.piperead(cr, 4)
        if d2 != b'pang':
            p.exits('t21-pipe-read2')
        p.pipewrite(cw, b'pung')
        p.exits('')
    # Only the child keeps the read end of req and the write end of rsp, so
    # an early child exit hangs the response pipe up instead of blocking the
    # parent forever.
    p.pipeclose(req_r)
    p.pipeclose(rsp_w)
    p.pipewrite(req_w, b'ping')
    p.pipewrite(req_w, b'pang')
    first = p.piperead(rsp_r, 4)
    second = p.piperead(rsp_r, 4)
    if first != b'pong' or second != b'pung':
        waited, status = p.wait()
        raise AssertionError('Pipe responses %r/%r waited=%r status=%r' %
                             (first, second, waited, status))
    ok(first == b'pong',
       'RFORKEX Pipe carries parent-to-child bytes')
    ok(second == b'pung',
       'RFORKEX Pipe carries child-to-parent bytes')
    waited, status = p.wait()
    ok(waited == pid and status == '', 'RFORKEX Pipe child exits cleanly')
    p.pipeclose(req_w)
    p.pipeclose(rsp_r)

    # --- RFORKEX: Pipe attenuation is enforced ----------------------------
    a, w = p.pipenew()
    pid, ch = p.rforkex(p.RFPROC,
                        ((a, p.VREAD | p.VMETA, 1),
                         (w, p.VWRITE | p.VMETA, 2)))
    if pid == 0:
        ca = find_slot(1)
        cw = find_slot(2)
        good = (ca and cw and
                p.obj_info(ca)[10] == (p.VREAD | p.VMETA) and
                p.obj_info(cw)[10] == (p.VWRITE | p.VMETA))
        if good:
            try:
                p.pipewrite(ca, b'x')
            except OSError:
                pass
            else:
                good = False
        if good:
            try:
                p.piperead(cw, 1)
            except OSError:
                pass
            else:
                good = False
        p.exits('' if good else 't21-piperights')
    waited, status = p.wait()
    ok(waited == pid and status == '',
       'RFORKEX refuses read/write outside the inherited Pipe rights')
    p.pipeclose(a)
    p.pipeclose(w)

    # --- OSPAWN: explicit multiple-Handle inheritance ---------------------
    oa = b'T21-SPAWN-A'
    ob = b'T21-SPAWN-B'
    ba = p.obj_create(oa)
    bb = p.obj_create(ob)
    child = (
        "import plan9 as p\n"
        "def find(d):\n"
        " for h in p.obj_list():\n"
        "  try: i=p.obj_info(h)\n"
        "  except OSError: continue\n"
        "  if i[8]==p.VKIND_BYTES and i[3]==len(d) and p.obj_read(h,len(d))==d:\n"
        "   return h\n"
        " return 0\n"
        "a=find(b'T21-SPAWN-A'); b=find(b'T21-SPAWN-B')\n"
        "assert a and b\n"
        "assert p.obj_info(a)[10]==(p.VREAD|p.VMETA)\n"
        "assert p.obj_info(b)[10]==(p.VREAD|p.VMETA|p.VSEND)\n")
    pid = space.spawn(OID, handle=sh, argv=('micropython', '-c', child),
                      vhandles=((ba, p.VREAD | p.VMETA),
                                (bb, p.VREAD | p.VMETA | p.VSEND)))
    waited, status = space.wait()
    ok(waited == pid and status == '',
       'OSPAWN inherits multiple Handles with attenuated rights')

    # --- OSPAWN: failed planning leaves the parent unchanged --------------
    holds = p.obj_info(ba)[4]
    denied(lambda: space.spawn(OID, handle=sh,
                               argv=('micropython', '-c', 'pass'),
                               vhandles=((ba, 0xffff),)),
           'OSPAWN excessive rights')
    denied(lambda: space.spawn(OID, handle=sh,
                               argv=('micropython', '-c', 'pass'),
                               vhandles=((0, 0),)),
           'OSPAWN invalid Handle')
    ok(p.obj_info(ba)[4] == holds and p.obj_read(ba, len(oa)) == oa,
       'rejected OSPAWN plans leave the parent unchanged')

    # --- OSPAWN: a post-commit failure rolls the child back ---------------
    a, w = p.pipenew()
    base = p.obj_info(w)[4]
    denied(lambda: space.spawn(OID, handle=sh,
                               argv=('micropython', '-c', 'pass'),
                               pipes=((w, 2),), view=1,
                               roles=('t21-no-such-role',)),
           'OSPAWN role failure after Handle commit')
    ok(p.obj_info(w)[4] == base,
       'post-commit OSPAWN failure rolls back inherited references')
    p.pipeclose(a)
    p.pipeclose(w)

    # --- OSPAWN: stdoutpipe is remapped to the child Handle ---------------
    a, w = p.pipenew()
    pid = space.spawn(OID, handle=sh,
                      argv=('micropython', '-c',
                            "print('T21-STDOUT-OK')\n"),
                      pipes=((w, 2),), stdout=w)
    p.pipeclose(w)
    out = b''
    while True:
        part = p.piperead(a, 64)
        if not part:
            break
        out += part
    ok(b'T21-STDOUT-OK' in out,
       'OSPAWN remaps stdoutpipe to the assigned child Handle')
    waited, status = space.wait()
    ok(waited == pid and status == '', 'OSPAWN stdout child exits cleanly')
    p.pipeclose(a)

    # --- Terminating a child releases its inherited references ------------
    a, w = p.pipenew()
    base = p.obj_info(w)[4]
    pid = space.spawn(OID, handle=sh,
                      argv=('micropython', '-c',
                            'import plan9 as p\np.sleep(60000)\n'),
                      pipes=((w, 2),))
    ok(p.obj_info(w)[4] == base + 1,
       'OSPAWN child holds the inherited Pipe reference')
    space.terminate(pid)
    waited, status = space.wait()
    ok(waited == pid, 'terminated child is reaped')
    ok(p.obj_info(w)[4] == base,
       'terminated child releases its inherited Pipe reference')
    p.pipeclose(a)
    p.pipeclose(w)

    p.obj_close(reused)
    p.obj_close(ba)
    p.obj_close(bb)
    p.obj_close(b1)
    p.obj_close(b2)
    p.obj_close(b)
    p.obj_close(sh)
    print('T21-FINISH', n[0], 'PASS')
    return 1
