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
# observe-only grant, sees B running after A is gone, and reads completion.
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
r = p.process_wait(bh, -1)
if r[8] != 0 or r[3] != p.OBJ_PROC_COMPLETED:
    fail('chain-completion')
if r[0] != pid or r[2] != p.OBJ_PROC_CAUSE_EXIT:
    fail('chain-result')
publish(base, b'CHAIN-OK')
p.exits('')
'''

# A starts C with the explicitly delegated observation Handle, then exits
# immediately.  B is still running; C observes it after A is completely gone.
A_CHAIN = PRELUDE + r'''
sp = findkind(p.VKIND_SPACE, p.VEXECUTE)
bh = findkind(p.VKIND_PROCESS, p.VOBSERVE)
rw = findregion(True)
if not sp or not bh or not rw:
    fail('a-handles')
ch = space.spawn(@CID@, handle=sp,
                 argv=('micropython', '-c', @CHILD@),
                 vhandles=((bh, p.VOBSERVE | p.VMETA),
                           (rw, p.VREAD | p.VWRITE | p.VMETA |
                            p.VDELEGATE)))
p.obj_close(bh)
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
sp = findkind(p.VKIND_SPACE, p.VEXECUTE)
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

    def waitword(base, target, label):
        deadline = p.time_ns() + 20000000000
        while True:
            v = p.region_atomic_load(base)
            if v == target:
                return
            r = p.region_wait(base, 0, v, 200)
            if r == 2 or r == 3:
                p.sleep(20)
            if p.time_ns() > deadline:
                raise AssertionError(label + ' timeout')

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

    sh = p.obj_open_space('init')

    # --- launcher-first exit: A -> B, delegate to A -> C, A exits ---------
    chain_code = A_CHAIN.replace('@CID@', str(CID)).replace(
        '@CHILD@', repr(C_CHAIN))
    reg = p.region_create(8192)
    rw = p.obj_open_handle(reg, RW)
    ro = p.obj_open_handle(reg, RO)
    cbase = p.region_map(ro, 0x4f100000)
    b = spawn(B_LONG.replace('@MS@', '6000'))
    bobs = p.obj_open_handle(b, OB)
    a = spawn(chain_code,
              ((sh, EX), (bobs, OB), (rw, RW)), cpu=0)
    p.obj_close(bobs)
    p.obj_close(rw)
    ra = rec_ok(a, p.OBJ_PROC_CAUSE_EXIT, 'A-exit')
    ok(ra[0] != 0 and ra[1] == '',
       'launcher A delegates C and exits independently and cleanly')
    waitword(cbase, 1, 'chain result')
    ok(rstr(cbase) == b'CHAIN-OK',
       'C observes B after A exited; A no longer owns B')
    rb = rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'B-chain')
    p.obj_close(b)
    p.obj_close(ro)
    p.obj_close(reg)

    # --- two independent observers obtain the same immutable result -------
    r1 = p.region_create(8192)
    r2 = p.region_create(8192)
    rw1 = p.obj_open_handle(r1, RW)
    ro1 = p.obj_open_handle(r1, RO)
    rw2 = p.obj_open_handle(r2, RW)
    ro2 = p.obj_open_handle(r2, RO)
    base1 = p.region_map(ro1, 0x4f200000)
    base2 = p.region_map(ro2, 0x4f210000)
    b = spawn(B_LONG.replace('@MS@', '700'))
    o1 = p.obj_open_handle(b, OB)
    o2 = p.obj_open_handle(b, OB)
    c = spawn(OBSERVER, ((o1, OB), (rw1, RW)), cpu=1)
    d = spawn(OBSERVER, ((o2, OB), (rw2, RW)), cpu=2)
    p.obj_close(o1)
    p.obj_close(o2)
    p.obj_close(rw1)
    p.obj_close(rw2)
    rec_ok(c, p.OBJ_PROC_CAUSE_EXIT, 'observer-C')
    rec_ok(d, p.OBJ_PROC_CAUSE_EXIT, 'observer-D')
    rbs = rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'observer-B')
    first = rstr(base1)
    second = rstr(base2)
    ok(first == second and first != b'',
       'two observers independently read the same completion result')
    ok(first.startswith(str(rbs[0]).encode() + b'|'),
       'the observer result agrees with the launcher observation')
    p.obj_close(b)
    p.obj_close(ro1)
    p.obj_close(ro2)
    p.obj_close(r1)
    p.obj_close(r2)

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
    waitword(tbase, 1, 'controller result')
    ok(rstr(tbase) == b'TERM-OK',
       'a delegated controller terminates B; observe-only cannot')
    rb = rec_ok(b, p.OBJ_PROC_CAUSE_TERMINATED, 'terminated-B')
    ok(rb[1] != '', 'authorized termination publishes a distinct cause/status')
    p.obj_close(b)
    p.obj_close(ro)
    p.obj_close(reg)

    # --- stale Handle and guessed numeric identity -------------------------
    dead = spawn(SHORT.replace('@MS@', '0'))
    rec_ok(dead, p.OBJ_PROC_CAUSE_EXIT, 'dead')
    p.obj_close(dead)
    denied(lambda: p.process_info(dead), 'closed process Handle')
    denied(lambda: p.process_terminate(dead), 'closed control Handle')
    live = spawn(B_LONG.replace('@MS@', '60000'))
    denied(lambda: p.process_terminate(0x12345678),
           'guessed numeric process identifier')
    ok(rec(live, 0)[3] == p.OBJ_PROC_RUNNING,
       'guessed identity cannot affect a live process')
    p.process_terminate(live)
    rec_ok(live, p.OBJ_PROC_CAUSE_TERMINATED, 'live-control')
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
    p.obj_close(ro)
    p.obj_close(reg)

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

    # --- no observer / launcher exit, then stress and reclamation ---------
    baseline = len(p.obj_list())
    free0 = p.system()[6]
    for i in range(12):
        b = spawn(SHORT.replace('@MS@', '20'), cpu=i % 3)
        rec_ok(b, p.OBJ_PROC_CAUSE_EXIT, 'stress-exit')
        p.obj_close(b)
    noobs = NO_OBSERVER.replace('@CID@', str(CID)).replace(
        '@CHILD@', repr(SHORT.replace('@MS@', '150')))
    a = spawn(noobs, ((sh, EX),), cpu=0)
    rec_ok(a, p.OBJ_PROC_CAUSE_EXIT, 'no-observer-launcher')
    p.obj_close(a)
    p.sleep(1200)
    p.sleep(300)
    h1 = len(p.obj_list())
    free1 = p.system()[6]
    ok(h1 == baseline,
       'repeated create/observe/exit cycles return the Handle baseline')
    ok(free0 - free1 <= 262144,
       'completion records and process resources are reclaimed '
       '(free delta=%d)' % (free0 - free1))

    p.obj_close(sh)
    print('T41-FINISH', n[0], 'PASS')
    return 1
