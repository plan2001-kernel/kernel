"""End-to-end MicroPython IPC over capability regions for Phase 4.

Three independent MicroPython processes exchange task, delta and immutable
8 KiB records through two volatile regions.  Region Handles cross the process
boundary only as explicit SPAWN vhandles; no ObjectFIFO, Pipe, semaphore,
rendezvous or payload-copy syscall is used.  The coordinator only creates the
regions, attenuates the grants, starts the siblings and collects completion.
The children enforce the writer-release exit chain themselves.  The
coordinator never holds a writable mapping and never proxies test data.

Every assertion prints T40-PASS after it passes.  The runner requires one
final sequential T40-FINISH record.
"""

CID = 0x4f45584543000003

T_SIZE = 65536
R_SIZE = 32768
ACCOUNT_TSIZE = 16384
ACCOUNT_RSIZE = 8192

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

PUB = 0
CTRL = 8
CTRL_SEQ_MASK = 0xff
CTRL_WBW = 0x100
MAX_IO = 65536
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
IDS = ''


def fail(msg):
    p.exits('p4:' + IDS + msg)


def ctrl_seq(value):
    return value & CTRL_SEQ_MASK


def ctrl_word(seq, wbw):
    # Low 8 bits are the stage sequence.  CTRL_WBW is a stage-local flag that
    # is only meaningful together with stage 2; advancing to stage 3 clears
    # it explicitly.  Waiters compare ctrl_seq(), never the mixed integer.
    return (seq & CTRL_SEQ_MASK) | (CTRL_WBW if wbw else 0)


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


def record_span_error(off, length, region_size):
    # Subtraction-based checks keep the comparison independent of integer
    # overflow.  A record needs a 32-byte header plus a payload that fits in
    # the same region.  MAX_IO is the separate MicroPython bridge limit.
    if off < 0 or length < 0:
        return 'negative'
    if off > region_size:
        return 'offset'
    if region_size - off < 32:
        return 'header'
    if length > MAX_IO:
        return 'io-limit'
    if length > region_size - off - 32:
        return 'payload'
    return None


def record_ref_error(ref, region_size):
    if ref < 0:
        return 'negative'
    if ref > region_size:
        return 'offset'
    if region_size - ref < 32:
        return 'header'
    return None


def delta_error(ref, sub_off, sub_len, dpay_len, base_len, region_size):
    e = record_ref_error(ref, region_size)
    if e is not None:
        return 'ref-' + e
    if sub_off < 0 or sub_len < 0:
        return 'negative'
    if sub_off > base_len:
        return 'sub-off'
    if sub_len > base_len - sub_off:
        return 'sub-len'
    if dpay_len < 16:
        return 'header'
    if sub_len > dpay_len - 16:
        return 'payload'
    if dpay_len != 16 + sub_len:
        return 'exact'
    return None


def put_record_error(base, off, typ, seq, payload, region_size, ref=0):
    length = len(payload)
    e = record_span_error(off, length, region_size)
    if e is not None:
        return 'record-put-' + e
    if ref != 0:
        e = record_ref_error(ref, region_size)
        if e is not None:
            return 'record-put-ref-' + e
    return None


def put_record(base, off, typ, seq, payload, region_size, ref=0):
    e = put_record_error(base, off, typ, seq, payload, region_size, ref)
    if e is not None:
        fail(e)
    length = len(payload)
    hdr = struct.pack('<IHHIIQQ', MAGIC, typ, 0, seq, length, ref,
                      fnv(payload))
    p.region_write(base + off, hdr + payload)


def record_header_error(base, off, region_size):
    e = record_span_error(off, 0, region_size)
    if e is not None:
        return 'record-get-' + e, None
    hdr = p.region_read(base + off, 32)
    if len(hdr) != 32:
        return 'short-record-header', None
    fields = struct.unpack('<IHHIIQQ', hdr)
    magic, typ, flags, seq, length, ref, csum = fields
    if magic != MAGIC:
        return 'record-magic', None
    e = record_span_error(off, length, region_size)
    if e is not None:
        return 'record-get-' + e, None
    if ref != 0:
        e = record_ref_error(ref, region_size)
        if e is not None:
            return 'record-ref-' + e, None
    return None, fields


def get_record(base, off, want_type, want_seq, region_size):
    e, fields = record_header_error(base, off, region_size)
    if e is not None:
        fail(e)
    magic, typ, flags, seq, length, ref, csum = fields
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


def wait_ctrl(base, target, timeout, deadline):
    while True:
        v = p.region_atomic_load(base + CTRL)
        if ctrl_seq(v) >= target:
            return v
        r = p.region_wait(base, CTRL, v, timeout)
        if r == 2:
            if p.time_ns() > deadline:
                fail('ctrl-timeout@%x' % base)
        elif r == 3:
            fail('ctrl-writer-gone@%x' % base)
        elif r == 0 or r == 1:
            pass
        else:
            fail('ctrl-status')


def wait_ctrl_boot(base, target, timeout, deadline):
    while True:
        v = p.region_atomic_load(base + CTRL)
        if ctrl_seq(v) >= target:
            return v
        r = p.region_wait(base, CTRL, v, timeout)
        if r == 2:
            if p.time_ns() > deadline:
                fail('ctrl-boot-timeout@%x' % base)
        elif r == 3:
            if p.time_ns() > deadline:
                fail('ctrl-boot-writer-gone@%x' % base)
            p.sleep(10)
        elif r == 0 or r == 1:
            pass
        else:
            fail('ctrl-boot-status')


def wait_release(base, off, expected, timeout, deadline):
    while True:
        v = p.region_atomic_load(base + off)
        if v != expected:
            fail('release-value@%x+%x' % (base, off))
        r = p.region_wait(base, off, expected, timeout)
        if r == 3:
            return
        if r == 2:
            if p.time_ns() > deadline:
                fail('release-timeout@%x+%x' % (base, off))
        elif r == 0 or r == 1:
            pass
        else:
            fail('release-status')
'''

A_CODE = PRELUDE + r'''
CPU = @CPU@
DELAY = @DELAY@
if p.proc_info()[1] != CPU:
    fail('cpu-affinity')
t = find_region(True, @TSIZE@)
r = find_region(False, @RSIZE@)
IDS = 'T%x:R%x:' % (p.obj_info(t)[2] & 0xffffffff,
                    p.obj_info(r)[2] & 0xffffffff)
tb = map_at(t, 0x30000000)
rb = map_at(r, 0x31000000)
if p.region_wait(tb, 0, p.region_atomic_load(tb), 0) != 2:
    fail('writer-self-presence-after-map')
p.obj_close(t)
p.obj_close(r)
if p.region_wait(tb, 0, p.region_atomic_load(tb), 0) != 2:
    fail('writer-self-presence-after-close')

wait_ctrl_boot(rb, 1, 500, p.time_ns() + 15000000000)
if p.region_wait(rb, 0, p.region_atomic_load(rb), 0) != 2:
    fail('b-writer-not-visible')

base = base_bytes()
put_record(tb, BASE_OFF, T_BASE, 1, base, @TSIZE@)
p.region_atomic_store(tb, 1)
p.region_wake(tb, 0, 8)
put_record(tb, SCALAR_OFF, T_SCALAR, 2, struct.pack('<Q', SCALAR_X),
           @TSIZE@)
p.region_atomic_store(tb, 2)
p.region_wake(tb, 0, 8)

wait_value(rb, 0, 1, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_SCALAR, T_RESULT, 1, @RSIZE@)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != SCALAR_Y:
    fail('scalar-answer')
v = wait_ctrl(rb, 2, 1000, p.time_ns() + 15000000000)
if (v & CTRL_WBW) == 0:
    fail('wake-before-wait-not-proven')
p.sleep(DELAY)
delta = struct.pack('<QII', DELTA_SUB_OFF, DELTA_SUB_LEN, 0) + DELTA_BYTES
put_record(tb, DELTA_OFF, T_DELTA, 3, delta, @TSIZE@, BASE_OFF)
p.region_atomic_store(tb, 3)
if p.region_wake(tb, 0, 8) < 1:
    fail('delta-no-registered-waiter')
wait_value(rb, 0, 2, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_DELTA, T_RESULT, 2, @RSIZE@)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != delta_digest(base):
    fail('delta-answer')
wait_ctrl(rb, 3, 1000, p.time_ns() + 15000000000)
p.sleep(DELAY)
large = large_bytes()
put_record(tb, LARGE_OFF, T_LARGE, 4, large, @TSIZE@)
p.region_atomic_store(tb, 4)
if p.region_wake(tb, 0, 8) < 1:
    fail('large-no-registered-waiter')
wait_value(rb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_LARGE, T_RESULT, 3, @RSIZE@)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != fnv(large):
    fail('large-answer')
wait_ctrl(rb, 4, 1000, p.time_ns() + 15000000000)
wait_release(rb, CTRL, ctrl_word(4, False), 500,
             p.time_ns() + 15000000000)
p.exits('')
'''

B_CODE = PRELUDE + r'''
CPU = @CPU@
if p.proc_info()[1] != CPU:
    fail('cpu-affinity')
t = find_region(False, @TSIZE@)
r = find_region(True, @RSIZE@)
IDS = 'T%x:R%x:' % (p.obj_info(t)[2] & 0xffffffff,
                    p.obj_info(r)[2] & 0xffffffff)
tb = map_at(t, 0x32000000)
rb = map_at(r, 0x33000000)
if p.region_wait(rb, 0, p.region_atomic_load(rb), 0) != 2:
    fail('writer-self-presence-after-map')
p.obj_close(t)
p.obj_close(r)
if p.region_wait(rb, 0, p.region_atomic_load(rb), 0) != 2:
    fail('writer-self-presence-after-close')

p.region_atomic_store(rb + CTRL, ctrl_word(1, False))
p.region_wake(rb, CTRL, 8)
p.sleep(2000)

wbw = False
if p.region_atomic_load(tb) >= 2:
    wbw = True
else:
    wait_value(tb, 0, 2, 1000, p.time_ns() + 15000000000)
if p.region_wait(tb, 0, p.region_atomic_load(tb), 0) != 2:
    fail('a-writer-not-visible')
typ, seq, ref, bpay = get_record(tb, BASE_OFF, T_BASE, 1, @TSIZE@)
if len(bpay) != BASE_LEN:
    fail('base-length')
typ, seq, ref, spay = get_record(tb, SCALAR_OFF, T_SCALAR, 2, @TSIZE@)
if len(spay) != 8 or struct.unpack('<Q', spay)[0] != SCALAR_X:
    fail('scalar-task')
put_record(rb, R_SCALAR, T_RESULT, 1, struct.pack('<Q', SCALAR_Y),
           @RSIZE@)
p.region_atomic_store(rb, 1)
p.region_wake(rb, 0, 8)
p.region_atomic_store(rb + CTRL, ctrl_word(2, wbw))
p.region_wake(rb, CTRL, 8)

wait_value(tb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, dpay = get_record(tb, DELTA_OFF, T_DELTA, 3, @TSIZE@)
if len(dpay) < 16:
    fail('delta-header')
sub_off, sub_len, pad = struct.unpack('<QII', dpay[:16])
e = delta_error(ref, sub_off, sub_len, len(dpay), len(bpay), @TSIZE@)
if e is not None:
    fail('delta-' + e)
if ref != BASE_OFF:
    fail('delta-ref-not-base')
state = bytearray(bpay)
state[sub_off:sub_off + sub_len] = dpay[16:16 + sub_len]
digest = fnv(bytes(state))
put_record(rb, R_DELTA, T_RESULT, 2, struct.pack('<Q', digest), @RSIZE@)
p.region_atomic_store(rb, 2)
p.region_wake(rb, 0, 8)
p.region_atomic_store(rb + CTRL, ctrl_word(3, False))
p.region_wake(rb, CTRL, 8)

wait_value(tb, 0, 4, 1000, p.time_ns() + 15000000000)
typ, seq, ref, lpay = get_record(tb, LARGE_OFF, T_LARGE, 4, @TSIZE@)
if len(lpay) != LARGE_LEN or lpay != large_bytes():
    fail('large-content')
put_record(rb, R_LARGE, T_RESULT, 3, struct.pack('<Q', fnv(lpay)),
           @RSIZE@)
p.region_atomic_store(rb, 3)
p.region_wake(rb, 0, 8)
p.region_atomic_store(rb + CTRL, ctrl_word(4, False))
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
t = find_region(False, @TSIZE@)
r = find_region(False, @RSIZE@)
IDS = 'T%x:R%x:' % (p.obj_info(t)[2] & 0xffffffff,
                    p.obj_info(r)[2] & 0xffffffff)
tb = map_at(t, 0x34000000)
rb = map_at(r, 0x35000000)
p.obj_close(t)
p.obj_close(r)

wait_value_boot(tb, 0, 2, 1000, p.time_ns() + 15000000000)
typ, seq, ref, bpay = get_record(tb, BASE_OFF, T_BASE, 1, @TSIZE@)
if len(bpay) != BASE_LEN:
    fail('base-length')
snap = bpay
typ, seq, ref, spay = get_record(tb, SCALAR_OFF, T_SCALAR, 2, @TSIZE@)
if len(spay) != 8 or struct.unpack('<Q', spay)[0] != SCALAR_X:
    fail('scalar-task')

# Deliberately lag: keep the base record snapshot while A appends later
# records and B publishes answers.
p.sleep(250)
wait_value(rb, 0, 1, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_SCALAR, T_RESULT, 1, @RSIZE@)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != SCALAR_Y:
    fail('scalar-answer')

wait_value(tb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, dpay = get_record(tb, DELTA_OFF, T_DELTA, 3, @TSIZE@)
if len(dpay) < 16:
    fail('delta-header')
sub_off, sub_len, pad = struct.unpack('<QII', dpay[:16])
e = delta_error(ref, sub_off, sub_len, len(dpay), len(snap), @TSIZE@)
if e is not None:
    fail('delta-' + e)
if ref != BASE_OFF:
    fail('delta-ref-not-base')
state = bytearray(snap)
state[sub_off:sub_off + sub_len] = dpay[16:16 + sub_len]
recon_digest = fnv(bytes(state))
if recon_digest != delta_digest(base_bytes()):
    fail('delta-reconstruct')
wait_value(rb, 0, 2, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_DELTA, T_RESULT, 2, @RSIZE@)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != recon_digest:
    fail('delta-answer')

wait_value(tb, 0, 4, 1000, p.time_ns() + 15000000000)
typ, seq, ref, lpay = get_record(tb, LARGE_OFF, T_LARGE, 4, @TSIZE@)
if len(lpay) != LARGE_LEN or lpay != large_bytes():
    fail('large-content')
if fnv(lpay) != fnv(large_bytes()):
    fail('large-checksum')
typ, seq, ref, again = get_record(tb, BASE_OFF, T_BASE, 1, @TSIZE@)
if again != snap:
    fail('snapshot-overwritten')
wait_value(rb, 0, 3, 1000, p.time_ns() + 15000000000)
typ, seq, ref, pay = get_record(rb, R_LARGE, T_RESULT, 3, @RSIZE@)
if len(pay) != 8 or struct.unpack('<Q', pay)[0] != fnv(large_bytes()):
    fail('large-answer')
wait_ctrl(rb, 4, 1000, p.time_ns() + 15000000000)
wait_release(tb, 0, 4, 500, p.time_ns() + 15000000000)
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

# CTRL is a low-bit stage sequence plus high-bit flags.  Stage 2 carries the
# wake-before-wait proof flag; stage 3 must not be inferred from the mixed
# integer, and the flag is stage-local, so stage 3 clears it explicitly.
CTRL_PRODUCER = PRELUDE + r'''
t = find_region(True, 8192)
b = map_at(t, 0x30000000)
p.obj_close(t)
p.region_write(b + 64, b'CTRL-STAGE2!!')
p.region_atomic_store(b + CTRL, ctrl_word(2, True))
p.region_wake(b, CTRL, 8)
p.sleep(5000)
p.region_write(b + 64, b'CTRL-STAGE3!!')
p.region_atomic_store(b + CTRL, ctrl_word(3, False))
p.region_wake(b, CTRL, 8)
p.sleep(500)
p.exits('')
'''

CTRL_CONSUMER = PRELUDE + r'''
t = find_region(False, 8192)
b = map_at(t, 0x31000000)
p.obj_close(t)
deadline = p.time_ns() + 12000000000
v2 = wait_ctrl(b, 2, 500, deadline)
if ctrl_seq(v2) != 2:
    fail('ctrl-stage2-sequence')
if (v2 & CTRL_WBW) == 0:
    fail('ctrl-stage2-flag')
t0 = p.time_ns()
v3 = wait_ctrl(b, 3, 500, deadline)
ms = (p.time_ns() - t0) // 1000000
if ms < 800:
    fail('ctrl-stage3-false-positive')
if ctrl_seq(v3) != 3:
    fail('ctrl-stage3-sequence')
if (v3 & CTRL_WBW) != 0:
    fail('ctrl-stage3-flag-policy')
if p.region_read(b + 64, 13) != b'CTRL-STAGE3!!':
    fail('ctrl-stage3-data')
p.exits('')
'''

CTRL_HOLD = PRELUDE + r'''
t = find_region(True, 8192)
b = map_at(t, 0x30000000)
p.obj_close(t)
p.region_atomic_store(b + CTRL, ctrl_word(2, True))
p.region_wake(b, CTRL, 8)
p.sleep(5000)
p.exits('')
'''

CTRL_TIMEOUT_CONSUMER = PRELUDE + r'''
t = find_region(False, 8192)
b = map_at(t, 0x31000000)
p.obj_close(t)
deadline = p.time_ns() + 12000000000
wait_ctrl(b, 2, 500, deadline)
deadline = p.time_ns() + 1200000000
while True:
    v = p.region_atomic_load(b + CTRL)
    if ctrl_seq(v) >= 3:
        p.exits('ctrl-stage3-unexpectedly-present')
    r = p.region_wait(b, CTRL, v, 300)
    if r == 2 and p.time_ns() > deadline:
        p.exits('')
    if r == 3:
        p.exits('ctrl-writer-gone-too-early')
'''

# Live malformed headers must be rejected by the same bounds predicate used
# before every record access, without reading beyond the mapped region.
RECORD_NEGATIVE = PRELUDE + r'''
t = find_region(True, 32768)
b = map_at(t, 0x30000000)
p.obj_close(t)
if record_span_error(32760, 32, 32768) is None:
    fail('neg-header-end')
if record_span_error(0, 32768, 32768) is None:
    fail('neg-payload-end')
if record_span_error(64, 65537, 32768) is None:
    fail('neg-oversize')
if record_ref_error(65536, 32768) is None:
    fail('neg-ref-range')
if record_ref_error(32760, 32768) is None:
    fail('neg-ref-header')
if delta_error(64, 4096, 16, 32, 4096, 32768) is None:
    fail('neg-delta-suboff')
if delta_error(65536, 0, 16, 32, 4096, 32768) is None:
    fail('neg-delta-ref')
if delta_error(64, 0, 20, 32, 4096, 32768) is None:
    fail('neg-delta-len')
if put_record_error(b, 64, T_RESULT, 1, b'x' * 40000, 32768) is None:
    fail('neg-put-long')
if put_record_error(b, 32760, T_RESULT, 1, b'x', 32768) is None:
    fail('neg-put-end')
hdr = struct.pack('<IHHIIQQ', MAGIC, T_RESULT, 0, 1, 40000, 0, 0)
p.region_write(b, hdr)
e, fields = record_header_error(b, 0, 32768)
if e is None:
    fail('neg-live-payload')
p.exits('')
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

    # This acceptance run is not a persistence test.  Suppress the periodic
    # intermediate O commits so multi-round startup timing reflects region
    # IPC rather than synchronous volume writes; the final program-end commit
    # still records the durable result.
    space.interval = 3600

    n = [0]

    def ok(value, name):
        assert value, name
        n[0] += 1
        print('T40-PASS', n[0], name)

    def waitmap(pids):
        got = {}
        order = []
        for h in pids:
            r = p.process_wait(h, -1)
            got[h] = r[1]
            order.append(h)
            p.obj_close(h)
        return got, order

    sh = p.obj_open_space('init')
    RW_RIGHTS = p.VREAD | p.VWRITE | p.VMETA | p.VDELEGATE
    RO_RIGHTS = p.VREAD | p.VMETA | p.VDELEGATE

    def spawn(code, vh, cpu):
        return space.spawn(CID, handle=sh, cpu=cpu,
                           argv=('micropython', '-c', code),
                           vhandles=vh)

    def main_round(tag, delay, tsize=T_SIZE, rsize=R_SIZE, report=True):
        T = p.region_create(tsize)
        R = p.region_create(rsize)
        tw = p.obj_open_handle(T, RW_RIGHTS)
        tr = p.obj_open_handle(T, RO_RIGHTS)
        tr2 = p.obj_open_handle(T, RO_RIGHTS)
        rw = p.obj_open_handle(R, RW_RIGHTS)
        rr = p.obj_open_handle(R, RO_RIGHTS)
        rr2 = p.obj_open_handle(R, RO_RIGHTS)

        def code(template, cpu, delay=None):
            s = (template.replace('@TSIZE@', str(tsize))
                 .replace('@RSIZE@', str(rsize))
                 .replace('@CPU@', str(cpu)))
            if delay is not None:
                s = s.replace('@DELAY@', str(delay))
            return s

        # B first so its answer region has the unique writer before A waits;
        # A then installs the unique task writer before C starts observing.
        b = spawn(code(B_CODE, 1), ((tr, RO_RIGHTS), (rw, RW_RIGHTS)), 1)
        p.sleep(300)
        a = spawn(code(A_CODE, 0, delay),
                  ((tw, RW_RIGHTS), (rr, RO_RIGHTS)), 0)
        p.sleep(300)
        c = spawn(code(C_CODE, 2), ((tr2, RO_RIGHTS), (rr2, RO_RIGHTS)), 2)

        got, order = waitmap((a, b, c))
        for h in (tw, tr, tr2, rw, rr, rr2):
            p.obj_close(h)
        p.obj_close(T)
        p.obj_close(R)

        clean = True
        for pid in (a, b, c):
            if got.get(pid) != '':
                clean = False
        if report:
            ok(len({a, b, c}) == 3,
               'round %s: A, B and C have three distinct PIDs' % tag)
            ok(clean, 'round %s: scalar, delta and 8 KiB records verified '
               'end-to-end (A=%r B=%r C=%r observed=%r)'
               % (tag, got.get(a), got.get(b), got.get(c), order))
            ok(clean, 'round %s: release chain requires B writer release '
               'before A exits and A writer release before C exits' % tag)
        else:
            if len({a, b, c}) != 3 or not clean:
                raise AssertionError(
                    'accounting round %s failed (A=%r B=%r C=%r order=%r)'
                    % (tag, got.get(a), got.get(b), got.get(c), order))

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

    # CTRL stage sequence and the WBW flag are separate fields.  Stage 2
    # with the flag must not satisfy a stage-3 wait, and withholding stage 3
    # must produce a bounded protocol timeout.
    reg = p.region_create(8192)
    wh = p.obj_open_handle(reg, RW_RIGHTS)
    rh = p.obj_open_handle(reg, RO_RIGHTS)
    w = spawn(CTRL_PRODUCER, ((wh, RW_RIGHTS),), 1)
    p.obj_close(wh)
    p.sleep(300)
    r = spawn(CTRL_CONSUMER, ((rh, RO_RIGHTS),), 2)
    p.obj_close(rh)
    got, order = waitmap((w, r))
    ok(got[w] == '' and got[r] == '',
       'CTRL stage 2 + WBW does not satisfy stage 3; the waiter blocks until '
       'real stage 3 and the flag is decoded (W=%r R=%r)'
       % (got[w], got[r]))
    p.obj_close(reg)

    reg = p.region_create(8192)
    wh = p.obj_open_handle(reg, RW_RIGHTS)
    rh = p.obj_open_handle(reg, RO_RIGHTS)
    w = spawn(CTRL_HOLD, ((wh, RW_RIGHTS),), 1)
    p.obj_close(wh)
    p.sleep(300)
    r = spawn(CTRL_TIMEOUT_CONSUMER, ((rh, RO_RIGHTS),), 2)
    p.obj_close(rh)
    got, order = waitmap((w, r))
    ok(got[r] == '' and 'unexpectedly' not in got[r],
       'withheld CTRL stage 3 returns only after the bounded timeout '
       '(status=%r)' % (got[r],))
    p.obj_close(reg)

    # Malformed and out-of-region records are rejected by the bounds check
    # before any payload access.  The live header below claims 40000 payload
    # bytes in a 32 KiB region.
    reg = p.region_create(32768)
    wh = p.obj_open_handle(reg, RW_RIGHTS)
    npid = spawn(RECORD_NEGATIVE, ((wh, RW_RIGHTS),), 1)
    p.obj_close(wh)
    got, order = waitmap((npid,))
    ok(got[npid] == '',
       'malformed header, invalid delta/reference and oversized writes are '
       'rejected before access (status=%r)' % (got[npid],))
    p.obj_close(reg)

    # --- region backing: repeated create/map-RO/unmap/close ------------
    # The coordinator never holds a writable mapping.  If backing pages were
    # not reclaimed this loop would lose 8 * 64 KiB.
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

    # --- plain three-child lifecycle control ---------------------------
    h0 = len(p.obj_list())
    free0 = p.system()[6]
    t1 = spawn(TRIVIAL, (), 0)
    t2 = spawn(TRIVIAL, (), 1)
    t3 = spawn(TRIVIAL, (), 2)
    got, order = waitmap((t1, t2, t3))
    p.sleep(1000)
    trivial_ctrl = free0 - p.system()[6]
    print('T40-INFO plain three-child lifecycle delta=%d' % trivial_ctrl)

    # --- matched full-round slope measurement --------------------------
    # The control rounds run the identical three-process protocol with
    # 16 KiB + 8 KiB regions.  The only difference from the measured rounds
    # is the 64 KiB + 32 KiB backing size, so this isolates region pages
    # from coordinator heap, process-exit and page-table overhead.  Several
    # warm-up rounds let the coordinator's own static heap high-water mark
    # settle before the samples start.
    for i in range(3):
        main_round('warm-full-%d' % (i + 1), 200, T_SIZE, R_SIZE,
                   report=False)
        p.sleep(500)
        main_round('warm-control-%d' % (i + 1), 200,
                   ACCOUNT_TSIZE, ACCOUNT_RSIZE, report=False)
        p.sleep(500)

    rounds = 8
    full_deltas = []
    control_deltas = []
    for i in range(rounds):
        f0 = p.system()[6]
        main_round('full-%d' % (i + 1), 200, T_SIZE, R_SIZE, report=False)
        p.sleep(1000)
        f1 = p.system()[6]
        full_deltas.append(f0 - f1)
        c0 = p.system()[6]
        main_round('control-%d' % (i + 1), 200,
                   ACCOUNT_TSIZE, ACCOUNT_RSIZE, report=False)
        p.sleep(1000)
        c1 = p.system()[6]
        control_deltas.append(c0 - c1)
        print('T40-INFO account pair %d full_delta=%d control_delta=%d'
              % (i + 1, full_deltas[-1], control_deltas[-1]))

    sum_full = 0
    sum_control = 0
    for i in range(rounds):
        sum_full += full_deltas[i]
        sum_control += control_deltas[i]
    avg_full = sum_full // rounds
    avg_control = sum_control // rounds
    gap = avg_full - avg_control
    h1 = len(p.obj_list())
    ok(h1 == h0,
       'coordinator Handle slots return to baseline after %d full and '
       '%d control rounds' % (rounds, rounds))
    # The measured backing-size difference is 73728 bytes.  A persistent
    # leak of the full T+R backing set would drive the gap toward that
    # value every round; 49152 is below one 64+32 KiB set and above normal
    # pair-to-pair noise after warm-up.
    ok(gap < 49152,
       'full 64+32 KiB rounds show no region-sized per-round slope versus '
       'the 16+8 KiB control (avg full=%d avg control=%d gap=%d)'
       % (avg_full, avg_control, gap))

    # Leak-sensitivity control: deliberately retain one full T+R backing set
    # during a settle measurement.  The movement must exceed the assertion
    # threshold, and closing the regions must return the pages.  This shows
    # the check above would fail for persistent leaked region backing.
    free0 = p.system()[6]
    lt = p.region_create(T_SIZE)
    lr = p.region_create(R_SIZE)
    p.sleep(500)
    leak_delta = free0 - p.system()[6]
    p.obj_close(lt)
    p.obj_close(lr)
    p.sleep(500)
    reclaimed = free0 - p.system()[6]
    ok(leak_delta > 49152 and reclaimed <= 65536,
       'leak-sensitivity control: retaining a full T+R set moves free pages '
       'by %d (threshold 49152) and closing it reclaims to %d'
       % (leak_delta, reclaimed))

    # --- failed allocation cleanup and writer-slot availability --------
    # A full Handle table must reject region creation without leaking the
    # eagerly allocated backing pages.
    fill = []
    while True:
        try:
            fill.append(p.obj_create(b'x'))
        except OSError:
            break
    free0 = p.system()[6]
    for i in range(8):
        try:
            p.region_create(T_SIZE)
        except OSError:
            pass
        else:
            raise AssertionError('region creation with a full Handle table')
    p.sleep(500)
    free1 = p.system()[6]
    ok(free0 - free1 <= 65536,
       'failed region allocation releases its pages (delta=%d)'
       % (free0 - free1))
    for h in fill:
        p.obj_close(h)

    reg = p.region_create(8192)
    wh = p.obj_open_handle(reg, RW_RIGHTS)
    w = spawn(SECOND_WRITER_ALLOWED, ((wh, RW_RIGHTS),), 1)
    p.obj_close(wh)
    got, order = waitmap((w,))
    ok(got[w] == '', 'a writer slot maps normally after accounting rounds')
    p.obj_close(reg)

    p.obj_close(sh)
    print('T40-FINISH', n[0], 'PASS')
    return 1
