"""Phase 5 acceptance: independent process capabilities and completion.

Real separate MicroPython processes run on the AMD64 QEMU guest.  A process
Handle is the authority; no numeric PID, guessed Handle or ancestry creates
control or observation.  Completion is an immutable, repeatable record that
survives the launcher and may be read by several independent observers.

Every assertion prints T41-PASS immediately after it passes.  The runner
requires one final sequential T41-FINISH.
"""

CID = 0x4f45584543000003

PRELUDE = r'''
import plan9 as p
import space

def fail(s):
    p.exits('t41:' + s)

def findkind(kind, need=0):
    for h in p.obj_list():
        try:
            i = p.obj_info(h)
        except OSError:
            continue
        if i[8] != kind:
            continue
        if need == 0 or (i[10] & need) == need:
            return h
    return 0

def findregion(write):
    for h in p.obj_list():
        try:
            i = p.obj_info(h)
        except OSError:
            continue
        if i[8] == p.VKIND_REGION and bool(i[10] & p.VWRITE) == write:
            return h
    return 0

def publish(base, data, word=1, off=0):
    p.region_write(base + 64, data)
    p.region_atomic_store(base + off, word)
    p.region_wake(base, off, 0x7fffffff)
'''

# A long-lived target.  Its only communication is the exit status/cause.
B_LONG = PRELUDE + r'''
p.sleep(@MS@)
p.exits('')
'''

# C in the launcher-first chain: it receives B's observation Handle and a
# result-region writer from A.  It proves it cannot terminate through an
# observe-only grant, sees B running after A has launched it, and reads the
# immutable completion twice.
C_CHAIN = PRELUDE + r'''
bh = findkind(p.VKIND_PROCESS, p.VOBSERVE)
rw = findregion(True)
if not bh or not rw:
    fail('chain-handles')
try:
    p.process_terminate(bh)
except OSError:
    pass
else:
    fail('observe-can-terminate')
base = p.region_map(rw, 0x40000000)
before = p.process_info(bh)
if before[3] != p.OBJ_PROC_RUNNING:
    fail('b-not-running')
pid = before[0]
r1 = p.process_wait(bh, -1)
r2 = p.process_wait(bh, -1)
if r1[8] != 0 or r2[8] != 0 or r1[3] != p.OBJ_PROC_COMPLETED:
    fail('chain-completion')
if (r1[0] != pid or r1[0] != r2[0] or r1[1] != r2[1] or
        r1[2] != p.OBJ_PROC_CAUSE_EXIT):
    fail('chain-result')
publish(base, b'CHAIN-OK')
p.exits('')
'''

# A itself launches B, delegates B's observation Handle to an independent C,
# then exits.  B outlives A.
A_CHAIN = PRELUDE + r'''
sp = findkind(p.VKIND_GE_VOLUME, p.VEXECUTE)
rw = findregion(True)
if not sp or not rw:
    fail('a-handles')
b = space.spawn(@CID@, handle=sp,
                argv=('micropython', '-c', @BCHILD@))
if b <= 0:
    fail('a-b-spawn')
obs = p.obj_open_handle(b, p.VOBSERVE | p.VMETA | p.VDELEGATE)
if obs <= 0:
    fail('a-observe')
c = space.spawn(@CID@, handle=sp,
                argv=('micropython', '-c', @CHILD@),
                vhandles=((obs, p.VOBSERVE | p.VMETA),
                          (rw, p.VREAD | p.VWRITE | p.VMETA |
                           p.VDELEGATE)))
if c <= 0:
    fail('a-c-spawn')
p.obj_close(obs)
p.obj_close(b)
p.obj_close(rw)
p.exits('')
'''

# One of two independent observers.  It reads the same completion twice and
# records pid, cause, state and status in its own result region.
OBSERVER = PRELUDE + r'''
bh = findkind(p.VKIND_PROCESS, p.VOBSERVE)
rw = findregion(True)
if not bh or not rw:
    fail('observer-handles')
base = p.region_map(rw, 0x42000000)
r1 = p.process_wait(bh, -1)
r2 = p.process_wait(bh, -1)
if r1[8] != 0 or r2[8] != 0:
    fail('observer-timeout')
if (r1[0] != r2[0] or r1[2] != r2[2] or r1[3] != r2[3] or
        r1[1] != r2[1] or r1[4] != r2[4]):
    fail('observer-repeat')
rec = (str(r1[0]) + '|' + str(r1[2]) + '|' + str(r1[3]) + '|' +
       r1[1]).encode()
if len(rec) > 64:
    fail('observer-long')
publish(base, rec)
p.exits('')
'''

# Observer with an explicit READY barrier: it signals before entering
# ProcessWait so the launcher can prove both waiters are enrolled before B
# completes.
OBSERVER_BARRIER = PRELUDE + r'''
bh = findkind(p.VKIND_PROCESS, p.VOBSERVE)
rw = findregion(True)
if not bh or not rw:
    fail('barrier-handles')
base = p.region_map(rw, 0x42000000)
p.region_write(base + 128, b'R')
p.region_atomic_store(base + 8, 1)
p.region_wake(base, 8, 0x7fffffff)
r1 = p.process_wait(bh, -1)
r2 = p.process_wait(bh, -1)
if r1[8] != 0 or r2[8] != 0:
    fail('barrier-timeout')
if (r1[0] != r2[0] or r1[2] != r2[2] or r1[3] != r2[3] or
        r1[1] != r2[1] or r1[4] != r2[4]):
    fail('barrier-repeat')
rec = (str(r1[0]) + '|' + str(r1[2]) + '|' + str(r1[3]) + '|' +
       r1[1]).encode()
publish(base, rec)
p.exits('')
'''

# Observer for the deadline-vs-exit race.  It checks the atomic snapshot
# invariant on every return: result 1 must not carry COMPLETED.
RACE_OBSERVER = PRELUDE + r'''
bh = findkind(p.VKIND_PROCESS, p.VOBSERVE)
rw = findregion(True)
if not bh or not rw:
    fail('race-handles')
base = p.region_map(rw, 0x46000000)
timeouts = 0
while True:
    r = p.process_wait(bh, 3)
    if r[8] == 0:
        if r[3] != p.OBJ_PROC_COMPLETED:
            fail('race-zero-not-completed')
        break
    if r[3] == p.OBJ_PROC_COMPLETED:
        fail('race-timeout-completed')
    timeouts += 1
    if timeouts > 100000:
        fail('race-runaway')
r2 = p.process_wait(bh, 0)
if (r2[8] != 0 or r2[3] != p.OBJ_PROC_COMPLETED or r2[0] != r[0] or
        r2[1] != r[1] or r2[2] != r[2]):
    fail('race-repeat')
rec = (str(timeouts) + '|' + str(r[0]) + '|' + str(r[2]) + '|' +
       r[1]).encode()
publish(base, rec)
p.exits('')
'''

# B waits for a launcher-controlled publication word, then exits.
GO_B = PRELUDE + r'''
rh = findregion(False)
if not rh:
    fail('go-handle')
base = p.region_map(rh, 0x47000000)
while True:
    v = p.region_atomic_load(base)
    if v == 1:
        break
    p.region_wait(base, 0, v, 1000)
p.exits('')
'''

# The delegated controller has both authorities.  It proves the observation
# grant cannot be escalated and cannot terminate; the control grant can.
CONTROLLER = PRELUDE + r'''
obs = findkind(p.VKIND_PROCESS, p.VOBSERVE)
ctl = findkind(p.VKIND_PROCESS, p.VCONTROL)
rw = findregion(True)
if not obs or not ctl or not rw:
    fail('controller-handles')
base = p.region_map(rw, 0x44000000)
try:
    p.process_terminate(obs)
except OSError:
    pass
else:
    fail('observer-terminated')
try:
    p.obj_open_handle(obs, p.VCONTROL)
except OSError:
    pass
else:
    fail('observer-minted-control')
try:
    p.process_wait(ctl, 0)
except OSError:
    pass
else:
    fail('control-observe')
p.process_terminate(ctl)
r = p.process_wait(obs, -1)
if r[2] != p.OBJ_PROC_CAUSE_TERMINATED:
    fail('termination-cause')
if r[1] == '':
    fail('termination-status')
publish(base, b'TERM-OK')
p.exits('')
'''

# Faulting writer: publishes an immutable payload, then faults with its RW
# mapping still installed.  Completion must release the writer slot.
FAULTER = PRELUDE + r'''
rw = findregion(True)
if not rw:
    fail('faulter-handle')
base = p.region_map(rw, 0x43000000)
publish(base, b'FAULT-DATA')
p.sleep(100)
p.region_write(0x50000000, b'X')
p.exits('no-fault')
'''

# Launcher that deliberately keeps no observer and exits.
NO_OBSERVER = PRELUDE + r'''
sp = findkind(p.VKIND_GE_VOLUME, p.VEXECUTE)
if not sp:
    fail('noobs-space')
b = space.spawn(@CID@, handle=sp,
                argv=('micropython', '-c', @CHILD@))
p.obj_close(b)
p.exits('')
'''

WAITER = PRELUDE + r'''
bh = findkind(p.VKIND_PROCESS, p.VOBSERVE)
if not bh:
    fail('waiter-handle')
p.process_wait(bh, -1)
p.exits('waiter-not-interrupted')
'''

SHORT = PRELUDE + "p.sleep(@MS@)\np.exits('')\n"

STATUS_CHILD = PRELUDE + r'''
rw = findregion(True)
if not rw:
    fail('status-handle')
base = p.region_map(rw, 0x45000000)
publish(base, b'STATUS-DATA')
p.exits(@STATUS@)
'''

LONG_STATUS_CHILD = PRELUDE + r'''
p.exits(@STATUS@)
'''


# A real SysV notify callback executes NOTED(NCONT).  Prove delivery with a
# recoverable bad-pointer syscall before waiting with a live writer mapping.
IGNORE_TERMINATION = PRELUDE + r'''
p.sysraw(28, p.notify_probe_addr())
try:
    p.sysraw(41, 1, 128)
except OSError:
    pass
if p.notify_probe_count() != 1:
    fail('notify-not-executed')
rw = findregion(True)
base = p.region_map(rw, 0x48000000)
publish(base, b'IGNORE-READY')
while True:
    try:
        p.region_wait(base, 0, 1, -1)
    except OSError:
        pass  # deliberately ignore an ordinary note interrupt too
    # No ordinary callback or interrupted syscall may resume this target.
    p.region_write(base + 128, b'ESCAPED')
'''

RACING_CONTROLLER = PRELUDE + r'''
ctl = findkind(p.VKIND_PROCESS, p.VCONTROL)
rh = findregion(False)
base = p.region_map(rh, 0x49000000)
while p.region_atomic_load(base) == 0:
    p.region_wait(base, 0, 0, -1)
try:
    p.process_terminate(ctl)
except OSError as e:
    # The other controller may already have completed termination.
    if 'process exited' not in str(e):
        fail('controller:' + str(e))
p.exits('')
'''

# High-iteration termination stress target.  It maps a writer region and a
# shared GO region, publishes READY, waits for the launcher's GO, then
# voluntarily exits, faults or blocks in REGIONWAIT depending on the mode.
STRESS_TARGET = PRELUDE + r'''
rw = findregion(True)
go = findregion(False)
if not rw or not go:
    fail('stress-target-handles')
base = p.region_map(rw, 0x4f600000)
g = p.region_map(go, 0x4f610000)
p.region_write(base + 64, b'STRESS-DATA')
p.region_atomic_store(base, 1)
p.region_wake(base, 0, 0x7fffffff)
while p.region_atomic_load(g) == 0:
    p.region_wait(g, 0, 0, -1)
@MODE@
'''

# Independent stress controller.  It publishes READY in its own writer
# region, waits for GO, then attempts authorized termination.  Eprocdied is
# the expected outcome whenever the target completed first.
STRESS_CONTROLLER = PRELUDE + r'''
ctl = findkind(p.VKIND_PROCESS, p.VCONTROL)
go = findregion(False)
rw = findregion(True)
if not ctl or not go or not rw:
    fail('stress-controller-handles')
rbase = p.region_map(rw, @RBASE@)
g = p.region_map(go, @GBASE@)
p.region_write(rbase + 64, b'READY')
p.region_atomic_store(rbase, 1)
p.region_wake(rbase, 0, 0x7fffffff)
while p.region_atomic_load(g) == 0:
    p.region_wait(g, 0, 0, -1)
p.sleep(@CTLDELAY@)
try:
    p.process_terminate(ctl)
except OSError as e:
    if 'process exited' not in str(e):
        fail('stress-controller:' + str(e))
p.exits('')
'''

# An RO observer that starts before any writer mapping exists.  The old
# semantics returned writer-release (3) immediately and forced polling; the
# current contract blocks until a writer appears or the timeout applies.
WAIT_NO_WRITER = PRELUDE + r'''
r = findregion(False)
b = p.region_map(r, 0x4f540000)
rv = p.region_wait(b, 0, 0, 5000)
if rv == 3:
    fail('no-writer-returned-3')
if rv not in (0, 1):
    fail('no-writer-status')
p.exits('')
'''

# Region backing is charged at creation, so a bounded child must reject a
# region that does not fit its remaining budget instead of allocating it.
# At least one 16 MiB region fits beside the runtime; a later one must fail.
CREATE_OVER_BUDGET = PRELUDE + r'''
made = 0
try:
    while made < 4:
        p.region_create(16*1024*1024)
        made += 1
except OSError as e:
    if 'memory' not in str(e).lower():
        fail('budget-error:' + str(e))
    if made == 0:
        fail('budget-rejected-first-region')
    p.exits('')
p.exits('region-created-over-budget')
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

    space.interval = 3600
    n = [0]

    def ok(value, name):
        assert value, name
        n[0] += 1
        print('T41-PASS', n[0], name)

    def denied(call, name):
        try:
            call()
        except OSError:
            return
        raise AssertionError('expected rejection: ' + name)

    def rstr(base):
        b = p.region_read(base + 64, 64)
        i = b.find(b'\0')
        return b if i < 0 else b[:i]

    def waitwordoff(base, off, target, label):
        deadline = p.time_ns() + 20000000000
        while True:
            v = p.region_atomic_load(base + off)
            if v == target:
                return
            r = p.region_wait(base, off, v, 200)
            if r == 2 or r == 3:
                p.sleep(20)
            if p.time_ns() > deadline:
                raise AssertionError(label + ' timeout')

    def waitword(base, target, label):
        waitwordoff(base, 0, target, label)

    def rec(h, timeout=-1):
        return p.process_wait(h, timeout)

    def rec_ok(h, cause, label):
        r = rec(h)
        if r[8] != 0 or r[3] != p.OBJ_PROC_COMPLETED or r[2] != cause:
            raise AssertionError('%s: result=%r' % (label, (r,)))
        return r

    def spawn(code, vh=(), cpu=None):
        return space.spawn(CID, handle=sh, cpu=cpu,
                           argv=('micropython', '-c', code),
                           vhandles=vh)

    RW = p.VREAD | p.VWRITE | p.VMETA | p.VDELEGATE
    RO = p.VREAD | p.VMETA | p.VDELEGATE
    OB = p.VOBSERVE | p.VMETA | p.VDELEGATE
    CT = p.VCONTROL | p.VMETA | p.VDELEGATE
    EX = p.VEXECUTE | p.VMETA | p.VDELEGATE

    sh = p.obj_open_volume()

    # --- A launches B, delegates observation to C, then A exits -----------
    chain_code = (A_CHAIN.replace('@CID@', str(CID))
                  .replace('@BCHILD@', repr(B_LONG.replace('@MS@', '6000')))
                  .replace('@CHILD@', repr(C_CHAIN)))
    reg = p.region_create(8192)
    rw = p.obj_open_handle(reg, RW)
    ro = p.obj_open_handle(reg, RO)
    cbase = p.region_map(ro, 0x4f100000)
    a = spawn(chain_code, ((sh, EX), (rw, RW)), cpu=0)
    p.obj_close(rw)
    ra = rec_ok(a, p.OBJ_PROC_CAUSE_EXIT, 'A-exit')
    ok(ra[0] != 0 and ra[1] == '',
       'launcher A starts B, delegates C and exits independently and cleanly')
    waitword(cbase, 1, 'chain result')
    ok(rstr(cbase) == b'CHAIN-OK',
       'C observes B completion after A is gone; A owned the B Handle')
    p.obj_close(a)
    p.region_unmap(cbase)
    p.obj_close(ro)
    p.obj_close(reg)

    # --- two observers are enrolled before B can complete ------------------
    r1 = p.region_create(8192)
    r2 = p.region_create(8192)
    go = p.region_create(8192)
    rw1 = p.obj_open_handle(r1, RW)
    ro1 = p.obj_open_handle(r1, RO)
    rw2 = p.obj_open_handle(r2, RW)
    ro2 = p.obj_open_handle(r2, RO)
    gorw = p.obj_open_handle(go, RW)
    goro = p.obj_open_handle(go, RO)
    base1 = p.region_map(ro1, 0x4f200000)
    base2 = p.region_map(ro2, 0x4f210000)
    gobase = p.region_map(gorw, 0x4f220000)
    b = spawn(GO_B, ((goro, RO),), cpu=1)
    o1 = p.obj_open_handle(b, OB)
    o2 = p.obj_open_handle(b, OB)
    c = spawn(OBSERVER_BARRIER, ((o1, OB), (rw1, RW)), cpu=1)
    d = spawn(OBSERVER_BARRIER, ((o2, OB), (rw2, RW)), cpu=2)
    p.obj_close(o1)
    p.obj_close(o2)
    p.obj_close(rw1)
    p.obj_close(rw2)
    p.obj_close(goro)
    waitwordoff(base1, 8, 1, 'observer-C ready')
    waitwordoff(base2, 8, 1, 'observer-D ready')
    deadline = p.time_ns() + 20000000000
    while p.process_info(b)[9] < 2:
        if p.time_ns() > deadline:
            raise AssertionError('observers did not enroll before completion')
        p.sleep(5)
    ok(True, 'two observers are enrolled in ProcessWait before B completes')
    extra = p.obj_open_handle(b, OB)
    denied(lambda: p.obj_revoke(extra),
           'VkindProcess has no explicit revoke operation')
    p.obj_close(extra)
    ok(p.process_info(b)[9] >= 2,
       'closing an independent observe grant does not disturb blocked waiters')
    p.region_atomic_store(gobase, 1)
    p.region_wake(gobase, 0, 0x7fffffff)
    rec_ok(c, p.OBJ_PROC_CAUSE_EXIT, 'observer-C')
    rec_ok(d, p.OBJ_PROC_CAUSE_EXIT, 'observer-D')
    rbs = rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'observer-B')
    first = rstr(base1)
    second = rstr(base2)
    ok(first == second and first != b'',
       'two simultaneously blocked observers read the same result')
    ok(first.startswith(str(rbs[0]).encode() + b'|'),
       'the observer result agrees with the launcher observation')
    p.obj_close(c)
    p.obj_close(d)
    p.obj_close(b)
    p.region_unmap(gobase)
    p.region_unmap(base1)
    p.region_unmap(base2)
    p.obj_close(gorw)
    p.obj_close(ro1)
    p.obj_close(ro2)
    p.obj_close(r1)
    p.obj_close(r2)
    p.obj_close(go)

    # --- authority matrix and delegated termination -----------------------
    reg = p.region_create(8192)
    rw = p.obj_open_handle(reg, RW)
    ro = p.obj_open_handle(reg, RO)
    tbase = p.region_map(ro, 0x4f300000)
    b = spawn(B_LONG.replace('@MS@', '60000'))
    obs = p.obj_open_handle(b, OB)
    ctl = p.obj_open_handle(b, CT)
    pure = p.obj_open_handle(b, p.VCONTROL)
    denied(lambda: p.process_terminate(obs),
           'observe-only termination')
    ok(rec(b, 0)[3] == p.OBJ_PROC_RUNNING,
       'a denied observe-only termination leaves B running')
    denied(lambda: p.process_info(pure),
           'control-only metadata without Vmeta')
    denied(lambda: p.process_wait(pure, 0),
           'control-only observation without Vobserve')
    t = spawn(CONTROLLER, ((obs, OB), (ctl, CT), (rw, RW)), cpu=1)
    p.obj_close(obs)
    p.obj_close(ctl)
    p.obj_close(pure)
    p.obj_close(rw)
    rec_ok(t, p.OBJ_PROC_CAUSE_EXIT, 'controller')
    p.obj_close(t)
    waitword(tbase, 1, 'controller result')
    ok(rstr(tbase) == b'TERM-OK',
       'a delegated controller terminates B; observe-only cannot')
    rb = rec_ok(b, p.OBJ_PROC_CAUSE_TERMINATED, 'terminated-B')
    ok(rb[1] != '', 'authorized termination publishes a distinct cause/status')
    p.obj_close(b)
    p.region_unmap(tbase)
    p.obj_close(ro)
    p.obj_close(reg)

    # Handled faults must not taint a later explicit voluntary exit.
    handled = PRELUDE + r'''
p.sysraw(28, p.notify_probe_addr())
try:
    p.sysraw(41, 1, 128)
except OSError:
    pass
if p.notify_probe_count() != 1:
    fail('handled-note-not-run')
p.exits('handled-note-exit')
'''
    h = spawn(handled)
    result = rec_ok(h, p.OBJ_PROC_CAUSE_EXIT, 'handled-note-exit')
    ok(result[1] == 'handled-note-exit',
       'explicit exit after handled NCONT note has voluntary cause')
    p.obj_close(h)

    # --- cold user pages through the actual OBJINVOKE ABI -----------------
    import struct
    cold_live = p.system()[5]
    b = spawn(LONG_STATUS_CHILD.replace('@STATUS@', repr('cold-snapshot')))
    expected = rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'cold-target')
    callbase = p.seg_attach('memory', 4096)

    def rawprocess(op, dest, timeout=0):
        # ObjCall: no Python object/wrapper touches dest before kernel entry.
        call = struct.pack('<IIIIQQQQQQqQII', 1, op, 0, 0,
                           timeout, 0, 0, 0, dest, 200, 0, 0, 0, 0)
        p.region_write(callbase, call)
        return p.sysraw(140, b, callbase, len(call))

    for op in (49, 50):
        for offset in (0, 4096 - 96):
            destbase = p.seg_attach('memory', 8192)
            dest = destbase + offset
            rawprocess(op, dest)
            data = p.region_read(dest, 200)
            fields = struct.unpack('<IIIIqQQQQII', data[:64])
            ok(fields[1] == p.OBJ_PROC_COMPLETED and
               fields[2] == expected[2] and fields[4] == expected[0] and
               fields[5] == expected[4] and
               data[64:64 + fields[9]] == b'cold-snapshot' and
               struct.unpack('<I', data[192:196])[0] == 0,
               'cold user pages op=%d offset=%d snapshot intact' % (op, offset))
            p.seg_detach(destbase)
    # Install the proven NCONT handler so invalid addresses report OSError.
    p.sysraw(28, p.notify_probe_addr())
    for op in (49, 50):
        denied(lambda: rawprocess(op, 1), 'bad process destination')
    ok(p.process_info(b)[9] == 0, 'bad INFO/WAIT destinations leave no waiters')
    p.sysraw(28, 0)
    p.seg_detach(callbase)
    p.obj_close(b)

    ok(p.system()[5] == cold_live,
       'cold and bad destination calls release completion references')

    # --- non-cancellable termination, racing controllers, writer release --
    reg = p.region_create(8192)
    ro = p.obj_open_handle(reg, RO)
    base = p.region_map(ro, 0x4f310000)
    b = spawn(IGNORE_TERMINATION, ((reg, RW),), cpu=1)
    waitword(base, 1, 'genuine NCONT handler and writer ready')
    ok(rstr(base) == b'IGNORE-READY',
       'target executed a genuine userspace NCONT callback before blocking')
    go = p.region_create(8192)
    goro = p.obj_open_handle(go, RO)
    gobase = p.region_map(go, 0x4f320000)
    ctl = p.obj_open_handle(b, CT)
    c = spawn(RACING_CONTROLLER, ((ctl, CT), (goro, RO)), cpu=0)
    d = spawn(RACING_CONTROLLER, ((ctl, CT), (goro, RO)), cpu=2)
    p.sleep(200)  # allow both independent controllers to enter their GO wait
    p.region_atomic_store(gobase, 1)
    p.region_wake(gobase, 0, 0x7fffffff)
    ok(p.region_wait(base, 0, 1, 10000) == 3,
       'forced writer termination wakes RO reader with writer-release status')
    rb = rec(b, 10000)
    ok(rb[8] == 0 and rb[2] == p.OBJ_PROC_CAUSE_TERMINATED and
       rb[1] == 'terminated', 'NCONT cannot cancel capability termination')
    ok(p.region_read(base + 128, 7) == b'\0' * 7 and
       rstr(base) == b'IGNORE-READY',
       'target cannot resume after termination; immutable RO payload survives')
    denied(lambda: p.process_terminate(b), 'already completed target')
    for h in (c, d):
        rc = rec(h, 10000)
        ok(rc[8] == 0 and rc[1] == '', 'racing controller completes cleanly')
        p.obj_close(h)
    p.obj_close(ctl)
    p.obj_close(b)
    p.region_unmap(base)
    p.region_unmap(gobase)
    for h in (ro, reg, goro, go):
        p.obj_close(h)

    # --- genuine Handle-slot reuse cannot revive stale authority ----------
    first = spawn(SHORT.replace('@MS@', '0'))
    i1 = p.process_info(first)
    rec_ok(first, p.OBJ_PROC_CAUSE_EXIT, 'recycled-first')
    p.obj_close(first)
    live = 0
    i2 = None
    for _ in range(8):
        h = spawn(B_LONG.replace('@MS@', '60000'))
        if (h & 0xff) == (first & 0xff):
            live = h
            i2 = p.process_info(h)
            break
        p.process_terminate(h)
        rec_ok(h, p.OBJ_PROC_CAUSE_TERMINATED, 'recycled-probe')
        p.obj_close(h)
    if live == 0 or i2 is None:
        raise AssertionError('could not force a recycled Handle slot')
    ok(live != first and i1[4] != i2[4],
       'recycled Handle slot receives a fresh generation')
    denied(lambda: p.process_info(first), 'stale Handle after slot reuse')
    denied(lambda: p.process_terminate(first),
           'stale control Handle after slot reuse')
    denied(lambda: p.process_terminate(0x12345678),
           'guessed numeric process identifier')
    ok(p.process_info(live)[3] == p.OBJ_PROC_RUNNING,
       'stale and guessed identities cannot affect the replacement process')
    p.process_terminate(live)
    rec_ok(live, p.OBJ_PROC_CAUSE_TERMINATED, 'recycled-live')
    p.obj_close(live)

    # --- fault/Broken cleanup releases the writable region slot -----------
    reg = p.region_create(8192)
    rw = p.obj_open_handle(reg, RW)
    ro = p.obj_open_handle(reg, RO)
    fbase = p.region_map(ro, 0x4f400000)
    b = spawn(FAULTER, ((rw, RW),), cpu=2)
    p.obj_close(rw)
    waitword(fbase, 1, 'faulter publish')
    rf = rec_ok(b, p.OBJ_PROC_CAUSE_FAULT, 'faulted-B')
    ok('fault' in rf[1].lower(),
       'fault produces a terminal completion with diagnostic status')
    r = p.region_wait(fbase, 0, 1, 3000)
    ok(r == 3, 'faulted writer releases its RW slot and notifies waiters')
    ok(p.region_read(fbase + 64, 10) == b'FAULT-DATA',
       'a read-only observer retains the immutable snapshot after fault')
    p.obj_close(b)
    p.region_unmap(fbase)
    p.obj_close(ro)
    p.obj_close(reg)

    # --- exit status is snapshotted before segment reclamation -------------
    reg = p.region_create(8192)
    rw = p.obj_open_handle(reg, RW)
    ro = p.obj_open_handle(reg, RO)
    sbase = p.region_map(ro, 0x4f410000)
    b = spawn(STATUS_CHILD.replace('@STATUS@',
                                   repr('phase5-status-exact')),
              ((rw, RW),), cpu=0)
    p.obj_close(rw)
    waitword(sbase, 1, 'status publish')
    rs = rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'status-B')
    ok(rs[1] == 'phase5-status-exact',
       'exact nonempty exit status survives address-space reclamation')
    r = p.region_wait(sbase, 0, 1, 3000)
    ok(r == 3, 'status test writer releases its RW slot before completion')
    ok(p.region_read(sbase + 64, 11) == b'STATUS-DATA',
       'the immutable payload remains readable after status publication')
    p.obj_close(b)
    p.region_unmap(sbase)
    p.obj_close(ro)
    p.obj_close(reg)

    longstatus = 'phase5-long-' + 'x' * 300
    b = spawn(LONG_STATUS_CHILD.replace('@STATUS@', repr(longstatus)))
    rl = rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'long-status')
    ok(len(rl[1]) == 127 and longstatus.startswith(rl[1]),
       'exit status is bounded, NUL-terminated and truncated deterministically')
    p.obj_close(b)

    # --- timeout, interrupt and repeated reads ----------------------------
    b = spawn(B_LONG.replace('@MS@', '3000'))
    o = p.obj_open_handle(b, OB)
    r = rec(o, 50)
    ok(r[8] == 1 and r[3] == p.OBJ_PROC_RUNNING,
       'bounded wait times out without consuming completion')
    w = spawn(WAITER, ((o, OB),), cpu=1)
    p.sleep(300)
    p.process_terminate(w)
    rw = rec_ok(w, p.OBJ_PROC_CAUSE_TERMINATED, 'interrupted-waiter')
    ok(rw[1] != '', 'authorized termination interrupts a blocked observer wait')
    ok(rec(b, 0)[3] == p.OBJ_PROC_RUNNING,
       'interrupting the waiter leaves the observed process running')
    r = rec_ok(o, p.OBJ_PROC_CAUSE_EXIT, 'timeout-final')
    r2 = rec(o)
    ok(r2[0] == r[0] and r2[2] == r[2] and r2[1] == r[1],
       'completion remains readable repeatedly after timeout/interrupt')
    p.obj_close(w)
    p.obj_close(o)
    p.obj_close(b)

    # --- deadline-vs-exit race keeps return code and snapshot atomic ------
    r1 = p.region_create(8192)
    r2 = p.region_create(8192)
    rw1 = p.obj_open_handle(r1, RW)
    ro1 = p.obj_open_handle(r1, RO)
    rw2 = p.obj_open_handle(r2, RW)
    ro2 = p.obj_open_handle(r2, RO)
    base1 = p.region_map(ro1, 0x4f420000)
    base2 = p.region_map(ro2, 0x4f430000)
    b = spawn(B_LONG.replace('@MS@', '5000'), cpu=0)
    o1 = p.obj_open_handle(b, OB)
    o2 = p.obj_open_handle(b, OB)
    c = spawn(RACE_OBSERVER, ((o1, OB), (rw1, RW)), cpu=1)
    d = spawn(RACE_OBSERVER, ((o2, OB), (rw2, RW)), cpu=2)
    p.obj_close(o1)
    p.obj_close(o2)
    p.obj_close(rw1)
    p.obj_close(rw2)
    rec_ok(c, p.OBJ_PROC_CAUSE_EXIT, 'race-C')
    rec_ok(d, p.OBJ_PROC_CAUSE_EXIT, 'race-D')
    rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'race-B')
    f1 = rstr(base1).split(b'|')
    f2 = rstr(base2).split(b'|')
    ok(len(f1) == 4 and len(f2) == 4 and int(f1[0]) > 0 and int(f2[0]) > 0,
       'both observers hit bounded timeouts before B completed')
    ok(f1[1:] == f2[1:],
       'timeout returns never report COMPLETED and final records agree')
    p.obj_close(c)
    p.obj_close(d)
    p.obj_close(b)
    p.region_unmap(base1)
    p.region_unmap(base2)
    p.obj_close(ro1)
    p.obj_close(ro2)
    p.obj_close(r1)
    p.obj_close(r2)

    # --- two-controller termination/exit/fault stress ---------------------
    # Each round has one target owning a region writer and two independent
    # Vcontrol controllers released by an explicit GO barrier.  The target
    # either exits voluntarily, faults, or blocks in REGIONWAIT.  Depending on
    # the race the completion is EXIT/FAULT/TERMINATED, but it must be unique,
    # repeatable, bounded and must release the writer before observers read it.
    stress_live = p.system()[5]
    stress_handles = len(p.obj_list())
    stress_free = p.system()[6]
    stress_modes = (
        ('exit', "p.sleep(@DELAY@)\np.exits(@STATUS@)\n"),
        ('fault', "p.sleep(@DELAY@)\n"
                  "p.region_write(0x50000000, b'X')\n"
                  "p.exits('no-fault')\n"),
        ('wait', "while True:\n"
                 "    try:\n"
                 "        p.region_wait(g, 0, 1, -1)\n"
                 "    except OSError:\n"
                 "        pass\n"),
        ('pwait', "m = findkind(p.VKIND_PROCESS, p.VOBSERVE)\n"
                  "if not m:\n"
                  "    fail('stress-pwait-handle')\n"
                  "while True:\n"
                  "    try:\n"
                  "        p.process_wait(m, -1)\n"
                  "    except OSError:\n"
                  "        pass\n"),
    )
    for i in range(16):
        mode, body = stress_modes[i % len(stress_modes)]
        delay = i % 5
        ctldelay = 0 if mode in ('wait', 'pwait') else (i * 3) % 7
        status = 'stress-exit-%02d' % i

        treg = p.region_create(8192)
        trw = p.obj_open_handle(treg, RW)
        tro = p.obj_open_handle(treg, RO)
        tbase = p.region_map(tro, 0x4f600000)
        c1reg = p.region_create(8192)
        c1rw = p.obj_open_handle(c1reg, RW)
        c1ro = p.obj_open_handle(c1reg, RO)
        c1base = p.region_map(c1ro, 0x4f620000)
        c2reg = p.region_create(8192)
        c2rw = p.obj_open_handle(c2reg, RW)
        c2ro = p.obj_open_handle(c2reg, RO)
        c2base = p.region_map(c2ro, 0x4f640000)
        go = p.region_create(8192)
        gorw = p.obj_open_handle(go, RW)
        goro = p.obj_open_handle(go, RO)
        gobase = p.region_map(gorw, 0x4f660000)

        code = (STRESS_TARGET
                .replace('@MODE@', body)
                .replace('@DELAY@', str(delay))
                .replace('@STATUS@', repr(status)))
        hold = 0
        hobs = 0
        vh = ((trw, RW), (goro, RO))
        if mode == 'pwait':
            hold = spawn(B_LONG.replace('@MS@', '60000'), cpu=i % 3)
            hobs = p.obj_open_handle(hold, OB)
            vh = ((trw, RW), (goro, RO), (hobs, OB))
        b = spawn(code, vh, cpu=i % 3)
        if hobs:
            p.obj_close(hobs)
        ctl = p.obj_open_handle(b, CT)
        c1 = spawn(STRESS_CONTROLLER.replace('@RBASE@', '0x4f620000')
                   .replace('@GBASE@', '0x4f630000')
                   .replace('@CTLDELAY@', str(ctldelay)),
                   ((ctl, CT), (c1rw, RW), (goro, RO)),
                   cpu=(i + 1) % 3)
        c2 = spawn(STRESS_CONTROLLER.replace('@RBASE@', '0x4f640000')
                   .replace('@GBASE@', '0x4f650000')
                   .replace('@CTLDELAY@', str(ctldelay)),
                   ((ctl, CT), (c2rw, RW), (goro, RO)),
                   cpu=(i + 2) % 3)
        p.obj_close(trw)
        p.obj_close(c1rw)
        p.obj_close(c2rw)
        p.obj_close(goro)
        p.obj_close(ctl)

        waitword(tbase, 1, 'stress target ready')
        waitword(c1base, 1, 'stress controller 1 ready')
        waitword(c2base, 1, 'stress controller 2 ready')
        p.region_atomic_store(gobase, 1)
        p.region_wake(gobase, 0, 0x7fffffff)

        rb = rec(b, 10000)
        if rb[8] != 0 or rb[3] != p.OBJ_PROC_COMPLETED:
            raise AssertionError('stress completion %d: %r' % (i, (rb,)))
        rb2 = rec(b, 0)
        if (rb2[0] != rb[0] or rb2[1] != rb[1] or rb2[2] != rb[2] or
                rb2[3] != rb[3] or rb2[4] != rb[4]):
            raise AssertionError('stress completion changed %d' % i)
        cause = rb[2]
        if mode == 'exit':
            if cause == p.OBJ_PROC_CAUSE_EXIT:
                if rb[1] != status:
                    raise AssertionError('stress exit status %d: %r' %
                                         (i, (rb[1],)))
            elif cause == p.OBJ_PROC_CAUSE_TERMINATED:
                if rb[1] != 'terminated':
                    raise AssertionError('stress term status %d: %r' %
                                         (i, (rb[1],)))
            else:
                raise AssertionError('stress exit cause %d: %r' % (i, (rb,)))
        elif mode == 'fault':
            if cause not in (p.OBJ_PROC_CAUSE_FAULT,
                             p.OBJ_PROC_CAUSE_TERMINATED):
                raise AssertionError('stress fault cause %d: %r' % (i, (rb,)))
            if rb[1] == '':
                raise AssertionError('stress fault empty status %d' % i)
        else:
            if cause != p.OBJ_PROC_CAUSE_TERMINATED:
                raise AssertionError('stress %s cause %d: %r' %
                                     (mode, i, (rb,)))
            if rb[1] != 'terminated':
                raise AssertionError('stress %s status %d: %r' %
                                     (mode, i, (rb[1],)))
        if p.region_wait(tbase, 0, 1, 3000) != 3:
            raise AssertionError('stress writer not released %d' % i)
        if rstr(tbase) != b'STRESS-DATA':
            raise AssertionError('stress payload changed %d' % i)
        rec_ok(c1, p.OBJ_PROC_CAUSE_EXIT, 'stress-controller-1')
        rec_ok(c2, p.OBJ_PROC_CAUSE_EXIT, 'stress-controller-2')
        if hold:
            p.process_terminate(hold)
            rec_ok(hold, p.OBJ_PROC_CAUSE_TERMINATED, 'stress-hold')
            p.obj_close(hold)
        for h in (b, c1, c2):
            p.obj_close(h)
        p.region_unmap(gobase)
        p.region_unmap(c1base)
        p.region_unmap(c2base)
        p.region_unmap(tbase)
        for h in (gorw, go, c1ro, c1reg, c2ro, c2reg, tro, treg):
            p.obj_close(h)
        ok(True, 'stress round %d (%s): unique bounded cause %d completion '
           'and writer release' % (i, mode, cause))

    p.sleep(200)
    ok(p.system()[5] == stress_live,
       'two-controller stress returns live completion records to baseline (%d)'
       % stress_live)
    ok(len(p.obj_list()) == stress_handles,
       'two-controller stress returns the Handle table to baseline (%d)'
       % stress_handles)
    ok(stress_free - p.system()[6] <= 262144,
       'two-controller stress reclaims process resources (free delta=%d)'
       % (stress_free - p.system()[6]))

    # --- no observer / launcher exit, then stress and reclamation ---------
    live0 = p.system()[5]
    baseline = len(p.obj_list())
    free0 = p.system()[6]
    for i in range(16):
        status = 'S%02d-phase5-churn' % i
        b = spawn(LONG_STATUS_CHILD.replace('@STATUS@', repr(status)),
                  cpu=i % 3)
        rb = rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'stress-exit')
        if rb[1] != status:
            raise AssertionError('churn status %r != %r' % (rb[1], status))
        p.obj_close(b)
    noobs = NO_OBSERVER.replace('@CID@', str(CID)).replace(
        '@CHILD@', repr(SHORT.replace('@MS@', '150')))
    a = spawn(noobs, ((sh, EX),), cpu=0)
    rec_ok(a, p.OBJ_PROC_CAUSE_EXIT, 'no-observer-launcher')
    p.obj_close(a)
    p.sleep(1200)
    h1 = len(p.obj_list())
    free1 = p.system()[6]
    live1 = p.system()[5]
    ok(h1 == baseline,
       'repeated create/observe/exit cycles return the Handle baseline')
    ok(live1 == live0,
       'live completion records return to the exact baseline (%d)' % live0)
    ok(free0 - free1 <= 262144,
       'process and completion resources are reclaimed (free delta=%d)'
       % (free0 - free1))

    reg = p.region_create(8192)
    slotok = 0
    for i in range(8):
        h = p.obj_open_handle(reg, RW)
        base = p.region_map(h, 0x4f500000)
        if base != 0x4f500000:
            raise AssertionError('writer mapping address changed')
        p.region_unmap(base)
        p.obj_close(h)
        slotok += 1
    ok(slotok == 8,
       'writer slot and mapping address remain reusable across 8 cycles')
    p.obj_close(reg)

    # --- mapping flags and exact-base unmap ------------------------------
    reg = p.region_create(8192)
    robase = p.region_map(reg, 0x4f510000, p.OBJ_REGION_MAP_READONLY)
    rwbase = p.region_map(reg, 0x4f520000)
    if robase != 0x4f510000 or rwbase != 0x4f520000:
        raise AssertionError('flag mapping address changed')
    p.region_atomic_store(rwbase, 0x5a)
    if p.region_atomic_load(robase) != 0x5a:
        raise AssertionError('read-only alias does not see writer data')
    try:
        p.region_unmap(robase + 4096)
    except OSError:
        pass
    else:
        raise AssertionError('interior unmap accepted')
    p.region_unmap(robase)
    p.region_unmap(rwbase)
    p.obj_close(reg)
    ok(True, 'a Vwrite Handle maps read-only by flag without taking the '
       'writer slot, and unmap requires the exact base')

    # --- RFMEM shares an installed region mapping ----------------------
    reg = p.region_create(8192)
    base = p.region_map(reg, 0x4f530000)
    p.region_atomic_store(base, 0)
    pid = p.rfork(p.RFPROC | p.RFMEM)
    if pid == 0:
        p.region_atomic_store(base, 0x77)
        p.exits('')
    p.sleep(400)
    if p.region_atomic_load(base) != 0x77:
        raise AssertionError('RFMEM child did not share the region mapping')
    p.region_unmap(base)
    p.obj_close(reg)
    ok(True, 'an RFMEM child shares the installed region mapping')

    # --- WAIT blocks for a first writer instead of returning 3 ----------
    reg = p.region_create(8192)
    ro = p.obj_open_handle(reg, RO)
    child = spawn(WAIT_NO_WRITER, ((ro, RO),), cpu=1)
    p.obj_close(ro)
    p.sleep(1000)
    wbase = p.region_map(reg, 0x4f540000)
    p.region_atomic_store(wbase, 1)
    p.region_wake(wbase, 0, 0x7fffffff)
    rec_ok(child, p.OBJ_PROC_CAUSE_EXIT, 'no-writer-wait')
    p.region_unmap(wbase)
    p.obj_close(child)
    p.obj_close(reg)
    ok(True, 'WAIT blocks for a first writer instead of returning '
       'writer-release status')

    # --- creation charges the creator's memory budget -------------------
    child = space.spawn(CID, handle=sh, cpu=1,
                        membudget=48*1024*1024,
                        argv=('micropython', '-c', CREATE_OVER_BUDGET))
    rec_ok(child, p.OBJ_PROC_CAUSE_EXIT, 'over-budget-region')
    p.obj_close(child)
    ok(True, 'region backing is charged to the creating process budget')

    p.obj_close(sh)
    print('T41-FINISH', n[0], 'PASS')
    return 1
