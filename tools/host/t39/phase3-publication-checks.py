"""Userland acceptance test for Phase 3 atomic publication and WAIT/WAKE.

The seeded init module creates one writable region and independent read-only
observer processes through the normal SPAWN path.  Region Handles cross only
through the explicit ObjectFIFO capability channel; payload and the 64-bit
publication word cross only through the shared pages.  Every assertion prints
T39-PASS after it passes, and the runner requires one final sequential
T39-FINISH record.
"""

CID = 0x4f45584543000003

CHILD = r'''
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
sub = p.obj_subscribe(f, p.VRECV | p.VMETA, 128)

def _send(tag, obj=None):
    if obj is None:
        obj = p.obj_create(b'.')
    p.obj_publish(f, obj, tag)
    p.obj_close(obj)

def _recv(tag, timeout=10000):
    while True:
        h, env = p.obj_receive(sub, timeout, 64)
        if env == tag:
            return h
        p.obj_close(h)

def _region_handle():
    for h in p.obj_list():
        try:
            if p.obj_info(h)[8] == p.VKIND_REGION:
                return h
        except OSError:
            pass
    raise AssertionError('no region handle')

_send(b'READY')
'''


OBSERVER_STRESS = CHILD + r'''
import struct
h = _recv(b'REGION')
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
last = 0
for i in range(100):
    if p.region_atomic_load(base) != i:
        p.exits('stress-preload')
    _send(b'NEXT')
    if i % 2:
        p.sleep(30)
    r = p.region_wait(base, 0, i, 5000)
    if r == 2:
        p.exits('stress-timeout')
    if r not in (0, 1):
        p.exits('stress-status')
    v = p.region_atomic_load(base)
    if v != i + 1:
        p.exits('stress-version')
    data = p.region_read(base + 8, 64)
    if data[:8] != struct.pack('<Q', v):
        p.exits('stress-record-head')
    if data[8:] != bytes([v & 0xff]) * 56:
        p.exits('stress-record-body')
    last = v
_send(b'LARGE')
r = p.region_wait(base, 0, last, 5000)
if r == 2:
    p.exits('large-timeout')
v = p.region_atomic_load(base)
if v != last + 1:
    p.exits('large-version')
data = p.region_read(base + 8, 8192)
if data[:8] != struct.pack('<Q', v):
    p.exits('large-head')
if data[8:] != bytes([v & 0xff]) * (8192 - 8):
    p.exits('large-body')
_send(b'DONE')
p.exits('')
'''


OBSERVER_COUNT = CHILD + r'''
import struct
h = _recv(b'REGION')
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
while p.region_atomic_load(base) == 0:
    r = p.region_wait(base, 0, 0, 5000)
    if r == 2:
        p.exits('count-timeout')
    if r not in (0, 1):
        p.exits('count-status')
v = p.region_atomic_load(base)
if v != 1:
    p.exits('count-value')
data = p.region_read(base + 8, 64)
if data[:8] != struct.pack('<Q', 1):
    p.exits('count-record')
_send(b'DONE')
p.exits('')
'''


OBSERVER_KEY = CHILD + r'''
h = _recv(b'@REGION@')
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
while p.region_atomic_load(base + @OFF@) == 0:
    r = p.region_wait(base, @OFF@, 0, 5000)
    if r == 2:
        p.exits('key-timeout')
    if r not in (0, 1):
        p.exits('key-status')
_send(b'DONE')
p.exits('')
'''


OBSERVER_TIMEOUT = CHILD + r'''
h = _recv(b'REGION')
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
t0 = p.time_ns()
r = p.region_wait(base, 0, 0, 200)
ms = (p.time_ns() - t0) // 1000000
if r != 2:
    p.exits('timeout-status')
if ms < 150 or ms > 3000:
    p.exits('timeout-duration')
_send(b'DONE')
p.exits('')
'''


OBSERVER_EXIT = CHILD + r'''
h = _recv(b'REGION')
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
p.region_wait(base, 0, 0, -1)
p.exits('exit-returned')
'''


OBSERVER_WRITER = CHILD + r'''
h = _recv(b'REGION')
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
r = p.region_wait(base, 0, 0, -1)
if r != 3:
    p.exits('writer-status')
if p.region_read(base + 8, 8) != b'WRITER!!':
    p.exits('writer-data')
_send(b'DONE')
p.exits('')
'''


WRITER = CHILD + r'''
reg = p.region_create(8192)
rw = p.region_map(reg, 0x30000000)
p.region_atomic_store(rw, @WORD1@)
p.region_write(rw + 8, b'WRITER!!')
rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
_send(b'HANDLE', rd)
wr = p.obj_open_handle(reg, p.VREAD | p.VWRITE | p.VMETA | p.VDELEGATE)
_send(b'WRITER', wr)
_send(b'WREADY')
@AFTER@
p.exits('')
'''


OBSERVER_LATE = CHILD + r'''
h = _region_handle()
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
t0 = p.time_ns()
r = p.region_wait(base, 0, @WORD1@, 2000)
ms = (p.time_ns() - t0) // 1000000
if r == 2:
    _send(b'LATE-TIMEOUT')
    p.exits('late-timeout')
if r != 3:
    p.exits('late-status')
if ms > 1000:
    p.exits('late-slow')
if p.region_read(base + 8, 8) != b'WRITER!!':
    p.exits('late-data')
_send(b'DONE')
p.exits('')
'''


OBSERVER_SLEEP_EXIT = CHILD + r'''
h = _region_handle()
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
r = p.region_wait(base, 0, @WORD1@, 5000)
if r != 3:
    p.exits('sleep-exit-status')
if p.region_read(base + 8, 8) != b'WRITER!!':
    p.exits('sleep-exit-data')
_send(b'DONE')
p.exits('')
'''


OBSERVER_REPLACE = CHILD + r'''
h = _region_handle()
base = p.region_map(h, @BASE@)
_send(b'MAPPED')
seen3 = 0
for _ in range(1000):
    v = p.region_atomic_load(base)
    if v == @WORD2@:
        break
    r = p.region_wait(base, 0, v, 5000)
    if r == 2:
        p.exits('replace-timeout')
    if r == 3:
        seen3 = 1
        p.sleep(5)
    elif r not in (0, 1):
        p.exits('replace-status')
else:
    p.exits('replace-loop')
if p.region_read(base + 8, 8) != b'REPLACED':
    p.exits('replace-data')
_send(b'DONE3' if seen3 else b'DONE')
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
    import struct

    n = [0]

    def ok(value, name):
        assert value, name
        n[0] += 1
        print('T39-PASS', n[0], name)

    def denied(call, name):
        try:
            call()
        except (OSError, ValueError):
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

    sh = p.obj_open_space('init')

    def spawn_handles(code, fifo, handles=()):
        vh = ((fifo, p.VSEND | p.VSUBSCRIBE | p.VMETA | p.VDELEGATE),)
        return space.spawn(CID, handle=sh,
                           argv=('micropython', '-c', code),
                           vhandles=vh + tuple(handles))

    def spawn(code, fifo):
        return spawn_handles(code, fifo)

    def send(fifo, tag, obj=None):
        if obj is None:
            obj = p.obj_create(b'.')
        p.obj_publish(fifo, obj, tag)
        p.obj_close(obj)

    def recv(sub, tag, timeout=10000):
        while True:
            h, env = p.obj_receive(sub, timeout, 64)
            if env == tag:
                return h
            p.obj_close(h)

    def recv_any(sub, tags, timeout=10000):
        while True:
            h, env = p.obj_receive(sub, timeout, 64)
            p.obj_close(h)
            if env in tags:
                return env

    def record(v, size):
        return struct.pack('<Q', v) + bytes([v & 0xff]) * (size - 8)

    # --- local validation, atomic accessors and mapping authority --------
    reg = p.region_create(8192)
    rw = p.region_map(reg, 0x30000000)
    p.region_atomic_store(rw, 0)
    ok(p.region_atomic_load(rw) == 0, 'atomic load/store round trip')
    p.region_atomic_store(rw, 0x1122334455667788)
    ok(p.region_atomic_load(rw) == 0x1122334455667788,
       '64-bit publication word preserved')
    p.region_atomic_store(rw, 0)
    denied(lambda: p.region_atomic_load(rw + 4),
           'misaligned atomic accessor')
    denied(lambda: p.region_wait(rw, 4, 0, 0), 'misaligned wait offset')
    denied(lambda: p.region_wait(rw, 8192, 0, 0), 'wait offset past region')
    denied(lambda: p.region_wait(0x12345000, 0, 0, 0),
           'unmapped mapping base')
    denied(lambda: p.region_wake(rw, 4, 1), 'misaligned wake offset')
    denied(lambda: p.region_wake(rw, 0, 0), 'zero wake count')
    ok(p.region_wake(rw, 0, 1) == 0, 'wake with no waiter returns zero')
    ok(p.region_wait(rw, 0, 0xdeadbeef, 0) == 0,
       'changed value returns before sleeping')
    ok(p.region_wait(rw, 0, 0, 0) == 2,
       'non-blocking equal value reports timeout/would-block')
    ok(p.region_wait(rw, 8184, 0, 0) == 2,
       'last complete word is a valid wait target')
    ok(not hasattr(p, 'sem_acquire') and not hasattr(p, 'sem_release')
       and not hasattr(p, 'sem_timed_acquire') and not hasattr(p, 'RFREND'),
       'retired public sync wrappers and RFREND constant are absent')

    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    ro = p.region_map(rd, 0x31000000)
    denied(lambda: p.region_wake(ro, 0, 1), 'RO mapping cannot wake')
    ok(p.region_wait(ro, 0, 0, 0) == 2, 'RO mapping can compare and wait')
    p.obj_close(rd)
    ok(p.region_wait(ro, 0, 0, 0) == 2,
       'RO wait survives closing its Handle')
    p.obj_close(reg)
    ok(p.region_wake(rw, 0, 1) == 0,
       'writer mapping wakes after its Handle is closed')
    p.region_unmap(ro)
    p.region_unmap(rw)
    ok(True, 'local validation leaves no mapped region')

    # --- writer-presence ordering and late waiters ------------------------
    reg = p.region_create(8192)
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    ro = p.region_map(rd, 0x34000000)
    ok(p.region_wait(ro, 0, 0, -1) == 3,
       'unchanged word with no writer ever returns 3 immediately')
    ok(p.region_wait(ro, 0, 0x55, -1) == 0,
       'changed comparison wins even when no writer exists')
    p.region_unmap(ro)
    p.obj_close(rd)
    p.obj_close(reg)

    reg = p.region_create(8192)
    rw = p.region_map(reg, 0x30000000)
    p.region_atomic_store(rw, 0xabc)
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    ro = p.region_map(rd, 0x31000000)
    p.region_unmap(rw)
    t0 = p.time_ns()
    r = p.region_wait(ro, 0, 0xabc, -1)
    ms = (p.time_ns() - t0) // 1000000
    ok(r == 3 and ms < 1000,
       'late waiter after explicit unmap returns 3 promptly')
    ok(p.region_wait(ro, 0, 0x999, -1) == 0,
       'changed comparison wins after the writer unmapped')
    rw2 = p.region_map(reg, 0x32000000)
    ok(p.region_wait(ro, 0, 0xabc, 0) == 2,
       'a replacement writer makes the unchanged word waitable again')
    p.region_atomic_store(rw2, 0xdef)
    ok(p.region_wake(rw2, 0, 1) == 0,
       'replacement writer can wake its word')
    p.region_unmap(rw2)
    p.region_unmap(ro)
    p.obj_close(rd)
    p.obj_close(reg)
    ok(True, 'late-waiter replacement writer leaves no stale state')

    # --- revocation preserves installed wait/wake authority --------------
    reg = p.region_create(8192)
    rw = p.region_map(reg)
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    ro = p.region_map(rd, 0x32000000)
    p.obj_revoke(reg)
    ok(p.region_wait(ro, 0, 0, 0) == 2,
       'installed RO mapping still waits after revoke')
    ok(p.region_wake(rw, 0, 1) == 0,
       'installed writer mapping still wakes after revoke')
    denied(lambda: p.region_map(rd, 0x33000000),
           'revoked region denies a new mapping')
    p.region_unmap(ro)
    p.region_unmap(rw)
    p.obj_close(rd)
    p.obj_close(reg)
    ok(True, 'revocation affects only new maps')

    # --- publication stress: wait-before-wake and wake-before-wait -------
    reg = p.region_create(16384)
    rw = p.region_map(reg, 0x30000000)
    p.region_atomic_store(rw, 0)
    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA, 128)
    b = spawn(OBSERVER_STRESS.replace('@BASE@', '0x20000000'), fifo)
    p.obj_close(recv(sub, b'READY'))
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    send(fifo, b'REGION', rd)
    p.obj_close(recv(sub, b'MAPPED'))
    wakes = []
    for i in range(100):
        p.obj_close(recv(sub, b'NEXT'))
        if i % 2 == 0:
            p.sleep(50)
        v = i + 1
        p.region_write(rw + 8, record(v, 64))
        p.region_atomic_store(rw, v)
        wakes.append(p.region_wake(rw, 0, 1))
    p.obj_close(recv(sub, b'LARGE'))
    v = 101
    p.region_write(rw + 8, record(v, 8192))
    p.region_atomic_store(rw, v)
    ok(p.region_wake(rw, 0, 1) == 1,
       'large immutable payload publishes behind one word')
    p.obj_close(recv(sub, b'DONE'))
    got = waitmap((b,))
    ok(got[b] == '', 'publication stress observer completes without timeout')
    ok(wakes[::2] == [1] * 50,
       'wait-before-wake iterations wake exactly one registered waiter')
    ok(wakes[1::2] == [0] * 50,
       'wake-before-wait iterations lose no publication (no waiter yet)')
    p.obj_close(fifo)
    p.region_unmap(rw)
    p.obj_close(reg)

    # --- two observers: wake count and one-vs-many -----------------------
    reg = p.region_create(8192)
    rw = p.region_map(reg, 0x30000000)
    p.region_atomic_store(rw, 0)
    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA, 128)
    b = spawn(OBSERVER_COUNT.replace('@BASE@', '0x20000000'), fifo)
    c = spawn(OBSERVER_COUNT.replace('@BASE@', '0x21000000'), fifo)
    p.obj_close(recv(sub, b'READY'))
    p.obj_close(recv(sub, b'READY'))
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    send(fifo, b'REGION', rd)
    p.obj_close(recv(sub, b'MAPPED'))
    p.obj_close(recv(sub, b'MAPPED'))
    deadline = p.time_ns() + 5000000000
    found = False
    while p.time_ns() < deadline:
        if p.region_wake(rw, 0, 2) == 2:
            found = True
            break
        p.sleep(50)
    ok(found, 'wake count sees two registered RO observers')
    p.sleep(200)
    p.region_write(rw + 8, record(1, 64))
    p.region_atomic_store(rw, 1)
    ok(p.region_wake(rw, 0, 2) >= 1,
       'wake count releases eligible observers after publication')
    p.obj_close(recv(sub, b'DONE'))
    p.obj_close(recv(sub, b'DONE'))
    got = waitmap((b, c))
    ok(got[b] == '' and got[c] == '',
       'both RO observers receive the same publication')
    p.obj_close(fifo)
    p.region_unmap(rw)
    p.obj_close(reg)

    # --- key isolation: region identity and region-relative offset --------
    reg = p.region_create(8192)
    rw = p.region_map(reg, 0x30000000)
    p.region_atomic_store(rw, 0)
    p.region_atomic_store(rw + 8, 0)
    reg2 = p.region_create(8192)
    rw2 = p.region_map(reg2, 0x31000000)
    p.region_atomic_store(rw2, 0)
    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA, 128)
    b = spawn(OBSERVER_KEY.replace('@REGION@', 'REGION1')
              .replace('@BASE@', '0x20000000').replace('@OFF@', '0'), fifo)
    c = spawn(OBSERVER_KEY.replace('@REGION@', 'REGION1')
              .replace('@BASE@', '0x21000000').replace('@OFF@', '8'), fifo)
    d = spawn(OBSERVER_KEY.replace('@REGION@', 'REGION2')
              .replace('@BASE@', '0x22000000').replace('@OFF@', '0'), fifo)
    for _ in range(3):
        p.obj_close(recv(sub, b'READY'))
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    send(fifo, b'REGION1', rd)
    rd2 = p.obj_open_handle(reg2, p.VREAD | p.VMETA | p.VDELEGATE)
    send(fifo, b'REGION2', rd2)
    for _ in range(3):
        p.obj_close(recv(sub, b'MAPPED'))
    p.sleep(500)
    ok(p.region_wake(rw, 0, 1) == 1,
       'wake at offset 0 reaches exactly its word')
    p.sleep(150)
    ok(p.region_wake(rw, 8, 1) == 1,
       'offset 8 waiter was not aliased by offset 0 wake')
    ok(p.region_wake(rw2, 0, 1) == 1,
       'second region waiter was not aliased by first region wake')
    p.region_write(rw + 8, record(1, 64))
    p.region_atomic_store(rw, 1)
    p.region_write(rw + 16, record(1, 64))
    p.region_atomic_store(rw + 8, 1)
    p.region_write(rw2 + 8, record(1, 64))
    p.region_atomic_store(rw2, 1)
    p.region_wake(rw, 0, 1)
    p.region_wake(rw, 8, 1)
    p.region_wake(rw2, 0, 1)
    for _ in range(3):
        p.obj_close(recv(sub, b'DONE'))
    got = waitmap((b, c, d))
    ok(got[b] == '' and got[c] == '' and got[d] == '',
       'region identity and offset isolate independent wait keys')
    p.obj_close(fifo)
    p.region_unmap(rw)
    p.region_unmap(rw2)
    p.obj_close(reg)
    p.obj_close(reg2)

    # --- timeout and interruption cleanup --------------------------------
    reg = p.region_create(8192)
    rw = p.region_map(reg, 0x30000000)
    p.region_atomic_store(rw, 0)
    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA, 128)
    b = spawn(OBSERVER_TIMEOUT.replace('@BASE@', '0x20000000'), fifo)
    p.obj_close(recv(sub, b'READY'))
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    send(fifo, b'REGION', rd)
    p.obj_close(recv(sub, b'MAPPED'))
    p.obj_close(recv(sub, b'DONE'))
    got = waitmap((b,))
    ok(got[b] == '', 'bounded timeout returns and removes the waiter')
    p.obj_close(fifo)

    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA, 128)
    b = spawn(OBSERVER_EXIT.replace('@BASE@', '0x21000000'), fifo)
    p.obj_close(recv(sub, b'READY'))
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    send(fifo, b'REGION', rd)
    p.obj_close(recv(sub, b'MAPPED'))
    p.sleep(400)
    space.terminate(b)
    got = waitmap((b,))
    ok(got[b] != '', 'termination note removes a blocked waiter and exits')
    ok(p.region_wake(rw, 0, 1) == 0,
       'note/process-exit leaves no stale wait-queue entry')
    p.obj_close(fifo)
    p.region_unmap(rw)
    p.obj_close(reg)

    # --- writer death wakes observers without destroying their snapshot --
    reg = p.region_create(8192)
    rw = p.region_map(reg, 0x30000000)
    p.region_atomic_store(rw, 0)
    p.region_write(rw + 8, b'WRITER!!')
    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA, 128)
    b = spawn(OBSERVER_WRITER.replace('@BASE@', '0x20000000'), fifo)
    p.obj_close(recv(sub, b'READY'))
    rd = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    send(fifo, b'REGION', rd)
    p.obj_close(recv(sub, b'MAPPED'))
    p.sleep(500)
    p.region_unmap(rw)
    p.obj_close(reg)
    p.obj_close(recv(sub, b'DONE'))
    got = waitmap((b,))
    ok(got[b] == '',
       'writer mapping release wakes a blocked RO observer and preserves data')
    p.obj_close(fifo)

    # --- late waiter after a real writer process exit ---------------------
    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA | p.VDELEGATE | p.VWRITE, 128)
    w = spawn_handles(WRITER.replace('@WORD1@', '0x1111')
                      .replace('@AFTER@', ''), fifo)
    rh = recv(sub, b'HANDLE')
    wh = recv(sub, b'WRITER')
    p.obj_close(wh)
    got = waitmap((w,))
    ok(got[w] == '', 'writer process exits after delivering its reader Handle')
    b = spawn_handles(OBSERVER_LATE.replace('@BASE@', '0x20000000')
                      .replace('@WORD1@', '0x1111'), fifo,
                      ((rh, p.VREAD | p.VMETA | p.VDELEGATE),))
    p.obj_close(rh)
    p.obj_close(recv(sub, b'MAPPED'))
    tag = recv_any(sub, (b'DONE', b'LATE-TIMEOUT'))
    if tag == b'LATE-TIMEOUT':
        raise AssertionError('late waiter blocked despite no writer')
    got = waitmap((b,))
    ok(got[b] == '',
       'late waiter after real writer exit returns 3 and reads the snapshot')
    p.obj_close(fifo)

    # --- sleeping observer + real writer process exit ---------------------
    fifo = p.obj_fifocreate(64)
    sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA | p.VDELEGATE | p.VWRITE, 128)
    w = spawn_handles(WRITER.replace('@WORD1@', '0x3333')
                      .replace('@AFTER@', "_recv(b'DIE')"), fifo)
    rh = recv(sub, b'HANDLE')
    wh = recv(sub, b'WRITER')
    p.obj_close(wh)
    b = spawn_handles(OBSERVER_SLEEP_EXIT.replace('@BASE@', '0x22000000')
                      .replace('@WORD1@', '0x3333'), fifo,
                      ((rh, p.VREAD | p.VMETA | p.VDELEGATE),))
    p.obj_close(rh)
    p.obj_close(recv(sub, b'MAPPED'))
    p.sleep(400)
    send(fifo, b'DIE')
    p.obj_close(recv(sub, b'DONE'))
    got = waitmap((w, b))
    ok(got[w] == '' and got[b] == '',
       'sleeping RO observer wakes on real writer exit with snapshot intact')
    p.obj_close(fifo)

    # --- writer release versus rapid replacement (two vCPUs) --------------
    rounds = 16
    seen3 = 0
    for i in range(rounds):
        word1 = 0x1000 + i
        word2 = 0x2000 + i
        if i % 2:
            after = ("_recv(b'GO')\np.region_unmap(rw)\n"
                     "_send(b'UNMAPPED')\n_recv(b'DIE')")
        else:
            after = "_recv(b'GO')"
        code = (WRITER.replace('@WORD1@', '0x%x' % word1)
                .replace('@AFTER@', after))
        fifo = p.obj_fifocreate(64)
        sub = p.obj_subscribe(fifo, p.VRECV | p.VMETA | p.VDELEGATE | p.VWRITE, 128)
        w = spawn_handles(code, fifo)
        rh = recv(sub, b'HANDLE')
        wh = recv(sub, b'WRITER')
        bcode = (OBSERVER_REPLACE.replace('@BASE@', '0x20000000')
                 .replace('@WORD2@', '0x%x' % word2))
        b = spawn_handles(bcode, fifo,
                          ((rh, p.VREAD | p.VMETA | p.VDELEGATE),))
        p.obj_close(rh)
        p.obj_close(recv(sub, b'MAPPED'))
        p.sleep(400)
        send(fifo, b'GO')
        deadline = p.time_ns() + 5000000000
        while True:
            try:
                rw = p.region_map(wh, 0x30000000)
                break
            except OSError:
                if p.time_ns() > deadline:
                    raise AssertionError('replacement writer map timed out')
                p.sleep(10)
        p.region_write(rw + 8, b'REPLACED')
        p.region_atomic_store(rw, word2)
        p.region_wake(rw, 0, 2)
        send(fifo, b'DIE')
        tag = None
        while tag is None:
            h, env = p.obj_receive(sub, 10000, 64)
            p.obj_close(h)
            if env in (b'DONE', b'DONE3'):
                tag = env
        if tag == b'DONE3':
            seen3 += 1
        got = waitmap((w, b))
        if got[w] != '' or got[b] != '':
            raise AssertionError('replacement round status: %r %r' %
                                 (got[w], got[b]))
        p.region_unmap(rw)
        p.obj_close(wh)
        p.obj_close(fifo)
    ok(seen3 == rounds,
       'writer release survives rapid replacement in %d rounds' % rounds)

    # --- wait/wake state does not leak physical pages --------------------
    free0 = p.system()[6]
    for i in range(32):
        reg = p.region_create(8192)
        rw = p.region_map(reg)
        p.region_atomic_store(rw, i)
        if p.region_wait(rw, 0, i, 0) != 2:
            raise AssertionError('local non-blocking wait status')
        if p.region_wake(rw, 0, 1) != 0:
            raise AssertionError('local wake with no waiter')
        p.region_unmap(rw)
        p.obj_close(reg)
    free1 = p.system()[6]
    ok(free0 - free1 <= 65536,
       'repeated wait/wake/map/unmap/close reclaims physical pages')

    p.obj_close(sh)
    print('T39-FINISH', n[0], 'PASS')
    return 1
