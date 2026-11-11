"""Userland acceptance test for Phase 2 capability-mapped regions.

The seeded init module runs once on a fresh O image.  It creates sibling
processes through the normal SPAWN path; their only relationship is the
explicit ObjectFIFO capability channel used to deliver the region Handle.
Every assertion prints T38-PASS after it passes, and the runner requires one
final sequential T38-FINISH record.
"""

CID = 0x4f45584543000003

COMMON = r'''
import plan9 as p

def _fifo():
    for h in p.obj_list():
        try:
            if p.obj_info(h)[8] == p.VKIND_FIFO:
                return h
        except OSError:
            pass
    raise AssertionError('no FIFO handle')

f = _fifo()
sub = p.obj_subscribe(f, p.VRECV | p.VMETA, 64)

def _send(tag, obj=None):
    if obj is None:
        obj = p.obj_create(b'.')
    p.obj_publish(f, obj, tag)

def _recv(tag, timeout=-1):
    while True:
        h, env = p.obj_receive(sub, timeout, 64)
        if env == tag:
            return h
        p.obj_close(h)

'''

# A is the single writer.  It maps RW, publishes an attenuated reader
# Handle, observes B's reads, changes the same backing, and tests that a
# second writable mapping is rejected while its own is active.
A_MAIN = COMMON + r'''
reg = p.region_create(4096)
rw = p.region_map(reg)
p.region_write(rw, b'HELLO-REGION!')
try:
    p.region_map(reg)
except OSError:
    pass
else:
    p.exits('A:second-writer-allowed')
rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
try:
    p.obj_open_handle(rd, p.VWRITE)
except OSError:
    pass
else:
    p.exits('A:reader-gained-write')
p.obj_publish(f, rd, b'A:REGION')
p.obj_close(rd)
h = _recv(b'B:READ1')
p.obj_close(h)
p.region_write(rw, b'WORLD-REGION!')
_send(b'A:UPDATE', p.obj_create(b'u'))
h = _recv(b'B:READ2')
p.obj_close(h)
_send(b'A:FINISH', p.obj_create(b'f'))
h = _recv(b'B:ACK')
p.obj_close(h)
p.region_write(rw, b'AFTER-B-EXIT!!')
if p.region_read(rw, 14) != b'AFTER-B-EXIT!!':
    p.exits('A:writer-broken-after-reader')
p.region_unmap(rw)
p.obj_close(reg)
p.exits('')
'''

# B is a sibling, not a child of A.  It obtains the reader capability only
# through the explicit FIFO delivery, maps the same pages at a different
# process-local address, and finally makes a real user-mode store into the
# SG_RONLY mapping, which must fault.
B_MAIN = COMMON + r'''
h = _recv(b'A:REGION')
reg = h
ro = p.region_map(reg, 0x20000000)
if p.region_read(ro, 13) != b'HELLO-REGION!':
    p.exits('B:first-read')
_send(b'B:READ1', p.obj_create(b'1'))
h = _recv(b'A:UPDATE')
p.obj_close(h)
if p.region_read(ro, 13) != b'WORLD-REGION!':
    p.exits('B:second-read')
_send(b'B:READ2', p.obj_create(b'2'))
h = _recv(b'A:FINISH')
p.obj_close(h)
_send(b'B:ACK', p.obj_create(b'a'))
p.region_write(ro, b'X')
p.exits('B:ro-write-allowed')
'''

# Writer exits before the reader maps.  The region Handle delivered through
# the FIFO must keep the backing alive.
A_EXIT = COMMON + r'''
reg = p.region_create(4096)
rw = p.region_map(reg)
p.region_write(rw, b'WRITER-GONE!')
rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
p.obj_publish(f, rd, b'A:REGION')
p.exits('')
'''

B_EXIT = COMMON + r'''
h = _recv(b'A:REGION')
p.sleep(1000)
ro = p.region_map(h, 0x20000000)
if p.region_read(ro, 12) != b'WRITER-GONE!':
    p.exits('B2:writer-exit-data')
_send(b'B2:OK', p.obj_create(b'k'))
p.exits('')
'''

# Reader unmaps and exits before the writer finishes.  The writer must remain
# fully functional.
A_READER_EXIT = COMMON + r'''
reg = p.region_create(4096)
rw = p.region_map(reg)
p.region_write(rw, b'BEFORE-READER')
rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
p.obj_publish(f, rd, b'A:REGION')
h = _recv(b'B:EXIT')
p.obj_close(h)
p.region_write(rw, b'AFTER-READER!!')
if p.region_read(rw, 14) != b'AFTER-READER!!':
    p.exits('A3:writer-broken-after-reader-exit')
p.region_unmap(rw)
p.obj_close(reg)
p.exits('')
'''

B_READER_EXIT = COMMON + r'''
h = _recv(b'A:REGION')
ro = p.region_map(h, 0x20000000)
if p.region_read(ro, 13) != b'BEFORE-READER':
    p.exits('B3:initial-read')
p.region_unmap(ro)
_send(b'B:EXIT', p.obj_create(b'e'))
p.obj_close(h)
p.exits('')
'''

C_UNAUTHORIZED = r'''
import plan9 as p
for h in (1, 2, 3, 0x12345678):
    try:
        p.obj_info(h)
    except OSError:
        pass
    else:
        p.exits('C:guessed-handle')
try:
    p.region_map(1)
except OSError:
    pass
else:
    p.exits('C:mapped-guessed-handle')
try:
    p.region_unmap(0x20000000)
except OSError:
    pass
else:
    p.exits('C:unmapped-guessed-region')
p.exits('')
'''


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
        print('T38-PASS', n[0], name)

    def denied(call, name):
        try:
            call()
        except OSError:
            return
        raise AssertionError('expected rejection: ' + name)

    def waitmap(pids):
        got = {}
        for h in pids:
            r = p.process_wait(h, -1)
            got[h] = r[1]
            p.obj_close(h)
        return got

    def reap(h):
        r = p.process_wait(h, -1)
        return h, r[1]

    def spawn(code, fifo=None):
        vh = ()
        if fifo is not None:
            vh = ((fifo, p.VSEND | p.VSUBSCRIBE | p.VMETA | p.VDELEGATE),)
        return space.spawn(CID, handle=sh, argv=('micropython', '-c', code),
                           vhandles=vh)

    sh = p.obj_open_space('init')

    # --- local validation and lifetime -----------------------------------
    denied(lambda: p.region_create(0), 'zero-size region')
    denied(lambda: p.region_create(4095), 'unaligned region size')
    denied(lambda: p.region_create(16 * 1024 * 1024 + 4096),
           'oversized region')

    reg = p.region_create(4096)
    base = p.obj_info(reg)[4]
    denied(lambda: p.obj_read(reg, 1), 'region is not a byte object')
    denied(lambda: p.region_map(p.obj_create(b'x')), 'wrong Handle kind')
    denied(lambda: p.region_map(reg, 123), 'misaligned requested address')
    denied(lambda: p.region_map(reg, 0x7ffffffff000),
           'out-of-range requested address')
    denied(lambda: p.region_unmap(0x30000000), 'unmap of unmapped address')

    addr = p.region_map(reg, 0x30000000)
    ok(addr == 0x30000000, 'explicit region mapping address is honored')
    ok(p.obj_info(reg)[4] == base + 1,
       'a mapping holds one independent region reference')
    p.region_write(addr, b'persist-after-close')
    p.obj_close(reg)
    ok(p.region_read(addr, 19) == b'persist-after-close',
       'mapping survives closing its Handle')
    denied(lambda: p.seg_free(addr, 4096), 'SEGFREE cannot split a region')
    denied(lambda: p.seg_detach(addr), 'SEGDETACH cannot remove a region')
    denied(lambda: p.region_map(reg), 'stale Handle after close')
    p.region_unmap(addr)
    ok(True, 'mapping unmaps after its Handle is gone')
    denied(lambda: p.region_unmap(addr), 'double unmap is rejected')

    reg = p.region_create(4096)
    addr = p.region_map(reg)
    denied(lambda: p.region_map(reg, 0x30000000),
           'second simultaneous writer mapping is denied')
    p.region_unmap(addr)
    p.obj_close(reg)
    ok(True, 'writer slot is released by unmap')

    # --- fork and Handle delegation cannot add a second writer -----------
    reg = p.region_create(4096)
    rw = p.region_map(reg)
    p.region_write(rw, b'FORK-BASE')
    pid, _ = p.rforkex(p.RFPROC,
                       ((reg, p.VREAD | p.VWRITE | p.VMETA |
                              p.VDELEGATE, 1),))
    if pid == 0:
        child = 0
        for h in p.obj_list():
            if (h & 0xff) == 1:
                child = h
        try:
            p.region_map(child)
        except OSError:
            p.exits('')
        p.exits('fork-second-writer')
    waited, status = reap(pid)
    ok(waited == pid and status == '',
       'delegated writer Handle cannot map while parent writer is active')
    ok(p.region_read(rw, 9) == b'FORK-BASE',
       'rejected fork mapping leaves the parent backing intact')
    p.region_unmap(rw)
    p.obj_close(reg)

    # --- revocation blocks only future mappings -------------------------
    reg = p.region_create(4096)
    rw = p.region_map(reg)
    p.region_write(rw, b'PRE-REVOKE')
    reader = p.obj_open_handle(reg,
                               p.VREAD | p.VMETA | p.VDELEGATE)
    ro = p.region_map(reader, 0x31000000)
    denied(lambda: p.obj_revoke(reader),
           'read-only delegated Handle cannot revoke')
    p.obj_revoke(reg)
    denied(lambda: p.region_map(reader, 0x32000000),
           'new mapping through delegated Handle after revoke')
    denied(lambda: p.region_map(reg, 0x33000000),
           'new mapping through creator Handle after revoke')
    ok(p.region_read(ro, 10) == b'PRE-REVOKE',
       'installed RO mapping survives revocation')
    p.region_write(rw, b'POST-REVOKE')
    ok(p.region_read(ro, 11) == b'POST-REVOKE',
       'installed writer mapping remains live after revocation')
    p.region_unmap(ro)
    p.region_unmap(rw)
    p.obj_close(reader)
    p.obj_close(reg)
    ok(True, 'revocation leaves existing mappings and page lifetime intact')

    # --- ordinary anonymous memory is unaffected by region machinery ----
    anon = p.seg_attach('memory', 8192)
    if p.region_read(anon, 16) != b'\x00' * 16:
        raise AssertionError('anonymous memory is not zero-initialized')
    p.region_write(anon, b'ANON-KEEP')
    reg = p.region_create(4096)
    rw = p.region_map(reg)
    p.region_write(rw, b'region')
    p.region_unmap(rw)
    p.obj_close(reg)
    ok(p.region_read(anon, 9) == b'ANON-KEEP',
       'ordinary anonymous BSS/data memory keeps its zeroing and data')
    p.seg_detach(anon)

    # --- repeated create/map/unmap/close reclaims cleanly ----------------
    reg = p.region_create(4096)
    rw = p.region_map(reg)
    p.region_unmap(rw)
    p.obj_close(reg)
    free0 = p.system()[6]
    for _ in range(32):
        reg = p.region_create(4096)
        rw = p.region_map(reg)
        p.region_write(rw, b'reclaim')
        if p.region_read(rw, 7) != b'reclaim':
            raise AssertionError('reclaim round trip')
        p.region_unmap(rw)
        p.obj_close(reg)
    free1 = p.system()[6]
    if free0 - free1 > 4096:
        raise AssertionError('region accounting lost %d bytes: %d -> %d' %
                             (free0 - free1, free0, free1))
    ok(True, 'repeated create/map/unmap/close returns physical pages '
       '(heap/Pte overhead <= 1 page)')

    # --- failed installation rolls back object and page accounting -------
    fill = []
    while True:
        try:
            fill.append(p.obj_create(b'x'))
        except OSError:
            break
    free0 = p.system()[6]
    for _ in range(8):
        denied(lambda: p.region_create(4096),
               'region creation with a full Handle table')
    free1 = p.system()[6]
    if free0 - free1 > 4096:
        raise AssertionError('failed creation lost %d bytes: %d -> %d' %
                             (free0 - free1, free0, free1))
    ok(True, 'failed region creation releases its pages and object accounting')
    for h in fill:
        p.obj_close(h)
    reg = p.region_create(4096)
    p.obj_close(reg)
    ok(True, 'Handle table remains usable after failed creation rollback')

    # --- normal-exit child lifecycle accounting baseline -----------------
    freec0 = p.system()[6]
    c1 = spawn(C_UNAUTHORIZED)
    c2 = spawn(C_UNAUTHORIZED)
    got = waitmap((c1, c2))
    p.sleep(1000)
    freec1 = p.system()[6]
    ctrl = freec0 - freec1
    fifo = p.obj_fifocreate(64)
    b = spawn(B_MAIN, fifo)
    p.sleep(2000)
    a = spawn(A_MAIN, fifo)
    got = waitmap((a, b))
    ok(got[a] == '', 'writer process completes its post-reader work')
    ok('fault' in got[b], 'RO store faults and terminates only the reader '
       '(status=%r)' % (got[b],))
    p.obj_close(fifo)
    ok(True, 'explicit FIFO delivery authorizes the sibling reader')

    # --- writer exits before reader --------------------------------------
    free0 = p.system()[6]
    fifo = p.obj_fifocreate(64)
    b = spawn(B_EXIT, fifo)
    p.sleep(2000)
    a = spawn(A_EXIT, fifo)
    got = waitmap((a, b))
    p.sleep(1000)
    ok(got[a] == '' and got[b] == '',
       'reader maps and reads retained data after writer exit')
    p.obj_close(fifo)
    free1 = p.system()[6]
    if free0 - free1 > ctrl + 65536:
        raise AssertionError('writer-exit order lost %d bytes (control %d)' %
                             (free0 - free1, ctrl))
    ok(True, 'writer-exit order matches normal child lifecycle accounting')

    # --- reader exits before writer --------------------------------------
    free0 = p.system()[6]
    fifo = p.obj_fifocreate(64)
    b = spawn(B_READER_EXIT, fifo)
    p.sleep(2000)
    a = spawn(A_READER_EXIT, fifo)
    got = waitmap((a, b))
    p.sleep(1000)
    ok(got[a] == '' and got[b] == '',
       'writer remains functional after reader exit')
    p.obj_close(fifo)
    free1 = p.system()[6]
    if free0 - free1 > ctrl + 65536:
        raise AssertionError('reader-exit order lost %d bytes (control %d)' %
                             (free0 - free1, ctrl))
    ok(True, 'reader-exit order matches normal child lifecycle accounting')

    # --- ungranted third process -----------------------------------------
    c = spawn(C_UNAUTHORIZED)
    got = waitmap((c,))
    ok(got[c] == '', 'ungranted process cannot guess a Handle or address')

    p.obj_close(sh)
    print('T38-FINISH', n[0], 'PASS')
    return 1
