"""Phase 6 raw Ethernet L2 acceptance over capability-region rings.

The kernel owns the NIC, DMA, IRQ and L2 frame transfer.  Userspace receives
complete Ethernet frames (MACs and EtherType included) from one read-only
region and publishes complete frames through one authorized writable region.
There is no ObjectFIFO, Pipe, kernel IP stack or per-frame payload syscall on
this path.  The external QEMU socket peer validates and echoes every frame.
"""

import plan9 as p
import struct

PUB = 16
ACK = 32
HEADER = 4096
SLOTS = 64
SLOT = 2048
DATA = SLOT - 16
M64 = 0xffffffffffffffff
PEER = bytes((2, 0, 0, 0, 19, 1))

ETYPES = (
    (0x0800, b'T47-ipv4'),
    (0x86DD, b'T47-ipv6'),
    (0x0806, b'T47-arp'),
    (0x88B5, b'T47-exp'),
)


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


def rx_drain(base, last):
    out = []
    n = p.region_atomic_load(base+PUB)
    while last < n:
        last += 1
        off = HEADER + ((last-1) % SLOTS)*SLOT
        if p.region_atomic_load(base+off) != ((2*last) & M64):
            continue
        ln, flags = struct.unpack('<II', p.region_read(base+off+8, 8))
        if ln > DATA:
            continue
        frame = p.region_read(base+off+16, ln)
        if p.region_atomic_load(base+off) != ((2*last) & M64):
            continue
        out.append(frame)
    return out, last


def rx_collect(base, last, want, timeout_ms):
    out = []
    end = p.time_ns() + timeout_ms*1000000
    while len(out) < want and p.time_ns() < end:
        recs, last = rx_drain(base, last)
        out += recs
        if len(out) >= want:
            break
        cur = p.region_atomic_load(base+PUB)
        p.region_wait(base, PUB, cur, 50)
    return out, last


def rx_settle(base, last, timeout_ms):
    out = []
    end = p.time_ns() + timeout_ms*1000000
    while p.time_ns() < end:
        recs, last = rx_drain(base, last)
        out += recs
        if last >= p.region_atomic_load(base+PUB):
            break
        p.region_wait(base, PUB, p.region_atomic_load(base+PUB), 50)
    return out, last


def tx_publish(base, seq, frame):
    # Never reuse a slot before the kernel has acknowledged it: publish only
    # while the ring has room, otherwise wait for ack_seq to advance.
    nxt = seq + 1
    while nxt - p.region_atomic_load(base+ACK) > SLOTS:
        ack = p.region_atomic_load(base+ACK)
        if nxt - ack <= SLOTS:
            break
        p.region_wait(base, ACK, ack, 50)
    off = HEADER + ((nxt-1) % SLOTS)*SLOT
    p.region_atomic_store(base+off, ((2*nxt-1) & M64))
    p.region_write(base+off+8, struct.pack('<II', len(frame), 0))
    p.region_write(base+off+16, frame)
    p.region_atomic_store(base+off, ((2*nxt) & M64))
    p.region_atomic_store(base+PUB, nxt)
    p.region_wake(base, PUB, 1)
    return nxt


def tx_wait_ack(base, seq, timeout_ms):
    end = p.time_ns() + timeout_ms*1000000
    while True:
        ack = p.region_atomic_load(base+ACK)
        if ack >= seq or p.time_ns() >= end:
            return ack
        p.region_wait(base, ACK, ack, 50)


def mkframe(mac, etype, body, total=60):
    if 14 + len(body) > total:
        raise ValueError('body too long')
    return PEER + mac + struct.pack('>H', etype) + body + bytes(total-14-len(body))


def reply(frame):
    return frame[6:12] + frame[0:6] + frame[12:]


def check_ring(base, label):
    hdr = struct.unpack('<IIII', p.region_read(base, 16))
    ok(hdr == (1, 0x5250324f, SLOT, SLOTS),
       label + ' carries a bounded 64-slot L2 frame ring')
    ok(p.region_atomic_load(base+PUB) == 0 and
       p.region_atomic_load(base+ACK) == 0,
       label + ' starts with an empty publication/acknowledgement cursor')


def open_rings(cat, controller):
    d = p.obj_device_open(cat, 2, controller, 0)
    rx = p.obj_device_rx(d)
    rxbase = p.region_map(rx, 0)
    tx = p.obj_device_tx(d)
    txbase = p.region_map(tx, 0)
    return d, rx, rxbase, tx, txbase


def body():
    cat = p.obj_devices_open(2184)
    entries = [e for e in p.obj_device_list(cat) if e[1] == 2]
    ok(len(entries) == 1, 'catalog exposes one raw L2 Ethernet controller')
    ctl = entries[0][2]
    d = p.obj_device_open(cat, 2, ctl, 0)
    mac = p.obj_device_ether_info(d)[4]
    ok(len(mac) == 6, 'device reports a six-octet NIC MAC address')

    # A device handle without the family authority cannot acquire a channel.
    meta = p.obj_grant(d, p.VMETA)
    closed = False
    try:
        p.obj_device_rx(meta)
    except OSError:
        closed = True
    ok(closed, 'missing receive capability fails closed (no fallback channel)')
    closed = False
    try:
        p.obj_device_tx(meta)
    except OSError:
        closed = True
    p.obj_close(meta)
    ok(closed, 'device handle without Vsend cannot acquire the transmit region')

    rx = p.obj_device_rx(d)
    ok(rx > 0, 'RX channel is a capability region Handle')
    rxbase = p.region_map(rx, 0)
    check_ring(rxbase, 'RX region')
    amp = False
    try:
        p.obj_grant(rx, p.VWRITE)
    except OSError:
        amp = True
    ok(amp, 'receive Handle cannot be amplified to a writer')

    tx = p.obj_device_tx(d)
    ok(tx > 0, 'TX channel is a writable capability region Handle')
    txbase = p.region_map(tx, 0)
    check_ring(txbase, 'TX region')
    busy = False
    try:
        p.region_map(tx, 0)
    except OSError:
        busy = True
    ok(busy, 'a second concurrent writable TX mapping fails with Einuse')

    # Denied detach: metadata authority is not control authority.
    m = p.obj_grant(d, p.VMETA)
    denied = False
    try:
        p.obj_device_ether_detach(m)
    except OSError:
        denied = True
    p.obj_close(m)
    ok(denied, 'metadata-only device Handle cannot detach the controller')

    # Ethernet SEND is not a hidden legacy per-frame fallback.
    legacy = False
    try:
        p.obj_device_send(d, mkframe(mac, 0x88B5, b'T47-legacy'))
    except OSError:
        legacy = True
    ok(legacy, 'Ethernet SEND fails instead of acting as a legacy frame path')

    # L2 neutrality: publish and receive all documented EtherTypes.
    seq = 0
    expected = []
    for etype, marker in ETYPES:
        frame = mkframe(mac, etype, marker)
        seq = tx_publish(txbase, seq, frame)
        expected.append(reply(frame))
    ack = tx_wait_ack(txbase, seq, 5000)
    ok(ack >= seq, 'kernel consumed published frames and advanced ack_seq')
    replies, last = rx_collect(rxbase, 0, len(ETYPES), 5000)
    ok(len(replies) == len(ETYPES),
       'all four EtherType frames returned through the RX region')
    for i, (etype, marker) in enumerate(ETYPES):
        ok(expected[i] in replies,
           'RX reply for EtherType 0x%04x is byte-identical (MACs/type/payload)'
           % etype)

    # A larger frame with no padding requirement: payload must survive intact.
    large = mkframe(mac, 0x0800, b'T47-LARGE:' + b'\xab\xcd'*745, 1514)
    seq = tx_publish(txbase, seq, large)
    ack = tx_wait_ack(txbase, seq, 5000)
    replies, last = rx_collect(rxbase, last, 1, 5000)
    ok(ack >= seq and bool(replies) and replies[0] == reply(large),
       '1514-byte frame survives TX and RX byte-for-byte')

    # Bounded publication: a burst must respect acknowledgement backpressure
    # and must never overwrite a slot the kernel is still consuming.
    before = p.obj_device_ether_info(d)
    maxout = 0
    for i in range(80):
        frame = mkframe(mac, 0x88B5, b'T47-BURST:'+struct.pack('<H', i))
        seq = tx_publish(txbase, seq, frame)
        out = seq - p.region_atomic_load(txbase+ACK)
        if out > maxout:
            maxout = out
    ack = tx_wait_ack(txbase, seq, 5000)
    ok(maxout <= SLOTS,
       'publisher never exceeded the 64-slot ring (max %d outstanding)' % maxout)
    ok(ack >= seq, 'ack_seq catches up after the bounded burst')
    after = p.obj_device_ether_info(d)
    ok(after[11] - before[11] == 80,
       'kernel accepted all 80 backpressured frames')
    ok(after[15] == before[15] and after[16] == before[16],
       'bounded burst produced no transmit drops or errors')
    _, last = rx_settle(rxbase, last, 2000)
    st = p.obj_device_ether_info(d)
    ok(st[12] <= st[13] and st[14] <= st[13],
       'receive handoff stays within its bounded capacity')

    # Detach joins both workers, then a fresh owner reacquires the NIC.
    ok(p.obj_device_ether_detach(d) == 0, 'explicit detach joins RX/TX workers')
    ok(p.obj_device_ether_info(d)[6] == 1, 'detached controller reports closed')
    p.region_unmap(rxbase)
    p.obj_close(rx)
    p.obj_close(d)

    d2, rx2, rx2base, tx2, tx2base = open_rings(cat, ctl)
    check_ring(rx2base, 'reopened RX region')
    check_ring(tx2base, 'reopened TX region')
    ok(rx2 > 0 and tx2 > 0,
       'reopened device provides fresh RX/TX region Handles')

    # The orphaned detached TX region has no consumer: publishing there must
    # not reach the reopened NIC, and its acknowledgement cursor stays put.
    orphan = mkframe(mac, 0x88B5, b'T47-ORPHAN')
    oldseq = p.region_atomic_load(txbase+PUB)
    tx_publish(txbase, oldseq, orphan)
    p.sleep(300)
    ok(p.region_atomic_load(txbase+ACK) == oldseq,
       'old detached TX slot is never acknowledged by the reopened kernel')
    replies, _ = rx_collect(rx2base, 0, 1, 800)
    ok(reply(orphan) not in replies,
       'old detached TX region cannot transmit on the reopened NIC')
    p.region_unmap(txbase)
    p.obj_close(tx)

    # The reopened device transmits and receives on its fresh regions.
    frame = mkframe(mac, 0x0806, b'T47-REOPEN')
    seq2 = tx_publish(tx2base, 0, frame)
    ack2 = tx_wait_ack(tx2base, seq2, 5000)
    replies, _ = rx_collect(rx2base, 0, 1, 5000)
    ok(ack2 >= seq2 and bool(replies) and replies[0] == reply(frame),
       'reopened NIC transmits and returns the frame through fresh regions')

    # The fresh device rejects the retired channel paths as well.
    legacy = False
    try:
        p.obj_device_send(d2, frame)
    except OSError:
        legacy = True
    ok(legacy, 'reopened device still rejects legacy Ethernet SEND')

    ok(p.obj_device_ether_detach(d2) == 0, 'second detach joins workers')
    p.region_unmap(rx2base)
    p.region_unmap(tx2base)
    p.obj_close(rx2)
    p.obj_close(tx2)
    p.obj_close(d2)
    p.obj_close(cat)


def run():
    try:
        body()
    except Exception as ex:
        FAILED.append('exception')
        print('T47-RESULT', shared.n, 'FAIL')
        raise AssertionError('T47 aborted: ' + repr(ex))
    if FAILED:
        print('T47-RESULT', shared.n, 'FAIL')
        raise AssertionError('T47 failures: ' + ', '.join(FAILED))
    print('T47-RESULT', shared.n, 'PASS')
    return 1
