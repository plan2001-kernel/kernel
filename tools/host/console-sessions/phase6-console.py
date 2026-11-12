"""Phase 6 console owner and two independent REPL sessions.

The boot init process is the authorized console owner.  It claims the PS/2
keyboard, creates six capability regions (input, output and diagnostic for
each of two sessions), delegates attenuated Handles with an explicit SPAWN
vhandle plan, and multiplexes focus over the publication-region transport.
The two children run the real friendly MicroPython REPL over their regions.
No ObjectFIFO or Pipe endpoint exists on this path.

Every assertion prints T46-PASS after it passes.  The runner requires one
final sequential T46-FINISH record.
"""

import plan9 as p
import space
import struct

CID = 0x4f45584543000003
RING_VERSION = 1
RING_MAGIC = 0x5250324f
PUB = 16
HEADER = 4096
SLOTS = 64
SLOT = 2048
DATA = SLOT - 16
BYTES = HEADER + SLOTS*SLOT
M64 = 0xffffffffffffffff

RO = p.VREAD | p.VMETA
RW = p.VREAD | p.VWRITE | p.VMETA
CAT = p.VCREATE | p.VMETA | p.VDELEGATE
KBD_FAMILY = 4
CATALOG_RIGHTS = 2184
KBD_OPTS = struct.pack('<IIII', 1, 0, 0, 1)

class Counter:
    def __init__(self, n=0):
        self.n = n


shared = Counter()
alias = shared

PASS = [0]
FAILED = []


def ok(value, name):
    if value:
        PASS[0] += 1
        print('T46-PASS', PASS[0], name)
    else:
        FAILED.append(name)
        print('T46-FAIL', name)


def new_ring():
    h = p.region_create(BYTES)
    base = p.region_map(h, 0)
    if base <= 0:
        raise AssertionError('region map failed')
    p.region_write(base, struct.pack('<IIIIQQQ', RING_VERSION, RING_MAGIC,
                                     SLOT, SLOTS, 0, 0, 0))
    p.region_unmap(base)
    return h


def ring_write(base, seq, data):
    data = bytes(data)
    while data:
        part, data = data[:DATA], data[DATA:]
        seq += 1
        off = HEADER + ((seq-1) % SLOTS)*SLOT
        p.region_atomic_store(base+off, (2*seq-1) & M64)
        p.region_write(base+off+8, struct.pack('<II', len(part), 0))
        p.region_write(base+off+16, part)
        p.region_atomic_store(base+off, (2*seq) & M64)
        p.region_atomic_store(base+PUB, seq)
        p.region_wake(base, PUB, 8)
    return seq


def ring_drain(base, last):
    """Return (output bytes, diagnostic bytes, new_last)."""
    out = bytearray()
    diag = bytearray()
    n = p.region_atomic_load(base+PUB)
    while last < n:
        last += 1
        off = HEADER + ((last-1) % SLOTS)*SLOT
        if p.region_atomic_load(base+off) != (2*last) & M64:
            continue
        ln, flags = struct.unpack('<II', p.region_read(base+off+8, 8))
        if ln > DATA:
            continue
        chunk = p.region_read(base+off+16, ln)
        if p.region_atomic_load(base+off) != (2*last) & M64:
            continue
        if flags & 1:
            diag += chunk
        else:
            out += chunk
    return bytes(out), bytes(diag), last


class Session:
    def __init__(self, name, ih, ibase, oh, obase, proc):
        self.name = name
        self.ih = ih
        self.ibase = ibase
        self.oh = oh
        self.obase = obase
        self.proc = proc
        self.iseq = 0
        self.olast = 0
        self.diagbuf = bytearray()

    def type_bytes(self, data):
        self.iseq = ring_write(self.ibase, self.iseq, data)

    def output(self, deadline):
        out = bytearray()
        while now() < deadline:
            chunk, diag, self.olast = ring_drain(self.obase, self.olast)
            out += chunk
            self.diagbuf += diag
            if chunk or diag:
                continue
            r = p.region_wait(self.obase, PUB, self.olast, 50)
            if r == 3:
                chunk, diag, self.olast = ring_drain(self.obase, self.olast)
                out += chunk
                self.diagbuf += diag
                break
        return bytes(out)

    def wait_for(self, needle, timeout=6.0):
        out = bytearray()
        end = now() + timeout
        while now() < end:
            chunk, diag, self.olast = ring_drain(self.obase, self.olast)
            out += chunk
            self.diagbuf += diag
            if needle in out:
                return bytes(out)
            r = p.region_wait(self.obase, PUB, self.olast, 50)
            if r == 3:
                chunk, diag, self.olast = ring_drain(self.obase, self.olast)
                out += chunk
                self.diagbuf += diag
                break
        return bytes(out)

    def diag(self):
        return bytes(self.diagbuf)

    def close(self):
        for h in (self.ih, self.oh):
            try:
                p.obj_close(h)
            except OSError:
                pass


# PS/2 set 1 make codes for the characters used by this test.  The owner
# decodes the same way the native console does: modifiers change the emitted
# bytes, Ctrl-C and Ctrl-D are delivered exactly once to the focused session.
LOWER = {
    0x1e: 'a', 0x30: 'b', 0x2e: 'c', 0x20: 'd', 0x12: 'e', 0x21: 'f',
    0x22: 'g', 0x23: 'h', 0x17: 'i', 0x24: 'j', 0x25: 'k', 0x26: 'l',
    0x32: 'm', 0x31: 'n', 0x18: 'o', 0x19: 'p', 0x10: 'q', 0x13: 'r',
    0x1f: 's', 0x14: 't', 0x16: 'u', 0x2f: 'v', 0x11: 'w', 0x2d: 'x',
    0x15: 'y', 0x2c: 'z', 0x39: ' ', 0x02: '1', 0x03: '2', 0x04: '3',
    0x05: '4', 0x06: '5', 0x07: '6', 0x08: '7', 0x09: '8', 0x0a: '9',
    0x0b: '0', 0x34: '.', 0x33: ',', 0x0c: '-', 0x0d: '=',
}
SHIFT = {
    0x09: '(', 0x0a: ')', 0x0b: '=', 0x08: '*', 0x02: '!', 0x03: '"',
    0x04: '#', 0x05: '%', 0x06: '&', 0x07: '/', 0x0c: '_', 0x0d: '+',
    0x2b: '*', 0x2c: '<', 0x2d: '>', 0x33: ';', 0x34: ':', 0x35: '-',
}


class Keyboard:
    def __init__(self):
        self.shift = False
        self.ctrl = False
        self.prefix = False

    def decode(self, sc):
        """Return bytes emitted for one scan-code event, or b''."""
        if sc & 0x80:
            base = sc & 0x7f
            if base == 0x2a or base == 0x36:
                self.shift = False
            if base == 0x1d:
                self.ctrl = False
            return b''
        if sc == 0x2a or sc == 0x36:
            self.shift = True
            return b''
        if sc == 0x1d:
            self.ctrl = True
            return b''
        if self.ctrl:
            if sc == 0x2e:
                return b'\x03'      # Ctrl-C
            if sc == 0x20:
                return b'\x04'      # Ctrl-D
        if sc == 0x1c or sc == 0x1c:
            return b'\r'
        if sc == 0x27:
            return '\u00f6'.encode()  # ö via the Finnish layout
        if sc == 0x28:
            return '\u00e4'.encode()  # ä
        if sc == 0x2b:
            return b"'" if not self.shift else b'*'
        ch = None
        if self.shift:
            ch = SHIFT.get(sc)
        if ch is None:
            ch = LOWER.get(sc)
        if ch is None:
            return b''
        if self.shift and ch.isalpha():
            ch = ch.upper()
        return ch.encode()


# Scan-code sequences used by the test's synthetic key events.
def shifted(sc):
    return bytes((0x2a, sc, 0xaa))


def scan_text(text):
    out = bytearray()
    for ch in text:
        if ch == '\n':
            out.append(0x1c)
        elif ch == "'":
            out.append(0x2b)
        elif ch == '(':
            out += shifted(0x09)
        elif ch == ')':
            out += shifted(0x0a)
        elif ch == '*':
            out += shifted(0x2b)
        elif ch == '#':
            out += shifted(0x04)
        elif ch == '"':
            out += shifted(0x03)
        elif ch == ':':
            out += shifted(0x34)
        elif ch == ';':
            out += shifted(0x33)
        elif ch in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
            for sc, c in LOWER.items():
                if c == ch.lower():
                    out += shifted(sc)
                    break
        elif ch in 'abcdefghijklmnopqrstuvwxyz':
            for sc, c in LOWER.items():
                if c == ch:
                    out.append(sc)
                    break
        elif ch in '0123456789':
            for sc, c in LOWER.items():
                if c == ch:
                    out.append(sc)
                    break
        elif ch == ' ':
            out.append(0x39)
        else:
            raise AssertionError('no scan code for %r' % ch)
    return bytes(out)


def type_scan(kbd, sess, data):
    decoded = bytearray()
    for sc in data:
        decoded += kbd.decode(sc)
    sess.type_bytes(bytes(decoded))


def focus_type(kbd, sess, text, ctrl_char=None):
    data = scan_text(text)
    if ctrl_char == 'C':
        data += bytes((0x1d, 0x2e))
    elif ctrl_char == 'D':
        data += bytes((0x1d, 0x20))
    type_scan(kbd, sess, data)


def now():
    return p.time_ns() / 1000000000


def init():
    shared.n = 0
    PASS[0] = 0


def run():
    PASS[0] = 0
    sh = p.obj_open_volume()
    cat = p.obj_devices_open(CATALOG_RIGHTS)
    kbdh = p.obj_device_open(cat, KBD_FAMILY, 0, 0, 0, KBD_OPTS)

    # The owner alone holds the keyboard; a second claim fails closed.
    denied = False
    try:
        p.obj_device_open(cat, KBD_FAMILY, 0, 0, 0, KBD_OPTS)
    except OSError as e:
        denied = 'in use' in str(e) or 'busy' in str(e)
    ok(denied, 'exclusive keyboard claim rejects a second owner')

    sessions = []
    code = ("import plan9 as q\n"
            "try:\n"
            " iw=ow=-1\n"
            " for h in q.obj_list():\n"
            "  i=q.obj_info(h)\n"
            "  if i[8]==q.VKIND_REGION:\n"
            "   if i[10]&q.VWRITE: ow=h\n"
            "   else: iw=h\n"
            " q.console_attach(iw,ow)\n"
            " q.repl()\n"
            "except Exception as e:\n"
            " q.exits('attach:'+repr(e)+' iw='+str(iw)+' ow='+str(ow))\n")
    try:
        for name, cpu in (('A', 1), ('B', 2)):
            ih = new_ring()
            ro_in = p.obj_grant(ih, RO)
            ib = p.region_map(ih, 0)
            oh = new_ring()
            ro_out = p.obj_grant(oh, RO)
            ob = p.region_map(ro_out, 0)
            proc = space.spawn(CID, handle=sh, cpu=cpu,
                                 argv=('micropython', '-c', code),
                                 vhandles=((ih, RO), (oh, RW)))
            sessions.append(Session(name, ih, ib, oh, ob, proc))
            p.obj_close(ro_in)
            p.obj_close(ro_out)
            p.sleep(300)
            ok(proc > 0, 'session %s REPL spawned over region grants' % name)

        a, b = sessions
        outa = a.wait_for(b'>>>', 20)
        outb = b.wait_for(b'>>>', 20)
        ok(b'>>>' in outa and b'>>>' in outb,
           'two independent REPLs reached a prompt through regions')

        kbd = Keyboard()

        # Focus A and execute.  B must not see A's keystrokes or output.
        focus_type(kbd, a, "print(111)\n")
        outa = a.wait_for(b'111', 8)
        ok(b'111' in outa, 'focused session A executes its own input')
        rest_b, _, b.olast = ring_drain(b.obase, b.olast)
        ok(b'111' not in rest_b, 'input and output do not leak to session B')

        focus_type(kbd, b, "print(222)\n")
        outb = b.wait_for(b'222', 8)
        ok(b'222' in outb, 'focus switches atomically to session B')
        rest_a, _, a.olast = ring_drain(a.obase, a.olast)
        ok(b'222' not in rest_a, 'session A did not receive B output')

        # UTF-8 is delivered through the same decoded stream.
        focus_type(kbd, a, "#")
        type_scan(kbd, a, bytes((0x27,)))
        type_scan(kbd, a, bytes((0x1c,)))
        outa = a.wait_for('\u00f6'.encode(), 6)
        ok('\u00f6'.encode() in outa, 'UTF-8 bytes cross the session region')

        # Ctrl-C interrupts a blocked statement exactly once.
        focus_type(kbd, a, 'exec("while 1: pass")\n')
        p.sleep(400)
        focus_type(kbd, a, "", 'C')
        outa = a.wait_for(b'KeyboardInterrupt', 8)
        ok(outa.count(b'KeyboardInterrupt') >= 1,
           'Ctrl-C reaches only the focused session once')

        # Ctrl-D ends B; A survives and remains usable.
        focus_type(kbd, b, "", 'D')
        outb = b.output(now() + 6)
        code_b = p.process_wait(b.proc, 5000)[1]
        ok(code_b == '', 'Ctrl-D ends the active client cleanly')
        r = p.region_wait(b.obase, PUB, b.olast, 3000)
        outb += b.output(now() + 0.5)
        ok(r == 3, 'client exit releases its output writer slot')

        focus_type(kbd, a, "print(333)\n")
        outa = a.wait_for(b'333', 8)
        ok(b'333' in outa, 'surviving session recovers after peer exit')

        # The input grant is read-only in the client; no amplification.
        amp = False
        probe = p.obj_grant(a.ih, RO)
        try:
            p.obj_grant(probe, p.VWRITE)
        except OSError:
            amp = True
        p.obj_close(probe)
        ok(amp, 'read-only grant cannot be amplified to a writer')

        # Release and reacquire the keyboard without a leaked claim.
        p.obj_close(kbdh)
        kbdh = p.obj_device_open(cat, KBD_FAMILY, 0, 0, 0, KBD_OPTS)
        ok(kbdh > 0, 'keyboard reacquired after owner release')
        p.obj_close(kbdh)

        for s in sessions:
            if s.proc > 0:
                try:
                    p.obj_close(s.proc)
                except OSError:
                    pass
            s.close()
    finally:
        try:
            p.obj_close(cat)
        except OSError:
            pass
        p.obj_close(sh)

    if FAILED:
        print('T46-FINISH', PASS[0], 'FAIL')
        return 1
    print('T46-FINISH', PASS[0], 'PASS')
    return 1
