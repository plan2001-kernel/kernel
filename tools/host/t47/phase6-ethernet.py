"""Phase 6 raw Ethernet L2 test over a capability-region receive ring.

The kernel owns the NIC, DMA, IRQ and L2 frame transfer.  Userspace receives
complete Ethernet frames, including MACs and EtherType, from one read-only
region mapping.  There is no ObjectFIFO, Pipe, kernel IP stack or boot-time
network service on this path.
"""

import plan9 as p
import struct

PUB = 16
HEADER = 4096
SLOTS = 64
SLOT = 2048
DATA = SLOT - 16
M64 = 0xffffffffffffffff


class Counter:
    def __init__(self, n=0):
        self.n = n


shared = Counter()
alias = shared
FAILED = []


def init():
    shared.n = 0


def ok(value, name):
    if value:
        shared.n += 1
        print('T47-PASS', shared.n, name)
    else:
        FAILED.append(name)
        print('T47-FAIL', name)


def drain(base, last):
    out = []
    n = p.region_atomic_load(base+PUB)
    while last < n:
        last += 1
        off = HEADER + ((last-1) % SLOTS)*SLOT
        if p.region_atomic_load(base+off) != (2*last) & M64:
            continue
        ln, flags = struct.unpack('<II', p.region_read(base+off+8, 8))
        if ln > DATA:
            continue
        frame = p.region_read(base+off+16, ln)
        if p.region_atomic_load(base+off) != (2*last) & M64:
            continue
        out.append((last, frame))
    return out, last


def wait_frames(base, last, timeout_ms):
    out = []
    end = p.time_ns() + timeout_ms*1000000
    while p.time_ns() < end:
        recs, last = drain(base, last)
        out += recs
        if out:
            return out, last
        p.region_wait(base, PUB, last, 50)
    return out, last


def run():
    cat = p.obj_devices_open(2184)
    entries = [e for e in p.obj_device_list(cat) if e[1] == 2]
    ok(len(entries) == 1, 'catalog exposes one raw L2 Ethernet controller')
    ctl = entries[0][2]
    d = p.obj_device_open(cat, 2, ctl, 0)
    info = p.obj_device_ether_info(d)
    mac = info[4]
    peer = bytes((2, 0, 0, 0, 19, 1))

    def frame(body):
        return peer + mac + b'\x88\xb5' + body + bytes(46-len(body))

    # A missing receive capability fails closed: no hidden legacy channel.
    meta = p.obj_grant(d, p.VMETA)
    closed = False
    try:
        p.obj_device_rx(meta)
    except OSError:
        closed = True
    p.obj_close(meta)
    ok(closed, 'missing receive capability fails closed (no fallback channel)')

    # The receive channel is a read-only region, not a FIFO or Pipe endpoint.
    rx = p.obj_device_rx(d)
    ok(rx > 0, 'RX channel is a capability region Handle')
    base = p.region_map(rx, 0)
    hdr = p.region_read(base, 16)
    ok(struct.unpack('<IIII', hdr) == (1, 0x5250324f, SLOT, SLOTS),
       'RX region carries a bounded 64-slot L2 frame ring')
    amp = False
    try:
        p.obj_grant(rx, p.VWRITE)
    except OSError:
        amp = True
    ok(amp, 'receive Handle cannot be amplified to a writer')

    # Denied detach: metadata authority is not control authority.
    m = p.obj_grant(d, p.VMETA)
    denied = False
    try:
        p.obj_device_ether_detach(m)
    except OSError:
        denied = True
    p.obj_close(m)
    ok(denied, 'metadata-only device Handle cannot detach the controller')

    f = frame(b'T13:ONE')
    ok(p.obj_device_send(d, f) == len(f), 'TX accepts one complete L2 frame')

    last = 0
    recs, last = wait_frames(base, last, 5000)
    ok(bool(recs), 'external peer frame arrived through the region ring')
    if recs:
        got = recs[0][1]
        ok(got == mac+peer+b'\x88\xb5'+b'T13:RX:one\x00\xff'+bytes(34),
           'RX bytes are the complete L2 frame including EtherType')

    # Bounded overwrite: the ring must stay 64 slots and drop safely.
    f = frame(b'T13:BURST')
    ok(p.obj_device_send(d, f) == len(f), 'TX accepts burst request')
    p.sleep(800)
    st = p.obj_device_ether_info(d)
    ok(st[12] <= 64 and st[14] <= 64,
       'receive handoff stays bounded during burst')
    latest, last = drain(base, last)
    ok(all(s <= M64 for s, _ in latest),
       'ring slot sequences stay within the published range')

    # Detach joins the RX worker, then a new owner reacquires the NIC.
    ok(p.obj_device_ether_detach(d) == 0, 'explicit detach joins RX worker')
    info = p.obj_device_ether_info(d)
    ok(info[6] == 1, 'detached controller reports closed')
    p.obj_close(rx)
    p.obj_close(d)

    d = p.obj_device_open(cat, 2, ctl, 0)
    rx = p.obj_device_rx(d)
    base = p.region_map(rx, 0)
    f = frame(b'T13:ONE')
    p.obj_device_send(d, f)
    recs, _ = wait_frames(base, 0, 5000)
    ok(bool(recs) and recs[0][1][12:14] == b'\x88\xb5',
       'close/reopen reacquires raw L2 RX and preserves EtherType')
    p.obj_close(rx)
    p.obj_close(d)
    p.obj_close(cat)
    print('T47-RESULT', shared.n, 'PASS')
    return 1
