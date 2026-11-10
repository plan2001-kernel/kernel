"""End-to-end MicroPython IPC over capability regions for Phase 4.

Three independent MicroPython processes exchange task, delta and immutable
8 KiB records through two volatile regions.  Region Handles cross the process
boundary only as explicit SPAWN vhandles; no ObjectFIFO, Pipe, semaphore,
rendezvous or payload-copy syscall is used.  The coordinator only creates the
regions, attenuates the grants, starts the siblings, observes exit order and
collects completion.  It never holds a writable mapping and never proxies
test data.

Every assertion prints T40-PASS after it passes.  The runner requires one
final sequential T40-FINISH record.
"""

CID = 0x4f45584543000003

T_SIZE = 65536
R_SIZE = 32768

PUB = 0
CTRL = 8

BASE_OFF = 64
BASE_LEN = 4096
SCALAR_OFF = BASE_OFF + 32 + BASE_LEN
DELTA_OFF = SCALAR_OFF + 32 + 8
LARGE_OFF = DELTA_OFF + 32 + 32
LARGE_LEN = 8192
R_SCALAR = 64
R_DELTA = 104
R_LARGE = 144

MAGIC = 0x50345243
T_BASE = 1
T_SCALAR = 2
T_DELTA = 3
T_LARGE = 4
T_RESULT = 5

SCALAR_X = 1234567
SCALAR_Y = (SCALAR_X * 3 + 7) & 0xffffffffffffffff
DELTA_SUB_OFF = 1000
DELTA_SUB_LEN = 16
DELTA_BYTES = b'P4DELTA-UPDATE!!'

# Common record layout, region-relative addressing and ordering helpers.
# The publication word is a naturally aligned 64-bit little-endian value.
# Writers build the full record and payload first, then release-store the
# word.  Readers acquire-load the word before dereferencing a record and
# recheck the word after every REGIONWAIT return.
PRELUDE = r'''
import plan9 as p
import struct

T_SIZE = 65536
R_SIZE = 32768
PUB = 0
CTRL = 8
BASE_OFF = 64
BASE_LEN = 4096
SCALAR_OFF = BASE_OFF + 32 + BASE_LEN
DELTA_OFF = SCALAR_OFF + 32 + 8
LARGE_OFF = DELTA_OFF + 32 + 32
LARGE_LEN = 8192
R_SCALAR = 64
R_DELTA = 104
R_LARGE = 144
MAGIC = 0x50345243
T_BASE = 1
T_SCALAR = 2
T_DELTA = 3
T_LARGE = 4
T_RESULT = 5
SCALAR_X = 1234567
SCALAR_Y = (SCALAR_X * 3 + 7) & 0xffffffffffffffff
DELTA_SUB_OFF = 1000
DELTA_SUB_LEN = 16
DELTA_BYTES = b'P4DELTA-UPDATE!!'
M64 = 0xffffffffffffffff


def fail(msg):
    p.exits('p4:' + msg)


def fnv(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & M64
    return h


def base_bytes():
    out = bytearray(BASE_LEN)
    for i in range(BASE_LEN):
        out[i] = (i * 37 + 11) & 0xff
    return bytes(out)


def large_bytes():
    out = bytearray(LARGE_LEN)
    for i in range(LARGE_LEN):
        out[i] = (i * 131 + 7) & 0xff
    return bytes(out)


def delta_digest(base):
    state = bytearray(base)
    state[DELTA_SUB_OFF:DELTA_SUB_OFF + DELTA_SUB_LEN] = DELTA_BYTES
    return fnv(bytes(state))


def find_region(write, size):
    for h in p.obj_list():
        try:
            i = p.obj_info(h)
        except OSError:
            continue
        if i[8] == p.VKIND_FIFO or i[8] == p.VKIND_PIPE:
            fail('fifo-or-pipe-inherited')
        if i[8] != p.VKIND_REGION or i[3] != size:
            continue
        if bool(i[10] & p.VWRITE) == write:
            return h
    fail('region-missing')


def map_at(handle, addr):
    got = p.region_map(handle, addr)
    if got != addr:
        fail('mapping-address')
    return got


def put_record(base, off, typ, seq, payload, ref=0):
    hdr = struct.pack('<IHHIIQQ', MAGIC, typ, 0, seq, len(payload),
                      ref, fnv(payload))
    p.region_write(base + off, hdr + payload)


def get_record(base, off, want_type, want_seq):
    hdr = p.region_read(base + off, 32)
    if len(hdr) != 32:
        fail('short-record-header')
    magic, typ, flags, seq, length, ref, csum = struct.unpack(
        '<IHHIIQQ', hdr)
    if magic != MAGIC:
        fail('record-magic')
    if length > 65536:
        fail('record-length')
    payload = p.region_read(base + off + 32, length)
    if len(payload) != length:
        fail('short-record-payload')
    if fnv(payload) != csum:
        fail('record-checksum')
    if typ != want_type:
        fail('record-type')
    if seq != want_seq:
        fail('record-sequence')
    return typ, seq, ref, payload


def wait_value(base, off, target, timeout, deadline):
    while True:
        v = p.region_atomic_load(base + off)
        if v >= target:
            return v
        r = p.region_wait(base, off, v, timeout)
        if r == 2:
            if p.time_ns() > deadline:
                fail('wait-timeout@%x+%x' % (base, off))
        elif r == 3:
            fail('writer-gone@%x+%x' % (base, off))
        elif r == 0 or r == 1:
            pass
        else:
            fail('wait-status')


def wait_value_boot(base, off, target, timeout, deadline):
    while True:
        v = p.region_atomic_load(base + off)
        if v >= target:
            return v
        r = p.region_wait(base, off, v, timeout)
        if r == 2:
            if p.time_ns() > deadline:
                fail('boot-timeout@%x+%x' % (base, off))
        elif r == 3:
            # No writer is mapped yet during bootstrap; a later writer is
            # allowed.  This is a bounded startup retry, not the data path.
            if p.time_ns() > deadline:
                fail('boot-writer-gone@%x+%x' % (base, off))
            p.sleep(10)
        elif r == 0 or r == 1:
            pass
        else:
            fail('boot-status')
'''

A_CODE = PRELUDE + r'''
CPU = @CPU@
DELAY = @DELAY@
if p.proc_info()[1] != CPU:
    fail('cpu-affinity')
t = find_region(True, T_SIZE)
r = find_region(False, R_SIZE)
tb = map_at(t, 0x30000000)
rb = map_at(r, 0x31000000)
p.obj_close(t)
p.obj_close(r)

wait_value_boot(rb, CTRL, 1, 500, p.time_ns() + 15000000000)

base = base_bytes()
put_record(tb, BASE_OFF, T_BASE, 1, base)
p.region_atomic_store(tb, 1)
p.region_wake(tb, 0, 8)
put_record(tb, SCALAR_OFF, T_SCALAR, 2, struct.pack('<Q', SCALAR_X))
p.region_atomic_store(tb, 2)
p.region_wake(tb, 0, 8)

wait_value(rb, 0, 1, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_SCALAR, T_RESULT, 1)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != SCALAR_Y:
    fail('scalar-answer')
v = wait_value(rb, CTRL, 2, 1000, p.time_ns() + 15000000000)
if (v & 0x100) == 0:
    fail('wake-before-wait-not-proven')
p.sleep(DELAY)
delta = struct.pack('<QII', DELTA_SUB_OFF, DELTA_SUB_LEN, 0) + DELTA_BYTES
put_record(tb, DELTA_OFF, T_DELTA, 3, delta, BASE_OFF)
p.region_atomic_store(tb, 3)
if p.region_wake(tb, 0, 8) < 1:
    fail('delta-no-registered-waiter')
wait_value(rb, 0, 2, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_DELTA, T_RESULT, 2)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != delta_digest(base):
    fail('delta-answer')
wait_value(rb, CTRL, 3, 1000, p.time_ns() + 15000000000)
p.sleep(DELAY)
large = large_bytes()
put_record(tb, LARGE_OFF, T_LARGE, 4, large)
p.region_atomic_store(tb, 4)
if p.region_wake(tb, 0, 8) < 1:
    fail('large-no-registered-waiter')
wait_value(rb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_LARGE, T_RESULT, 3)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != fnv(large):
    fail('large-answer')
wait_value(rb, CTRL, 4, 1000, p.time_ns() + 15000000000)
p.sleep(300)
p.exits('')
'''

B_CODE = PRELUDE + r'''
CPU = @CPU@
if p.proc_info()[1] != CPU:
    fail('cpu-affinity')
t = find_region(False, T_SIZE)
r = find_region(True, R_SIZE)
tb = map_at(t, 0x32000000)
rb = map_at(r, 0x33000000)
p.obj_close(t)
p.obj_close(r)

p.region_atomic_store(rb + CTRL, 1)
p.region_wake(rb, CTRL, 8)
p.sleep(2000)

wbw = 0
if p.region_atomic_load(tb) >= 2:
    wbw = 0x100
else:
    wait_value(tb, 0, 2, 1000, p.time_ns() + 15000000000)
typ, seq, ref, bpay = get_record(tb, BASE_OFF, T_BASE, 1)
if len(bpay) != BASE_LEN:
    fail('base-length')
typ, seq, ref, spay = get_record(tb, SCALAR_OFF, T_SCALAR, 2)
if len(spay) != 8 or struct.unpack('<Q', spay)[0] != SCALAR_X:
    fail('scalar-task')
put_record(rb, R_SCALAR, T_RESULT, 1, struct.pack('<Q', SCALAR_Y))
p.region_atomic_store(rb, 1)
p.region_wake(rb, 0, 8)
p.region_atomic_store(rb + CTRL, 2 | wbw)
p.region_wake(rb, CTRL, 8)

wait_value(tb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, dpay = get_record(tb, DELTA_OFF, T_DELTA, 3)
sub_off, sub_len, pad = struct.unpack('<QII', dpay[:16])
if ref != BASE_OFF or sub_len != DELTA_SUB_LEN or len(dpay) != 16 + sub_len:
    fail('delta-shape')
if sub_off + sub_len > len(bpay):
    fail('delta-bounds')
state = bytearray(bpay)
state[sub_off:sub_off + sub_len] = dpay[16:16 + sub_len]
digest = fnv(bytes(state))
put_record(rb, R_DELTA, T_RESULT, 2, struct.pack('<Q', digest))
p.region_atomic_store(rb, 2)
p.region_wake(rb, 0, 8)
p.region_atomic_store(rb + CTRL, 3)
p.region_wake(rb, CTRL, 8)

wait_value(tb, 0, 4, 1000, p.time_ns() + 15000000000)
typ, seq, ref, lpay = get_record(tb, LARGE_OFF, T_LARGE, 4)
if len(lpay) != LARGE_LEN or lpay != large_bytes():
    fail('large-content')
put_record(rb, R_LARGE, T_RESULT, 3, struct.pack('<Q', fnv(lpay)))
p.region_atomic_store(rb, 3)
p.region_wake(rb, 0, 8)
p.region_atomic_store(rb + CTRL, 4)
p.region_wake(rb, CTRL, 8)
p.exits('')
'''

C_CODE = PRELUDE + r'''
CPU = @CPU@
if p.proc_info()[1] != CPU:
    fail('cpu-affinity')
for h in p.obj_list():
    try:
        i = p.obj_info(h)
    except OSError:
        continue
    if i[8] == p.VKIND_REGION and (i[10] & p.VWRITE):
        fail('c-received-writable-region')
t = find_region(False, T_SIZE)
r = find_region(False, R_SIZE)
tb = map_at(t, 0x34000000)
rb = map_at(r, 0x35000000)
p.obj_close(t)
p.obj_close(r)

wait_value_boot(tb, 0, 2, 1000, p.time_ns() + 15000000000)
typ, seq, ref, bpay = get_record(tb, BASE_OFF, T_BASE, 1)
if len(bpay) != BASE_LEN:
    fail('base-length')
snap = bpay
typ, seq, ref, spay = get_record(tb, SCALAR_OFF, T_SCALAR, 2)
if len(spay) != 8 or struct.unpack('<Q', spay)[0] != SCALAR_X:
    fail('scalar-task')

# Deliberately lag: keep the base record snapshot while A appends later
# records and B publishes answers.
p.sleep(250)
wait_value(rb, 0, 1, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_SCALAR, T_RESULT, 1)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != SCALAR_Y:
    fail('scalar-answer')

wait_value(tb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, dpay = get_record(tb, DELTA_OFF, T_DELTA, 3)
sub_off, sub_len, pad = struct.unpack('<QII', dpay[:16])
if ref != BASE_OFF or sub_len != DELTA_SUB_LEN or len(dpay) != 16 + sub_len:
    fail('delta-shape')
if sub_off + sub_len > len(snap):
    fail('delta-bounds')
state = bytearray(snap)
state[sub_off:sub_off + sub_len] = dpay[16:16 + sub_len]
recon_digest = fnv(bytes(state))
if recon_digest != delta_digest(base_bytes()):
    fail('delta-reconstruct')
wait_value(rb, 0, 2, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_DELTA, T_RESULT, 2)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != recon_digest:
    fail('delta-answer')

wait_value(tb, 0, 4, 1000, p.time_ns() + 15000000000)
typ, seq, ref, lpay = get_record(tb, LARGE_OFF, T_LARGE, 4)
if len(lpay) != LARGE_LEN or lpay != large_bytes():
    fail('large-content')
if fnv(lpay) != fnv(large_bytes()):
    fail('large-checksum')
typ, seq, ref, again = get_record(tb, BASE_OFF, T_BASE, 1)
if again != snap:
    fail('snapshot-overwritten')
wait_value(rb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_LARGE, T_RESULT, 3)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != fnv(large_bytes()):
    fail('large-answer')
wait_value(rb, CTRL, 4, 1000, p.time_ns() + 15000000000)
p.sleep(1000)
p.exits('')
'''

TRIVIAL = r'''
import plan9 as p
p.exits('')
'''

W_DEATH_PUB = PRELUDE + r'''
t = find_region(True, 8192)
b = map_at(t, 0x30000000)
p.obj_close(t)
p.region_write(b + 64, b'DEATH-CHANGED!')
p.region_atomic_store(b, 0x77)
p.exits('')
'''

R_DEATH_CHANGED = PRELUDE + r'''
t = find_region(False, 8192)
b = map_at(t, 0x31000000)
p.obj_close(t)
r = p.region_wait(b, 0, 0, 2000)
if r != 0:
    fail('changed-status')
if p.region_read(b + 64, 14) != b'DEATH-CHANGED!':
    fail('changed-data')
p.exits('')
'''

W_DEATH_HOLD = PRELUDE + r'''
t = find_region(True, 8192)
b = map_at(t, 0x30000000)
p.obj_close(t)
p.region_write(b + 64, b'DEATH-LATE!!!!')
p.exits('')
'''

R_DEATH_LATE = PRELUDE + r'''
t = find_region(False, 8192)
b = map_at(t, 0x31000000)
p.obj_close(t)
t0 = p.time_ns()
r = p.region_wait(b, 0, 0, 2000)
ms = (p.time_ns() - t0) // 1000000
if r != 3:
    fail('late-status')
if ms > 500:
    fail('late-not-immediate')
if p.region_read(b + 64, 14) != b'DEATH-LATE!!!!':
    fail('late-data')
p.exits('')
'''

W_DEATH_SLEEP = PRELUDE + r'''
t = find_region(True, 8192)
b = map_at(t, 0x30000000)
p.obj_close(t)
p.region_write(b + 64, b'DEATH-WAIT!!!!')
p.sleep(2000)
p.exits('')
'''

R_DEATH_WAIT = PRELUDE + r'''
t = find_region(False, 8192)
b = map_at(t, 0x31000000)
p.obj_close(t)
t0 = p.time_ns()
r = p.region_wait(b, 0, 0, -1)
ms = (p.time_ns() - t0) // 1000000
if r != 3:
    fail('blocked-status')
if ms < 500:
    fail('blocked-too-short')
if p.region_read(b + 64, 14) != b'DEATH-WAIT!!!!':
    fail('blocked-data')
p.exits('')
'''

UNAUTHORIZED = r'''
import plan9 as p
for h in (1, 2, 3, 4, 7, 0x12345678):
    try:
        p.obj_info(h)
    except OSError:
        pass
    else:
        p.exits('guessed-handle')
try:
    p.region_map(1, 0x30000000)
except OSError:
    pass
else:
    p.exits('mapped-guessed-handle')
try:
    p.region_wait(0x3f000000, 0, 0, 0)
except OSError:
    pass
else:
    p.exits('waited-guessed-address')
try:
    p.region_unmap(0x3f000000)
except OSError:
    pass
else:
    p.exits('unmapped-guessed-address')
p.exits('')
'''

RO_WAKE_FAULT = PRELUDE + r'''
t = find_region(False, 8192)
b = map_at(t, 0x36000000)
p.obj_close(t)
try:
    p.region_wake(b, 0, 1)
except (OSError, ValueError):
    pass
else:
    fail('ro-wake-allowed')
p.region_write(b, b'X')
p.exits('ro-write-allowed')
'''

WRITER_HOLD = PRELUDE + r'''
t = find_region(True, 8192)
b = map_at(t, 0x30000000)
p.obj_close(t)
p.region_write(b + 64, b'SECOND-WRITER!!')
p.sleep(2500)
p.exits('')
'''

SECOND_WRITER_DENY = PRELUDE + r'''
t = find_region(True, 8192)
try:
    p.region_map(t, 0x31000000)
except OSError:
    p.obj_close(t)
    p.exits('')
p.exits('second-writer-allowed')
'''

SECOND_WRITER_ALLOWED = PRELUDE + r'''
t = find_region(True, 8192)
b = map_at(t, 0x32000000)
p.obj_close(t)
p.region_write(b + 64, b'REUSED-WRITER!!')
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
        print('T40-PASS', n[0], name)

    def waitmap(pids):
        got = {}
        order = []
        while len(got) < len(pids):
            pid, status = space.wait()
            got[pid] = status
            order.append(pid)
        return got, order

    sh = p.obj_open_space('init')
    RW_RIGHTS = p.VREAD | p.VWRITE | p.VMETA | p.VDELEGATE
    RO_RIGHTS = p.VREAD | p.VMETA | p.VDELEGATE

    def spawn(code, vh, cpu):
        return space.spawn(CID, handle=sh, cpu=cpu,
                           argv=('micropython', '-c', code),
                           vhandles=vh)

    def main_round(tag, delay):
        T = p.region_create(T_SIZE)
        R = p.region_create(R_SIZE)
        tw = p.obj_open_handle(T, RW_RIGHTS)
        tr = p.obj_open_handle(T, RO_RIGHTS)
        tr2 = p.obj_open_handle(T, RO_RIGHTS)
        rw = p.obj_open_handle(R, RW_RIGHTS)
        rr = p.obj_open_handle(R, RO_RIGHTS)
        rr2 = p.obj_open_handle(R, RO_RIGHTS)

        # B first so its answer region has the unique writer before A waits;
        # A then installs the unique task writer before C starts observing.
        b = spawn(B_CODE.replace('@CPU@', '1'), (
            (tr, RO_RIGHTS), (rw, RW_RIGHTS)), 1)
        p.sleep(300)
        a = spawn(A_CODE.replace('@CPU@', '0').replace('@DELAY@', str(delay)),
                  ((tw, RW_RIGHTS), (rr, RO_RIGHTS)), 0)
        p.sleep(300)
        c = spawn(C_CODE.replace('@CPU@', '2'), (
            (tr2, RO_RIGHTS), (rr2, RO_RIGHTS)), 2)
        for h in (tw, tr, tr2, rw, rr, rr2):
            p.obj_close(h)

        got, order = waitmap((a, b, c))
        p.obj_close(T)
        p.obj_close(R)

        ok(len({a, b, c}) == 3,
           'round %s: A, B and C have three distinct PIDs' % tag)
        clean = True
        for pid in (a, b, c):
            if got.get(pid) != '':
                clean = False
        ok(clean, 'round %s: scalar, delta and 8 KiB records verified '
           'end-to-end (A=%r B=%r C=%r order=%r)'
           % (tag, got.get(a), got.get(b), got.get(c), order))
        ordered = order[0] == b and order.index(a) < order.index(c)
        ok(ordered, 'round %s: exit order B before A before C' % tag)

    ncpu = p.system()[1]
    ok(ncpu >= 3, 'three vCPUs configured for the SMP run (ncpu=%d)' % ncpu)
    print('T40-INFO affinity A=0 B=1 C=2; pinned placement requested at '
          'spawn, actual CPU is not independently measured')

    # Repeated runs with deliberately different timing.
    rounds = 6
    for i in range(rounds):
        main_round(str(i + 1), 250 + i * 40)
    ok(True, '%d multi-core three-process rounds completed with no lost '
       'publication or blocked reader' % rounds)

    # Writer exits before an RO reader waits, with a changed word: the
    # changed comparison takes precedence over writer absence.
    reg = p.region_create(8192)
    wh = p.obj_open_handle(reg, RW_RIGHTS)
    rh = p.obj_open_handle(reg, RO_RIGHTS)
    w = spawn(W_DEATH_PUB, ((wh, RW_RIGHTS),), 1)
    p.obj_close(wh)
    got, order = waitmap((w,))
    ok(got[w] == '', 'writer publishes and exits before any reader maps')
    r = spawn(R_DEATH_CHANGED, ((rh, RO_RIGHTS),), 2)
    p.obj_close(rh)
    got, order = waitmap((r,))
    ok(got[r] == '',
       'late RO reader returns changed (0), not status 3, and retains data')
    p.obj_close(reg)

    # Writer exits before an RO reader waits, with an unchanged word.
    reg = p.region_create(8192)
    wh = p.obj_open_handle(reg, RW_RIGHTS)
    rh = p.obj_open_handle(reg, RO_RIGHTS)
    w = spawn(W_DEATH_HOLD, ((wh, RW_RIGHTS),), 1)
    p.obj_close(wh)
    got, order = waitmap((w,))
    ok(got[w] == '', 'unchanged writer exits before the reader maps')
    r = spawn(R_DEATH_LATE, ((rh, RO_RIGHTS),), 2)
    p.obj_close(rh)
    got, order = waitmap((r,))
    ok(got[r] == '',
       'late unchanged RO waiter returns status 3 immediately with data')
    p.obj_close(reg)

    # Writer exits while an RO reader is blocked in REGIONWAIT.
    reg = p.region_create(8192)
    wh = p.obj_open_handle(reg, RW_RIGHTS)
    rh = p.obj_open_handle(reg, RO_RIGHTS)
    w = spawn(W_DEATH_SLEEP, ((wh, RW_RIGHTS),), 1)
    p.obj_close(wh)
    r = spawn(R_DEATH_WAIT, ((rh, RO_RIGHTS),), 2)
    p.obj_close(rh)
    got, order = waitmap((w, r))
    ok(got[w] == '' and got[r] == '',
       'blocked RO waiter wakes with status 3 on real writer exit and '
       'keeps its snapshot')
    p.obj_close(reg)

    # A second simultaneous writable mapping is rejected, and the slot is
    # released by writer exit.
    reg = p.region_create(8192)
    w1 = p.obj_open_handle(reg, RW_RIGHTS)
    w2 = p.obj_open_handle(reg, RW_RIGHTS)
    w = spawn(WRITER_HOLD, ((w1, RW_RIGHTS),), 1)
    p.obj_close(w1)
    p.sleep(700)
    d = spawn(SECOND_WRITER_DENY, ((w2, RW_RIGHTS),), 2)
    p.obj_close(w2)
    got, order = waitmap((d,))
    ok(got[d] == '', 'a second simultaneous writable mapping is rejected')
    got, order = waitmap((w,))
    ok(got[w] == '', 'the original writer remains valid after the rejection')
    w3 = p.obj_open_handle(reg, RW_RIGHTS)
    e = spawn(SECOND_WRITER_ALLOWED, ((w3, RW_RIGHTS),), 1)
    p.obj_close(w3)
    got, order = waitmap((e,))
    ok(got[e] == '', 'the writer slot is reusable after the writer exits')
    p.obj_close(reg)

    # No ambient authority: a process with no grants cannot guess a Handle,
    # a writable mapping or a mapping-relative WAIT key.
    u = spawn(UNAUTHORIZED, (), 2)
    got, order = waitmap((u,))
    ok(got[u] == '',
       'ungranted process cannot guess a Handle, mapping or WAIT address')

    # An RO observer cannot WAKE or store.  The store fault is delivered as
    # an ordinary note and must not panic the kernel.
    reg = p.region_create(8192)
    ro = p.obj_open_handle(reg, RO_RIGHTS)
    f = spawn(RO_WAKE_FAULT, ((ro, RO_RIGHTS),), 2)
    p.obj_close(ro)
    got, order = waitmap((f,))
    ok('fault' in got[f],
       'RO WAKE is denied and an RO store faults only that observer')
    t = spawn(TRIVIAL, (), 0)
    got, order = waitmap((t,))
    ok(got[t] == '', 'the kernel keeps running after the RO store fault')
    p.obj_close(reg)

    # Direct region-ref/page accounting: create, map read-only, unmap and
    # close repeatedly.  The coordinator never holds a writable mapping.
    free0 = p.system()[6]
    for i in range(8):
        reg = p.region_create(T_SIZE)
        ro = p.obj_open_handle(reg, RO_RIGHTS)
        base = p.region_map(ro, 0x3f000000)
        if p.region_read(base, 8) != b'\x00' * 8:
            raise AssertionError('fresh region is not zeroed')
        p.region_unmap(base)
        p.obj_close(ro)
        p.obj_close(reg)
    p.sleep(500)
    free1 = p.system()[6]
    ok(free0 - free1 <= 65536,
       'repeated region create/map/unmap/close returns page backing '
       '(delta=%d)' % (free0 - free1))

    # Bounded full-round accounting.  The three-child control captures the
    # ordinary process-exit residual; the second identical round must not
    # lose another region's worth of pages.
    h0 = len(p.obj_list())
    free0 = p.system()[6]
    t1 = spawn(TRIVIAL, (), 0)
    t2 = spawn(TRIVIAL, (), 1)
    t3 = spawn(TRIVIAL, (), 2)
    got, order = waitmap((t1, t2, t3))
    p.sleep(1000)
    ctrl_overhead = free0 - p.system()[6]
    print('T40-INFO three-child lifecycle control delta=%d' % ctrl_overhead)

    deltas = []
    prev = p.system()[6]
    for i in range(2):
        main_round('lifecycle%d' % (i + 1), 200)
        p.sleep(1000)
        now = p.system()[6]
        deltas.append(prev - now)
        print('T40-INFO lifecycle round %d delta=%d' % (i + 1, deltas[-1]))
        prev = now
    h1 = len(p.obj_list())
    ok(h1 == h0,
       'coordinator Handle slots return to baseline after round cleanup')
    bound = ctrl_overhead + T_SIZE + R_SIZE + 2 * 65536
    ok(deltas[-1] <= bound,
       'full-round exit/close/unmap accounting stays bounded '
       '(delta=%d control=%d bound=%d)'
       % (deltas[-1], ctrl_overhead, bound))

    p.obj_close(sh)
    print('T40-FINISH', n[0], 'PASS')
    return 1
