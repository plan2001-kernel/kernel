# T12 focused tests, seeded with mkovol's SOURCE argument, run on ordinary
# MicroPython init.run().  Requires a disposable QEMU xHCI + USB mouse.
# No physical hardware access.  Do not describe root-hub replies as a
# downstream peripheral transfer.  Each assertion prints only after passing.
class Counter:
    def __init__(self, n=0):
        self.n = n

shared = Counter()
alias = shared


def init():
    shared.n = 0


def run():
    import plan9 as p
    import space
    import struct
    n = [0]

    def epo(kind, mode):
        return struct.pack('<IIII', 1, mode, kind, 0)

    def ok(value, name):
        assert value, name
        n[0] += 1
        print('T12-PASS', n[0], name)

    def deny(fn, message):
        try:
            fn()
        except OSError as e:
            ok(str(e) == message, 'exact error: ' + message)
            return
        raise AssertionError('expected ' + message)

    def exchange(d, message):
        seq = p.obj_device_send(d, message)
        h, done, family, length = p.obj_device_recv(d)
        data = p.obj_read(h, 4096)
        p.obj_close(h)
        assert done == seq and family == 1 and len(data) == length
        return data

    bad = 'bad arg in system call'
    perm = 'permission denied'
    busy = 'device or object already in use'
    failed = 'device: transfer failed; close and reopen'
    c = p.obj_devices_open(2184)
    entries = p.obj_device_list(c)
    ok(len(entries) == p.obj_device_list(c, 0) and len(entries) != 0, 'catalog count')
    version, family, dev, ep, ms, mr, flags, kind, reserved = entries[0]
    ok((version, family, ep, ms, mr, flags, kind, reserved) ==
       (2, 1, 0, 4104, 4096, 0, 1, 0), 'discovery record')
    meta = p.obj_devices_open()
    deny(lambda: p.obj_device_open(meta, 1, dev, 0), perm)
    p.obj_close(meta)
    meta = p.obj_grant(c, 8)
    deny(lambda: p.obj_device_open(meta, 1, dev, 0), perm)
    p.obj_close(meta)
    deny(lambda: p.obj_device_open(c, 99, dev, 0), bad)
    deny(lambda: p.obj_device_open(c, 1, 9999, 0), bad)
    deny(lambda: p.obj_device_open(c, 1, dev, 0, 256), perm)
    deny(lambda: p.sysraw(137, 16, 0, 24, 0, 0), bad)
    deny(lambda: p.obj_invoke(c, 37, 1), bad)
    d = p.obj_device_open(c, 1, dev, 0)
    deny(lambda: p.obj_device_open(c, 1, dev, 0), busy)
    deny(lambda: p.obj_device_recv(d), 'device: no completion')
    deny(lambda: p.obj_invoke(d, 39), bad)
    deny(lambda: p.obj_device_send(d, b''), bad)
    query = bytes((163, 0, 0, 0, 1, 0, 4, 0))
    power = bytes((35, 3, 8, 0, 1, 0, 0, 0))
    deny(lambda: p.obj_device_send(d, query+b'x'), bad)
    deny(lambda: p.obj_device_send(d, bytes((163, 0, 0, 0, 1, 0, 1, 16))), bad)
    deny(lambda: p.obj_device_send(d, bytes((35, 3, 8, 0, 1, 0, 1, 0))), bad)
    meta = p.obj_grant(d, 8)
    deny(lambda: p.obj_device_send(meta, query), perm)
    deny(lambda: p.obj_device_recv(meta), perm)
    deny(lambda: p.obj_device_ctl(meta, 1), perm)
    deny(lambda: p.obj_grant(meta, 2), perm)
    p.obj_close(meta)
    send = p.obj_grant(d, 2|4|8)
    deny(lambda: p.obj_device_send(send, query), perm)
    deny(lambda: p.obj_device_send(send, power), perm)
    # An IN direction bit does not make vendor/class requests read-only.
    deny(lambda: p.obj_device_send(send, bytes((192, 1, 0, 0, 0, 0, 0, 0))), perm)
    deny(lambda: p.obj_device_send(send, bytes((163, 3, 8, 0, 1, 0, 4, 0))), perm)
    ok(len(exchange(d, query)) == 4, 'denial does not poison session')
    p.obj_close(send)
    ok(p.obj_device_ctl(d, 1) == 0, 'authorized native control')
    deny(lambda: p.obj_device_ctl(d, 99), 'bad process or channel control request')
    seq = p.obj_device_send(d, query)
    deny(lambda: p.obj_device_send(d, query), busy)
    deny(lambda: p.obj_device_ctl(d, 1), busy)
    h, got, fam, length = p.obj_device_recv(d)
    data = p.obj_read(h, 32)
    ok(got == seq and fam == 1 and length == 4 and len(data) == 4, 'complete root status')
    fifo = p.obj_fifocreate(2)
    sub = p.obj_subscribe(fifo, 141, 2)
    p.obj_publish(fifo, h, b'usb')
    other, envelope = p.obj_receive(sub, 0)
    ok(p.obj_read(other, 32) == data and envelope == b'usb', 'unchanged ObjectFIFO handoff')
    for handle in (other, sub, fifo):
        p.obj_close(handle)
    ok(len(exchange(d, bytes((163, 0, 0, 0, 1, 0, 0, 16)))) == 8, 'root short reply trimmed')
    ok(p.obj_read(h, 32) == data, 'immutable previous response')
    ok(exchange(d, power) == b'', 'complete OUT and empty bytes response')
    alias_handle = p.obj_grant(d, 206)
    p.obj_close(d)
    ok(len(exchange(alias_handle, query)) == 4, 'alias survives original close')
    p.obj_device_send(alias_handle, query)
    p.obj_close(alias_handle)
    d = p.obj_device_open(c, 1, dev, 0)
    ok(len(exchange(d, query)) == 4, 'final close releases pending and exclusive claim')
    # Fill the real Vhand table while a completion is pending.  Failure must
    # leave it available for receive after one slot is freed.
    seq = p.obj_device_send(d, query)
    handles = []
    try:
        for unused in range(128):
            handles.append(p.obj_create(b'quota'))
    except OSError as e:
        ok(str(e) == 'vobj: too many handles', 'exact Handle exhaustion')
    else:
        raise AssertionError('Handle bound missing')
    deny(lambda: p.obj_device_recv(d), 'vobj: too many handles')
    p.obj_close(handles.pop())
    reply, got, fam, length = p.obj_device_recv(d)
    ok(got == seq and length == 4 and p.obj_read(reply, 4) == data, 'receive retry preserves completion')
    p.obj_close(reply)
    for handle in handles:
        p.obj_close(handle)
    # Provider-side rejection happens after entry to usbcore.  Even with an
    # uncertain outcome, aliases must require explicit teardown/reopen.
    alias_handle = p.obj_grant(d, 206)
    deny(lambda: p.obj_device_send(d, bytes((192, 1, 0, 0, 0, 0, 0, 0))), 'root hub is a toy hub')
    deny(lambda: p.obj_device_send(alias_handle, query), failed)
    deny(lambda: p.obj_device_ctl(alias_handle, 1), failed)
    deny(lambda: p.obj_device_recv(alias_handle), 'device: no completion')
    p.obj_close(d)
    p.obj_close(alias_handle)
    d = p.obj_device_open(c, 1, dev, 0)
    ok(len(exchange(d, query)) == 4, 'explicit close and reopen recovers root session')
    p.obj_close(h)

    # Existing USBIOCTL performs bus enumeration only; all peripheral data
    # transfers below exercise the T12 OBJ device session and actual HCI ring.
    svc = p.obj_devices_open(2248)
    meta_svc = p.obj_devices_open()
    deny(lambda: p.obj_device_ctl(meta_svc, 1, bytearray(4)), perm)
    deny(lambda: p.obj_device_open(meta_svc, 5, dev, 0, 0, epo(1, 2)), perm)
    p.obj_close(meta_svc)
    data_svc = p.obj_devices_open(2184)
    deny(lambda: p.obj_device_open(data_svc, 5, dev, 0, 0, epo(1, 2)), perm)
    deny(lambda: p.obj_device_open(data_svc, 5, 9999, 0, 0, epo(0, 2)), bad)
    p.obj_close(data_svc)
    ctl = p.obj_device_open(svc, 5, dev, 0, 0, epo(1, 2))
    denied_ctl = p.obj_grant(ctl, 8)
    deny(lambda: p.obj_device_ctl(denied_ctl, 8, bytearray()), perm)
    p.obj_close(denied_ctl)
    deny(lambda: p.obj_device_ctl(ctl, 3, bytearray(1)), bad)
    usb_entries = [e for e in p.obj_device_list(svc) if e[1] in (1, 5)]
    port = 0
    print('T12-USB-ENTRIES', usb_entries)
    for candidate in range(1, 9):
        status = exchange(d, bytes((163, 0, 0, 0, candidate, 0, 4, 0)))
        print('T12-ROOT-PORT',candidate,status)
        if status[0] & 1:
            port = candidate
            break
    ok(port != 0, 'emulated downstream peripheral present on root port')
    exchange(d, bytes((35, 3, 8, 0, port, 0, 0, 0)))
    exchange(d, bytes((35, 3, 4, 0, port, 0, 0, 0)))
    p.sleep(100)
    status = exchange(d, bytes((163, 0, 0, 0, port, 0, 4, 0)))
    speed = 1 if status[1] & 2 else (2 if status[1] & 4 else 0)
    # Usbnewdev = speed:int, port:int, reply:char[16], amd64 little endian.
    newdev = bytearray(24)
    newdev[0] = speed
    newdev[4] = port
    ok(p.obj_device_ctl(ctl, 3, newdev) == 0, 'OBJ administration prepares real child endpoint')
    name = bytes(newdev[8:]).split(b'\x00')[0].decode()
    child = int(name[2:].split('.')[0])
    child_ctl = p.obj_device_open(svc, 5, child, 0, 0, epo(1, 2))
    p.obj_device_ctl(child_ctl, 8, bytearray())  # existing USBaddress
    peripheral = p.obj_device_open(c, 1, child, 0)
    descriptor_req = bytes((128, 6, 0, 1, 0, 0, 18, 0))
    descriptor = exchange(peripheral, descriptor_req)
    ok(len(descriptor) == 18 and descriptor[:2] == b'\x12\x01', 'downstream USB DEVICE descriptor over HCI')
    print('T12-PERIPHERAL', child, 'descriptor', descriptor)
    send = p.obj_grant(peripheral, 14)
    ok(exchange(send, descriptor_req) == descriptor, 'read-only descriptor with attenuated Vsend')
    deny(lambda: p.obj_device_send(send, bytes((0, 9, 0, 0, 0, 0, 0, 0))), perm)
    deny(lambda: p.obj_device_send(send, bytes((192, 1, 0, 0, 0, 0, 0, 0))), perm)
    p.obj_close(send)
    short = exchange(peripheral, bytes((128, 6, 0, 1, 0, 0, 0, 16)))
    ok(short == descriptor, 'real downstream short response is exact immutable content')
    # Side-effecting standard request requires Vcontrol; it changes USB
    # configuration and completes in the HCI, unlike a root-hub stub.
    ok(exchange(peripheral, bytes((0, 9, 0, 0, 0, 0, 0, 0))) == b'', 'downstream OUT SET_CONFIGURATION completion')
    # Independent processes share one session.  Each actor receives only
    # after its own SEND succeeds; Einuse is explicit one-slot backpressure.
    rd, wr = p.pipenew()
    go_read, go_write = p.pipenew()
    sh = p.obj_open_space(b'init')
    child_code = """import plan9 as p
hs=p.obj_list()
info=[]
for h in hs:
    try:
        info.append((h,p.obj_info(h)))
    except OSError:
        pass
d=next(h for h,i in info if i[8]==14)
go=next(h for h,i in info if i[8]==6 and i[10]&1)
out=next(h for h,i in info if i[8]==6 and i[10]&256)
p.obj_write(out,b'READY')
assert p.obj_read(go,1,5000)==b'G'
count=0
for attempt in range(256):
    try:
        seq=p.obj_device_send(d,bytes((128,6,0,1,0,0,18,0)))
    except OSError as e:
        assert str(e)=='device or object already in use',str(e)
        p.sleep(2)
        continue
    h,done,family,size=p.obj_device_recv(d)
    assert seq==done and size==18
    p.obj_close(h)
    count+=1
    if count==12:
        break
    p.sleep(2)
assert count==12
p.obj_write(out,b'DONE 12')
"""
    seq = p.obj_device_send(peripheral, descriptor_req)
    tmp, done, fam, length = p.obj_device_recv(peripheral)
    p.obj_close(tmp)
    pid = space.spawn(0x4f45584543000003, handle=sh,
        argv=('micropython', '-c', child_code),
        vhandles=((peripheral,14),(go_read,9),(wr,264)))
    p.obj_close(go_read)
    p.obj_close(wr)
    ok(p.obj_read(rd,5,10000)==b'READY', 'independent child ready on shared session')
    p.obj_write(go_write,b'G')
    p.obj_close(go_write)
    count = 0
    for attempt in range(256):
        try:
            p.obj_device_send(peripheral,descriptor_req)
        except OSError as e:
            assert str(e)==busy,str(e)
            p.sleep(2)
            continue
        tmp, done, fam, length = p.obj_device_recv(peripheral)
        assert p.obj_read(tmp,4096)==descriptor
        p.obj_close(tmp)
        count += 1
        if count==12:
            break
        p.sleep(2)
    ok(count==12, 'parent completes concurrent peripheral transfers')
    ok(p.obj_read(rd,7,10000)==b'DONE 12', 'child completes concurrent peripheral transfers')
    reaped, status = p.wait()
    ok(reaped==pid and status=='', 'exact successful child PID and exit status')
    p.obj_close(rd)
    final = p.obj_device_send(peripheral,descriptor_req)
    tmp, done, fam, length = p.obj_device_recv(peripheral)
    p.obj_close(tmp)
    ok(final==seq+25, '24 shared transfers preserve one session sequence')

    # Leave the child as the final device owner, with a pending response at
    # exit; the next open must acquire the exclusive DATA claim again.
    rd, wr = p.pipenew()
    go_read, go_write = p.pipenew()
    exit_code = child_code.split('count=0')[0] + """p.obj_device_send(d,bytes((128,6,0,1,0,0,18,0)))
p.obj_write(out,b'SENT')
"""
    pid = space.spawn(0x4f45584543000003, handle=sh,
        argv=('micropython','-c',exit_code),
        vhandles=((peripheral,14),(go_read,9),(wr,264)))
    p.obj_close(go_read)
    p.obj_close(wr)
    assert p.obj_read(rd,5,10000)==b'READY'
    p.obj_close(peripheral)
    p.obj_write(go_write,b'G')
    p.obj_close(go_write)
    assert p.obj_read(rd,4,10000)==b'SENT'
    reaped, status = p.wait()
    ok(reaped==pid and status=='', 'last-owner process exits with pending completion')
    p.obj_close(rd)
    p.obj_close(sh)
    peripheral = p.obj_device_open(c,1,child,0)
    ok(exchange(peripheral,descriptor_req)==descriptor, 'process exit frees response and exclusive endpoint')

    # A real peripheral STALL occurs after the setup has reached the HCI.
    # The failed session cannot be silently reused, including via aliases.
    alias_handle = p.obj_grant(peripheral,206)
    deny(lambda: p.obj_device_send(peripheral,bytes((128,6,0,255,0,0,18,0))), 'Stall Error')
    deny(lambda: p.obj_device_send(alias_handle,descriptor_req),failed)
    deny(lambda: p.obj_device_recv(alias_handle),'device: no completion')
    p.obj_close(peripheral)
    p.obj_close(alias_handle)
    peripheral = p.obj_device_open(c,1,child,0)
    ok(exchange(peripheral,descriptor_req)==descriptor, 'explicit reopen after actual peripheral STALL')

    # Check idle detach through existing USB administration, then close both
    # old aliases.  HCI transfers interrupted by hot unplug are not simulated.
    seq = p.obj_device_send(peripheral, descriptor_req)
    detached_reply, got, fam, length = p.obj_device_recv(peripheral)
    p.obj_device_ctl(child_ctl, 4, bytearray())  # existing USBdetach
    deny(lambda: p.obj_device_send(peripheral, descriptor_req), 'device is detached')
    deny(lambda: p.obj_device_send(peripheral, descriptor_req), failed)
    p.obj_close(peripheral)
    p.obj_close(child_ctl)
    ok(p.obj_read(detached_reply, 4096) == descriptor, 'response survives actual native detach and close')
    p.obj_close(detached_reply)
    p.obj_close(ctl)
    p.obj_close(svc)
    p.obj_close(d)
    data_svc = p.obj_devices_open(2184)
    data_ep = p.obj_device_open(data_svc, 5, dev, 0, 0, epo(0, 2))
    ok(p.obj_info(data_ep)[8] == p.VKIND_USB_EP,
       'Vcreate-only service retains USBDATA endpoint open')
    p.obj_close(data_ep)
    p.obj_close(data_svc)
    p.obj_close(c)
    shared.n = 1
    print('T12-FINISH', n[0], 'PASS')
    return 1
