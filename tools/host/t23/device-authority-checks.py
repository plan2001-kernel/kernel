# Issue #23 Part B focused checks: one catalog authority chain for UART, PS/2
# keyboard, USB endpoint/session opening, option validation and attenuation.
# Seeded with mkovol's ordinary SOURCE argument and run through init.run().
# USB needs the disposable QEMU xHCI + mouse supplied by the runner.
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

    def ok(value, name):
        assert value, name
        n[0] += 1
        print('T23-PASS', n[0], name)

    def deny(fn, message):
        try:
            fn()
        except OSError as e:
            ok(str(e) == message, 'exact error: ' + message)
            return
        raise AssertionError('expected ' + message)

    def skip(name, why):
        print('T23-SKIP', name, why)

    def opt(mode=0, kind=0, flags=0):
        return struct.pack('<IIII', 1, mode, kind, flags)

    bad = 'bad arg in system call'
    perm = 'permission denied'
    busy = 'device or object already in use'

    cat = p.obj_devices_open(2184)
    entries = p.obj_device_list(cat)
    total = p.obj_device_list(cat, 0)
    ok(len(entries) == total and len(entries) != 0, 'bounded catalog count')
    ok(all(len(e) == 9 and e[0] == 2 for e in entries), 'versioned records')
    uarts = [e for e in entries if e[1] == p.OBJ_DEVICE_UART]
    kbds = [e for e in entries if e[1] == p.OBJ_DEVICE_KBD]
    usbctl = [e for e in entries if e[1] == p.OBJ_DEVICE_USB_CONTROL]
    usbeps = [e for e in entries if e[1] == p.OBJ_DEVICE_USB_ENDPOINT]
    eths = [e for e in entries if e[1] == p.OBJ_DEVICE_ETHERNET]
    ok(len(uarts) >= 1, 'UART family discovered')
    ok(len(kbds) == 1 and kbds[0][2] == 0, 'PS/2 keyboard discovered')
    ok(len(usbctl) >= 1 and len(usbeps) >= len(usbctl), 'USB families discovered')
    if eths:
        ok(all(e[3] == 0 for e in eths), 'Ethernet selectors')
    else:
        skip('Ethernet discovery', 'no NIC modelled in this boot')

    meta = p.obj_devices_open()
    deny(lambda: p.obj_device_open(meta, p.OBJ_DEVICE_UART,
         uarts[0][2], 0, 0, opt(2)), perm)
    deny(lambda: p.obj_device_open(meta, p.OBJ_DEVICE_KBD,
         0, 0, 0, opt(0, 0, 1)), perm)
    deny(lambda: p.obj_device_open(meta, p.OBJ_DEVICE_USB_CONTROL,
         usbctl[0][2], usbctl[0][3]), perm)
    p.obj_close(meta)

    deny(lambda: p.obj_device_open(cat, 99, 0, 0), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         uarts[0][2], 1, 0, opt(2)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         uarts[0][2], 0, 0), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         uarts[0][2], 0, 0, b'\x01\x00\x00\x00'), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         uarts[0][2], 0, 0, struct.pack('<IIII', 9, 2, 0, 0)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         uarts[0][2], 0, 0, opt(3)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         uarts[0][2], 0, 0, opt(2, 0, 1)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         999, 0, 0, opt(2)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_KBD,
         0, 0, 0, opt(0, 0, 0)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_KBD,
         0, 0, 0, opt(2, 0, 1)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_KBD,
         1, 0, 0, opt(0, 0, 1)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_USB_ENDPOINT,
         usbeps[0][2], usbeps[0][3], 0), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_USB_ENDPOINT,
         usbeps[0][2], usbeps[0][3], 0, opt(2, 9, 0)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_USB_ENDPOINT,
         usbeps[0][2], usbeps[0][3], 0, opt(9, 0, 0)), bad)
    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_USB_ENDPOINT,
         9999, 0, 0, opt(2, 0, 0)), bad)

    deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_UART,
         uarts[0][2], 0, 8 + 128 + 256 + 1, opt(1)), perm)
    if len(uarts) > 1:
        try:
            u = p.obj_device_open(cat, p.OBJ_DEVICE_UART,
                                  uarts[1][2], 0, 0, opt(1))
        except OSError as e:
            skip('UART byte stream', str(e))
        else:
            ok(p.obj_info(u)[8] == p.VKIND_UART, 'catalog-opened UART kind')
            deny(lambda: p.obj_write(u, b'x' * (p.OBJ_STREAM_MAXIO + 1),
                 0), bad)
            deny(lambda: p.obj_read(u, 1), perm)
            att = p.obj_grant(u, 8 + 128 + 256)
            ok(p.obj_write(att, b'ATT', 0) == 3,
               'attenuated Vwrite UART stream')
            deny(lambda: p.obj_uart_ctl(att, 115200, 8, 1, 0), perm)
            p.obj_close(att)
            ok(p.obj_write(u, b'T23-UART', 0) == 8, 'bounded UART write')
            ok(p.obj_uart_ctl(u, 115200, 8, 1, 0) == 0, 'UART configuration')
            ok(len(p.obj_uart_status(u, 64)) > 0, 'UART status')
            p.obj_close(u)
            skip('UART read', 'COM2 has no model/loopback peer')
        try:
            w = p.obj_device_open(cat, p.OBJ_DEVICE_UART,
                                  uarts[1][2], 0, 0, opt(2))
        except OSError as e:
            skip('delegated UART child', str(e))
        else:
            sh = p.obj_open_space(b'init')
            child = """import plan9 as p
hs=p.obj_list()
h=next(h for h in hs if p.obj_info(h)[8]==7)
assert p.obj_write(h,b'CHILD',0)==5
p.obj_close(h)
"""
            pid = space.spawn(0x4f45584543000003, handle=sh,
                argv=('micropython', '-c', child),
                vhandles=((w, 8 + 128 + 256),))
            reaped, status = p.wait()
            ok(reaped == pid and status == '',
               'non-privileged child writes via delegated UART Handle')
            p.obj_close(sh)
            p.obj_close(w)
            w = p.obj_device_open(cat, p.OBJ_DEVICE_UART,
                                  uarts[1][2], 0, 0, opt(1))
            ok(w > 0, 'UART close/reopen after child exit')
            p.obj_close(w)
    else:
        skip('UART byte stream', 'only the console UART is present')

    kopt = opt(0, 0, 1)
    try:
        k = p.obj_device_open(cat, p.OBJ_DEVICE_KBD, 0, 0, 0, kopt)
    except OSError as e:
        ok(str(e) == busy or str(e) == 'PS/2 keyboard unavailable',
           'exclusive PS/2 claim is not silently shared')
    else:
        deny(lambda: p.obj_device_open(cat, p.OBJ_DEVICE_KBD,
             0, 0, 0, kopt), busy)
        ctl = p.obj_grant(k, 64)
        ok(p.obj_kbd_leds(ctl, 0) == 0,
           'delegated Vcontrol Handle sets LEDs')
        p.obj_close(ctl)
        meta_k = p.obj_grant(k, 8)
        deny(lambda: p.obj_kbd_leds(meta_k, 0), perm)
        p.obj_close(meta_k)
        p.obj_close(k)
        k = p.obj_device_open(cat, p.OBJ_DEVICE_KBD, 0, 0, 0, kopt)
        ok(k > 0, 'KBD exclusive close/reopen')
        p.obj_close(k)
        skip('KBD scan-code read', 'no deterministic QEMU scan-code source')

    d = p.obj_device_open(cat, p.OBJ_DEVICE_USB_CONTROL,
                          usbctl[0][2], usbctl[0][3])
    query = bytes((163, 0, 0, 0, 1, 0, 4, 0))
    seq = p.obj_device_send(d, query)
    deny(lambda: p.obj_device_send(d, query), busy)
    h, done, family, length = p.obj_device_recv(d)
    data = p.obj_read(h, 16)
    p.obj_close(h)
    ok(done == seq and family == p.OBJ_DEVICE_USB_CONTROL and
       len(data) == length == 4, 'one pending USB immutable completion')
    deny(lambda: p.obj_device_recv(d), 'device: no completion')
    p.obj_close(d)
    d = p.obj_device_open(cat, p.OBJ_DEVICE_USB_CONTROL,
                          usbctl[0][2], usbctl[0][3])
    ok(d > 0, 'USB control session close/reopen')
    p.obj_close(d)

    data_svc = p.obj_devices_open(2184)
    deny(lambda: p.obj_device_ctl(data_svc, 1, bytearray(4)), perm)
    p.obj_close(data_svc)
    admincat = p.obj_devices_open(2248)
    admin = p.obj_grant(admincat, 8 + 64 + 128)
    ok(p.obj_device_ctl(admin, 1, bytearray(struct.pack('<i', 0))) == 0,
       'catalog Vcontrol USB debug administration')
    p.obj_close(admin)
    p.obj_close(admincat)

    p.obj_close(cat)
    shared.n = 1
    print('T23-FINISH', n[0], 'PASS')
    return 1
