"""OwnerID provenance: immutable single OwnerID, registered launch and
owner-offered publication on the AMD64 QEMU MicroPython path.

Every check prints T49-PASS immediately after it passes.  Failed child
processes report only an exit status; the parent prints the result.
"""

MPY = 0x4f45584543000003
STATE = 0x4f45584543000100
AOBJ = 0x4f45584543000101
BOBJ = 0x4f45584543000102

COMMON = r'''
def fail(s):
    p.exits('ERR:' + s)

def findkind(kind, need):
    for h in p.obj_list():
        try:
            i = p.obj_info(h)
        except OSError:
            continue
        if i[8] == kind and (need == 0 or (i[10] & need) == need):
            return h
    return 0

def findregion():
    return findkind(p.VKIND_REGION, 0)

def findproc(need):
    return findkind(p.VKIND_PROCESS, need)
'''

B_CODE = r'''
import plan9 as p
import space
AOWN = @AOWN@
BOWN = @BOWN@
AOID = @AOID@
AOBJ = @AOBJ@
BOBJ = @BOBJ@
PUB = @PUB@
''' + COMMON + r'''
stage = 'start'
try:
    stage = 'owner'
    oi = p.owner_info()
    if oi[0] != BOWN or oi[1] != 'ownerB':
        fail('b-owner')
    stage = 'handles'
    sh = findkind(p.VKIND_SPACE, p.VEXECUTE)
    reg = findregion()
    ctl = findproc(p.VCONTROL)
    if not sh or not reg or not ctl:
        fail('b-handles')
    stage = 'persist'
    p.obj_put_oid(sh, BOBJ, b'B-persist')
    p.obj_commit(sh)
    ph = p.obj_open_ge_space(sh, BOBJ)
    if p.obj_owner(ph)[0] != BOWN or p.obj_read(ph, 16) != b'B-persist':
        fail('b-persist')
    p.obj_close(ph)
    # A granted B a full O Space capability. Knowing an OID alone is not
    # authority, but this explicit Space grant authorizes it across owners.
    stage = 'open-a'
    ah = p.obj_open_ge_space(sh, AOID)
    if p.obj_owner(ah)[0] != AOWN:
        fail('b-a-executable-provenance')
    p.obj_close(ah)
    ah = p.obj_open_ge_space(sh, AOBJ)
    if p.obj_owner(ah)[0] != AOWN or p.obj_read(ah, 16) != b'A-persist':
        fail('b-a-read-cap')
    p.obj_close(ah)
    p.obj_put_oid(sh, AOBJ, b'A-persist')
    p.obj_commit(sh)
    ah = p.obj_open_ge_space(sh, AOBJ)
    if p.obj_owner(ah)[0] != AOWN:
        fail('b-a-write-provenance')
    p.obj_close(ah)
    # B creates and reopens its own object; ownership is immutable.
    stage = 'own-create'
    gh, oid = p.obj_create_ge(sh, b'B-alpha')
    if p.obj_owner(gh)[0] != BOWN:
        fail('b-own-object-owner')
    stage = 'own-write'
    p.obj_write(gh, b'B-beta')
    p.obj_commit(gh)
    if p.obj_read(gh, 8) != b'B-beta':
        fail('b-own-object-rw')
    stage = 'own-close'
    p.obj_close(gh)
    stage = 'reopen'
    try:
        gh = p.obj_open_ge_space(sh, oid)
        own = p.obj_owner(gh)[0]
        data = p.obj_read(gh, 8)
        if own != BOWN or data != b'B-beta':
            fail('b-own-reopen-value')
        p.obj_close(gh)
    except OSError:
        fail('b-own-reopen')
    # A still occupies the unique writer slot. RO remains shared.
    stage = 'region'
    if p.obj_owner(reg)[0] != AOWN:
        fail('b-region-owner')
    try:
        p.region_map(reg, 0x51000000)
    except OSError:
        pass
    else:
        fail('b-exclusive-writer-mapping')
    base = p.region_map(reg, 0x51000000, p.OBJ_REGION_MAP_READONLY)
    if p.region_atomic_load(base + 8) != 1:
        r = p.region_wait(base, 8, 0, 2000)
        if r != 0 and p.region_atomic_load(base + 8) != 1:
            fail('b-wait')
    if p.region_read(base + 64, len(PUB)) != PUB:
        fail('b-published-data')
    try:
        p.region_wake(base, 8, 1)
    except OSError:
        pass
    else:
        fail('b-wake')
    try:
        p.obj_revoke(reg)
    except OSError:
        pass
    else:
        fail('b-revoke')
    # Vcontrol is explicitly delegated. OwnerID is not a termination gate.
    stage = 'terminate'
    if p.process_info(ctl)[3] == p.OBJ_PROC_RUNNING:
        p.process_terminate(ctl)
    # Registration is a trusted boot/installation action.
    stage = 'register'
    try:
        p.program_register('evil', 'root', sh, @MPY@)
    except OSError:
        pass
    else:
        fail('b-register-allowed')
    stage = 'done'
    p.region_unmap(base)
    p.exits('BOK:%d' % oid)
except BaseException as e:
    import plan9 as p
    p.exits('ERR:' + stage + ':' + repr(e))
'''

SLEEP_CODE = r'''
import plan9 as p
p.sleep(30000)
p.exits('')
'''

LONG_B_CODE = r'''
import plan9 as p
p.sleep(3000)
p.exits('BALIVE')
'''

D_CODE = r'''
import plan9 as p
''' + COMMON + r'''
try:
    reg = findregion()
    bh = findproc(p.VOBSERVE)
    if not reg or not bh:
        fail('d-handles')
    base = p.region_map(reg, 0x52000000)
    r = p.process_wait(bh, -1)
    if r[8] != 0 or r[3] != p.OBJ_PROC_COMPLETED:
        fail('d-wait')
    msg = r[1]
    p.region_write(base + 64, msg)
    p.region_atomic_store(base + 0, 1)
    p.region_wake(base, 0, 0x7fffffff)
    p.exits('DOK')
except BaseException as e:
    import plan9 as p
    p.exits('ERR:' + repr(e))
'''

L_CODE = r'''
import plan9 as p
''' + COMMON + r'''
try:
    reg = findregion()
    if not reg:
        fail('l-handles')
    bh = p.program_launch('B', ('micropython', '-c', @LONG_B@), ())
    dh = p.program_launch('D', ('micropython', '-c', @D_CODE@),
                          ((bh, p.VOBSERVE | p.VMETA | p.VDELEGATE),
                           (reg, p.VREAD | p.VWRITE | p.VMETA |
                            p.VDELEGATE)))
    p.obj_close(dh)
    p.obj_close(bh)
    p.exits('LEXIT')
except BaseException as e:
    import plan9 as p
    p.exits('ERR:' + repr(e))
'''

A_RB_CODE = r'''
import plan9 as p
AOWN = @AOWN@
AOBJ = @AOBJ@
BOBJ = @BOBJ@
def findspace():
    for h in p.obj_list():
        try:
            i = p.obj_info(h)
        except OSError:
            continue
        if i[8] == p.VKIND_SPACE and (i[10] & p.VEXECUTE):
            return h
    return 0
try:
    if p.owner_info()[0] != AOWN:
        p.exits('ERR:a-rb-owner')
    sh = findspace()
    if not sh:
        p.exits('ERR:a-rb-space')
    h = p.obj_open_ge_space(sh, AOBJ)
    if p.obj_owner(h)[0] != AOWN or p.obj_read(h, 16) != b'A-persist':
        p.exits('ERR:a-rb-own')
    p.obj_close(h)
    h = p.obj_open_ge_space(sh, BOBJ)
    if p.obj_owner(h)[0] == AOWN or p.obj_read(h, 16) != b'B-persist':
        p.exits('ERR:a-rb-delegated-read')
    p.obj_close(h)
    p.obj_put_oid(sh, BOBJ, b'B-persist')
    p.obj_commit(sh)
    h = p.obj_open_ge_space(sh, BOBJ)
    if p.obj_owner(h)[0] == AOWN:
        p.exits('ERR:a-rb-changed-provenance')
    p.obj_close(h)
    p.exits('ARBOK')
except BaseException as e:
    import plan9 as p
    p.exits('ERR:' + repr(e))
'''

B_RB_CODE = r'''
import plan9 as p
BOWN = @BOWN@
AOBJ = @AOBJ@
BOBJ = @BOBJ@
def findspace():
    for h in p.obj_list():
        try:
            i = p.obj_info(h)
        except OSError:
            continue
        if i[8] == p.VKIND_SPACE and (i[10] & p.VEXECUTE):
            return h
    return 0
try:
    if p.owner_info()[0] != BOWN:
        p.exits('ERR:b-rb-owner')
    sh = findspace()
    if not sh:
        p.exits('ERR:b-rb-space')
    h = p.obj_open_ge_space(sh, BOBJ)
    if p.obj_owner(h)[0] != BOWN or p.obj_read(h, 16) != b'B-persist':
        p.exits('ERR:b-rb-own')
    p.obj_close(h)
    h = p.obj_open_ge_space(sh, AOBJ)
    if p.obj_owner(h)[0] == BOWN or p.obj_read(h, 16) != b'A-persist':
        p.exits('ERR:b-rb-delegated-read')
    p.obj_close(h)
    p.obj_put_oid(sh, AOBJ, b'A-persist')
    p.obj_commit(sh)
    h = p.obj_open_ge_space(sh, AOBJ)
    if p.obj_owner(h)[0] == BOWN:
        p.exits('ERR:b-rb-changed-provenance')
    p.obj_close(h)
    p.exits('BRBOK')
except BaseException as e:
    import plan9 as p
    p.exits('ERR:' + repr(e))
'''

EXEC_CODE = r'''
import plan9 as p
o = p.owner_info()
p.exits('EXECOK' if o[0] == @AOWN@ and o[1] == 'ownerA' else 'EXECBAD')
'''

A_CODE = r'''
import plan9 as p
import space
AOWN = @AOWN@
BOWN = @BOWN@
AID = @AOID@
AOBJ = @AOBJ@
BOBJ = @BOBJ@
MPY = @MPY@
PUB = @PUB@
''' + COMMON + r'''
stage = 'start'
try:
    oi = p.owner_info()
    stage = 'owner'
    if oi[0] != AOWN or oi[1] != 'ownerA':
        fail('a-owner')
    sh = findkind(p.VKIND_SPACE, p.VEXECUTE)
    stage = 'space'
    if not sh:
        fail('a-space')
    # Fixed OID persistence: first publication assigns this process owner
    # atomically; same-owner rewrites must not create or overwrite the
    # immutable owner record.  The object survives reboot for the two-boot
    # check.
    stage = 'persist'
    for _ in range(3):
        p.obj_put_oid(sh, AOBJ, b'A-persist')
        p.obj_commit(sh)
    ph = p.obj_open_ge_space(sh, AOBJ)
    if p.obj_owner(ph)[0] != AOWN or p.obj_read(ph, 16) != b'A-persist':
        fail('a-persist')
    p.obj_close(ph)
    # Direct object ownership: create, write, commit, close and reopen.
    stage = 'create'
    gh, oid = p.obj_create_ge(sh, b'alpha')
    if p.obj_owner(gh)[0] != AOWN:
        fail('a-object-owner')
    stage = 'write'
    p.obj_write(gh, b'beta')
    p.obj_commit(gh)
    if p.obj_read(gh, 8) != b'beta':
        fail('a-object-rw')
    p.obj_close(gh)
    stage = 'reopen'
    gh = p.obj_open_ge_space(sh, oid)
    if p.obj_owner(gh)[0] != AOWN or p.obj_read(gh, 8) != b'beta':
        fail('a-object-reopen')
    p.obj_close(gh)
    # One writer, two RO observers, data offered through the existing IPC.
    stage = 'region'
    reg = p.region_create(8192)
    if p.obj_owner(reg)[0] != AOWN:
        fail('a-region-owner')
    base = p.region_map(reg, 0x41000000)
    p.region_write(base + 64, PUB)
    p.region_atomic_store(base + 8, 0)
    # A delegates Vcontrol explicitly to B; ownership is provenance.
    stage = 'child'
    child = space.spawn(AID, handle=sh,
                        argv=('micropython', '-c', @SLEEP@))
    if child <= 0:
        fail('a-child')
    rw = p.VREAD | p.VWRITE | p.VMETA | p.VDELEGATE
    ctl = p.VCONTROL | p.VMETA | p.VDELEGATE
    bargs = ('micropython', '-c', @B_CODE@)
    shr = (p.VEXECUTE | p.VREAD | p.VWRITE | p.VCREATE | p.VCOMMIT |
           p.VMETA | p.VDELEGATE)
    vh = ((sh, shr), (reg, rw), (child, ctl))
    stage = 'launchB'
    # The GE/O store admits one active writer transaction at a time, so the
    # two same-owner B processes are exercised sequentially rather than
    # racing in objtxbegin()'s "object store busy" rejection.
    b1 = p.program_launch('B', bargs, vh)
    if p.obj_owner(b1)[0] != BOWN:
        fail('a-b-owner')
    p.region_atomic_store(base + 8, 1)
    p.region_wake(base, 8, 0x7fffffff)
    stage = 'waitB'
    r1 = p.process_wait(b1, -1)
    b2 = p.program_launch('B', bargs, vh)
    if p.obj_owner(b2)[0] != BOWN:
        fail('a-b2-owner')
    r2 = p.process_wait(b2, -1)
    if r1[8] != 0 or r2[8] != 0 or r1[2] != p.OBJ_PROC_CAUSE_EXIT or \
       r2[2] != p.OBJ_PROC_CAUSE_EXIT:
        fail('a-b-completion')
    if not r1[1].startswith('BOK:') or not r2[1].startswith('BOK:'):
        fail('a-b-status:' + r1[1] + '/' + r2[1])
    boid = int(r1[1][4:])
    # A's full Space grant permits B-owned object access; existing Kowner
    # remains immutable even when this caller is another owner.
    bh = p.obj_open_ge_space(sh, boid)
    if p.obj_owner(bh)[0] != BOWN or p.obj_read(bh, 16) != b'B-beta':
        fail('a-open-b-object-with-space-grant')
    p.obj_close(bh)
    p.obj_put_oid(sh, boid, b'B-beta')
    p.obj_commit(sh)
    bh = p.obj_open_ge_space(sh, boid)
    if p.obj_owner(bh)[0] != BOWN:
        fail('a-modify-b-immutable-provenance')
    p.obj_close(bh)
    # The first B launch exercised the explicit cross-owner Vcontrol grant.
    stage = 'termchild'
    cr = p.process_wait(child, -1)
    if cr[8] != 0 or cr[2] != p.OBJ_PROC_CAUSE_TERMINATED:
        fail('a-terminate-completion')
    # Unregistered launch and non-boot registration are rejected.
    try:
        p.program_launch('missing', ('micropython', '-c', ''), ())
    except OSError:
        pass
    else:
        fail('a-launch-unregistered')
    try:
        p.program_register('evil-a', 'root', sh, MPY)
    except OSError:
        pass
    else:
        fail('a-register-allowed')
    p.obj_close(b1)
    p.obj_close(b2)
    stage = 'cleanup'
    p.obj_close(child)
    p.region_unmap(base)
    p.obj_close(reg)
    # OEXECUTE preserves the process OwnerID and its observer authority.
    stage = 'exec'
    space.hexecute(sh, AID, ('micropython', '-c', @EXEC@))
    fail('a-exec-returned')
except BaseException as e:
    import plan9 as p
    p.exits('ERR:' + stage + ':' + repr(e))
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

    def ok(value, label):
        if not value:
            raise AssertionError(label)
        n[0] += 1
        print('T49-PASS', n[0], label)

    def denied(call, label):
        try:
            call()
        except OSError:
            return
        raise AssertionError('expected rejection: ' + label)

    def waitok(h, label):
        r = p.process_wait(h, -1)
        if r[8] != 0 or r[3] != p.OBJ_PROC_COMPLETED or \
           r[2] != p.OBJ_PROC_CAUSE_EXIT:
            raise AssertionError('%s: %r' % (label, (r,)))
        return r

    def build(template, **kw):
        s = template
        for k, v in kw.items():
            s = s.replace('@' + k + '@', repr(v))
        return s

    sh = p.obj_open_space('init')
    root = p.owner_info()
    ok(root[0] == p.proc_info()[3] and root[1] == 'root',
       'boot process runs under the named ordinary owner root')
    ok(root[0] != 0, 'user-visible owner identity is separate from kernel 0')

    def readstate():
        h = 0
        try:
            h = p.obj_open_ge_space(sh, STATE)
            d = p.obj_read(h, 64)
        except OSError:
            return None
        finally:
            if h:
                p.obj_close(h)
        f = d.split()
        if len(f) != 3 or f[0] != b'OWNERID-RB1':
            return None
        return int(f[1]), int(f[2])

    saved = readstate()
    if saved is not None:
        aown, bown = saved
        ra, _ = p.program_register('A', 'ownerA', sh, MPY,
                                   p.OWNER_PROGRAM_LAUNCHABLE)
        rb, _ = p.program_register('B', 'ownerB', sh, MPY,
                                   p.OWNER_PROGRAM_LAUNCHABLE)
        ok(ra == aown and rb == bown,
           'reboot re-registration resolves the stored OwnerIDs deterministically')
        sth = p.obj_open_ge_space(sh, STATE)
        sok = p.obj_owner(sth)[0] == root[0]
        p.obj_close(sth)
        ok(sok, 'persisted state object keeps its boot owner across reboot')
        a = p.obj_open_ge_space(sh, AOBJ)
        b = p.obj_open_ge_space(sh, BOBJ)
        ok(p.obj_owner(a)[0] == aown and p.obj_owner(b)[0] == bown,
           'bootstrap Space grant resolves both owner domains')
        p.obj_close(a)
        p.obj_close(b)
        p.obj_put_oid(sh, AOBJ, b'A-persist')
        p.obj_commit(sh)
        a = p.obj_open_ge_space(sh, AOBJ)
        ok(p.obj_owner(a)[0] == aown,
           'bootstrap Space write preserves stored owner A')
        p.obj_close(a)
        rights = (sh, p.VEXECUTE | p.VREAD | p.VWRITE | p.VCREATE |
                  p.VCOMMIT | p.VMETA | p.VDELEGATE)
        ar = waitok(p.program_launch('A',
                    ('micropython', '-c',
                     build(A_RB_CODE, AOWN=aown, AOBJ=AOBJ, BOBJ=BOBJ)),
                    (rights,)), 'A-reboot')
        br = waitok(p.program_launch('B',
                    ('micropython', '-c',
                     build(B_RB_CODE, BOWN=bown, AOBJ=AOBJ, BOBJ=BOBJ)),
                    (rights,)), 'B-reboot')
        ok(ar[1] == 'ARBOK', 'owner A Space grant resolves B-owned object: ' + ar[1])
        ok(br[1] == 'BRBOK', 'owner B Space grant resolves A-owned object: ' + br[1])
        p.obj_close(sh)
        print('T49-REBOOT-FINISH', n[0], 'PASS')
        return 1

    # Before the first owner-aware creation the seeded objects have no
    # owner record; the documented legacy rule resolves them to root.  The
    # first creation migrates them explicitly, after which they still do.
    lh = p.obj_open_ge_space(sh, MPY)
    ok(p.obj_owner(lh)[0] == root[0],
       'seeded object has no owner record and resolves to legacy root')
    p.obj_close(lh)

    # Deterministic storage failure points.  A pre-publication abort must
    # leave the candidate reusable with no stale OwnerID; a post-publish
    # fault leaves a fully owner-assigned published object and advances the
    # allocator.
    g, baseoid = p.obj_create_ge(sh, b'fault-base')
    p.obj_close(g)
    p.ownerid_fault(1)
    denied(lambda: p.obj_create_ge(sh, b'fault-1'), 'create fault before owner staging')
    g, f1 = p.obj_create_ge(sh, b'fault-1-retry')
    ok(f1 == baseoid + 1 and p.obj_owner(g)[0] == root[0],
       'aborted pre-owner create reuses the OID with the caller owner')
    p.obj_close(g)
    p.ownerid_fault(2)
    denied(lambda: p.obj_create_ge(sh, b'fault-2'), 'create fault after owner staging')
    g, f2 = p.obj_create_ge(sh, b'fault-2-retry')
    ok(f2 == f1 + 1 and p.obj_owner(g)[0] == root[0],
       'aborted owner-staged create does not leak ownership onto reuse')
    p.obj_close(g)
    p.ownerid_fault(3)
    denied(lambda: p.obj_create_ge(sh, b'fault-3'), 'create fault after publication')
    g, f3 = p.obj_create_ge(sh, b'fault-3-retry')
    ok(f3 == f2 + 2 and p.obj_owner(g)[0] == root[0],
       'post-publication fault leaves a fully owner-assigned object')
    p.obj_close(g)
    denied(lambda: p.program_register('Z', '', sh, MPY),
           'empty owner name cannot select the kernel OwnerID')
    lh = p.obj_open_ge_space(sh, MPY)
    ok(p.obj_owner(lh)[0] == root[0],
       'legacy seeded object still resolves to root after migration')
    p.obj_close(lh)

    aown, aoid = p.program_register('A', 'ownerA', sh, MPY,
                                    p.OWNER_PROGRAM_LAUNCHABLE)
    bown, boid = p.program_register('B', 'ownerB', sh, MPY,
                                    p.OWNER_PROGRAM_LAUNCHABLE)
    rootown, doid = p.program_register('D', 'root', sh, MPY,
                                       p.OWNER_PROGRAM_LAUNCHABLE)
    lown, loid = p.program_register('L', 'ownerL', sh, MPY,
                                    p.OWNER_PROGRAM_LAUNCHABLE)
    ok(aown != bown and aown != root[0] and bown != root[0],
       'trusted registration creates distinct immutable owner identities')
    ok(aoid > 0 and boid > 0 and aoid != boid and rootown == root[0],
       'each program receives a private owner-owned executable object')
    ok(lown != aown and lown != bown, 'registered launch domain is distinct')

    pub = b'A-PUBLIC-49'
    a_code = build(A_CODE, AOWN=aown, BOWN=bown, AOID=aoid, MPY=MPY,
                   AOBJ=AOBJ, BOBJ=BOBJ, PUB=pub, SLEEP=SLEEP_CODE,
                   B_CODE=build(B_CODE, AOWN=aown, BOWN=bown, AOID=aoid,
                                AOBJ=AOBJ, BOBJ=BOBJ,
                                PUB=pub, MPY=MPY),
                   EXEC=build(EXEC_CODE, AOWN=aown))
    ah = p.program_launch('A', ('micropython', '-c', a_code),
                          ((sh, p.VEXECUTE | p.VREAD | p.VWRITE |
                            p.VCREATE | p.VCOMMIT | p.VMETA |
                            p.VDELEGATE),))
    ok(p.obj_owner(ah)[0] == aown,
       'registered launch starts an independent process under program owner A')
    ar = waitok(ah, 'A')
    ok(ar[1] == 'EXECOK',
       'A completed object, publication, control and exec checks: ' + ar[1])
    ok(p.obj_owner(ah)[0] == aown,
       'process OwnerID is immutable across its lifecycle and OEXECUTE')
    ok(ar[0] != p.getpid(), 'launched process is independent of the requester')
    denied(lambda: p.process_terminate(ah),
           'launcher has no process control over a registered program')
    p.obj_close(ah)

    # Launcher-first exit: L launches B and an independent owner-rooted
    # observer D, then exits.  D writes B's completion into a root-owned
    # region; the boot process observes it after L is gone.
    reg = p.region_create(8192)
    ro = p.obj_open_handle(reg, p.VREAD | p.VMETA | p.VDELEGATE)
    rw = p.obj_open_handle(reg, p.VREAD | p.VWRITE | p.VMETA | p.VDELEGATE)
    d_code = build(D_CODE, MPY=MPY)
    l_code = build(L_CODE, LONG_B=LONG_B_CODE, D_CODE=d_code)
    lh = p.program_launch('L', ('micropython', '-c', l_code),
                          ((rw, p.VREAD | p.VWRITE | p.VMETA |
                            p.VDELEGATE),))
    lr = waitok(lh, 'L')
    ok(lr[1] == 'LEXIT', 'launcher L exits while its launched work continues')
    p.obj_close(lh)
    p.obj_close(rw)

    base = p.region_map(ro, 0x53000000)
    deadline = p.time_ns() + 15000000000
    while p.region_atomic_load(base + 0) != 1:
        r = p.region_wait(base, 0, 0, 200)
        if r == 3:
            raise AssertionError('result region lost its writer')
        if p.time_ns() > deadline:
            raise AssertionError('D did not report B completion')
    msg = p.region_read(base + 64, 64)
    i = msg.find(b'\0')
    if i >= 0:
        msg = msg[:i]
    ok(msg == b'BALIVE',
       'launched B survives launcher exit and completes voluntarily')
    p.region_unmap(base)
    p.obj_close(ro)
    p.obj_close(reg)

    denied(lambda: p.program_launch('not-registered', ('micropython', '-c', ''), ()),
           'unregistered program launch is rejected')
    eown, eoid = p.program_register('E', 'ownerA', sh, MPY,
                                    p.OWNER_PROGRAM_LAUNCHABLE)
    ok(eown == aown and eoid != aoid,
       'the same trusted owner name resolves to one immutable identity')
    denied(lambda: p.program_launch('evil-root', ('micropython', '-c', ''), ()),
           'unregistered launch cannot create a process')

    state = ('OWNERID-RB1 %d %d' % (aown, bown)).encode()
    p.obj_put_oid(sh, STATE, state)
    p.obj_commit(sh)
    p.obj_close(sh)
    print('T49-FINISH', n[0], 'PASS')
    return 1
