"""OwnerID crash-recovery and corruption module for the recovery runner.

The host replaces the MODE placeholder with create-101, create-102,
create-103, migrate-104 or corrupt before mpy-cross compiles this file.
"""
import plan9 as p
import space

MODE = '__MODE__'
STATE = 0x4f45584543000100
MP_OID = 0x4f45584543000003
BASE = 0x0000010000000000
COUNT = 120


class Counter:
    def __init__(self):
        self.n = 0


shared = Counter()
alias = shared


def init():
    shared.n = 0


def fail(s):
    p.exits('ERR:' + MODE + ':' + s)


def read_state(sh):
    try:
        h = p.obj_open_ge_space(sh, STATE)
    except OSError:
        return None
    try:
        d = p.obj_read(h, 256)
    except OSError:
        d = None
    finally:
        p.obj_close(h)
    if not d:
        return None
    f = d.split()
    if not f or f[0] != b'RECOVERY1':
        return None
    return [x.decode() for x in f[1:]]


def write_state(sh, *fields):
    data = ('RECOVERY1 ' + ' '.join(str(x) for x in fields)).encode()
    p.obj_put_oid(sh, STATE, data)
    p.obj_commit(sh)


def expected(i):
    if i % 10 == 9:
        return None
    if i % 17 == 0:
        return b''
    if i % 3 == 0:
        return ('v2-%04d' % i).encode()
    return ('v1-%04d' % i).encode()


def probe(sh, oid):
    h = p.obj_open_ge_space(sh, oid)
    try:
        data = p.obj_read(h, 64)
    except OSError:
        p.obj_close(h)
        return (False, None, None)
    own = p.obj_owner(h)[0]
    p.obj_close(h)
    return (True, own, data)


def create_flow(case):
    sh = p.obj_open_space('init')
    st = read_state(sh)
    if st is not None and st[0] == 'CRASH':
        verify_create(sh, int(st[1]), int(st[2]))
        return 1
    if st is not None:
        fail('unexpected-state')
    g, base = p.obj_create_ge(sh, b'recovery-base')
    p.obj_close(g)
    target = base + 1
    write_state(sh, 'CRASH', case, target)
    p.ownerid_fault(case)
    p.obj_create_ge(sh, b'recovery-target')
    fail('create-did-not-stop')


def verify_create(sh, case, target):
    root = p.owner_info()[0]
    absent = False
    try:
        h = p.obj_open_ge_space(sh, target)
    except OSError as e:
        fail('target-open:' + repr(e))
    try:
        data = p.obj_read(h, 64)
    except OSError:
        absent = True
    else:
        if p.obj_owner(h)[0] != root:
            fail('target-owner')
        if data != b'recovery-target':
            fail('target-data')
    finally:
        p.obj_close(h)
    print('RECOVERY-TARGET case=%d target=%d present=%d' %
          (case, target, 1 - int(absent)))
    g, oid = p.obj_create_ge(sh, b'recovery-retry')
    if p.obj_owner(g)[0] != root:
        fail('retry-owner')
    p.obj_close(g)
    if absent:
        if oid != target:
            fail('retry-reuse:%d:%d' % (oid, target))
        present, own, data = probe(sh, target)
        if not present or own != root or data != b'recovery-retry':
            fail('reused-owner-or-data')
    else:
        if oid != target + 1:
            fail('retry-after-publish:%d:%d' % (oid, target))
        present, own, data = probe(sh, target)
        if not present or own != root or data != b'recovery-target':
            fail('published-changed')
    print('RECOVERY-VERIFY-FINISH case=%d PASS' % case)
    return 1


def check_legacy(sp, root, phase):
    for i in range(COUNT):
        oid = BASE + i
        present, own, data = probe(sp, oid)
        exp = expected(i)
        if exp is None:
            if present:
                fail('%s-tomb-present:%d' % (phase, i))
            continue
        if not present:
            fail('%s-missing:%d' % (phase, i))
        if own != root:
            fail('%s-owner:%d' % (phase, i))
        if data != exp:
            fail('%s-data:%d' % (phase, i))


def migrate_flow():
    sh = p.obj_open_space('init')
    st = read_state(sh)
    if st is not None and st[0] == 'MIGRATE':
        verify_migrate()
        return 1
    if st is not None:
        fail('unexpected-state')
    write_state(sh, 'MIGRATE', '104', '0')
    sp = p.obj_open_space('/')
    p.ownerid_fault(104)
    p.obj_create_ge(sp, b'migrate-trigger')
    fail('migrate-did-not-stop')


def verify_migrate():
    root = p.owner_info()[0]
    sp = p.obj_open_space('/')
    # Before retry the header must not claim owner mode: every legacy live
    # object still resolves through the documented seeded-owner rule.
    check_legacy(sp, root, 'pre')
    g, oid = p.obj_create_ge(sp, b'migrate-retry')
    if p.obj_owner(g)[0] != root:
        fail('retry-owner')
    p.obj_close(g)
    check_legacy(sp, root, 'post')
    e = p.obj_open_space('empty')
    g, eoid = p.obj_create_ge(e, b'empty-space-object')
    if p.obj_owner(g)[0] != root:
        fail('empty-space-owner')
    p.obj_close(g)
    print('RECOVERY-VERIFY-FINISH case=104 PASS')
    return 1


def abort_migrate_flow():
    root = p.owner_info()[0]
    sp = p.obj_open_space('/')
    p.ownerid_fault(4)
    try:
        p.obj_create_ge(sp, b'abort-trigger')
    except OSError:
        pass
    else:
        fail('abort-did-not-fail')
    # The failed migration must leave the published Space in its legacy
    # pre-migration state, with every live object still readable.
    check_legacy(sp, root, 'abort-pre')
    g, oid = p.obj_create_ge(sp, b'abort-retry')
    if p.obj_owner(g)[0] != root:
        fail('abort-retry-owner')
    p.obj_close(g)
    check_legacy(sp, root, 'abort-post')
    e = p.obj_open_space('empty')
    g, eoid = p.obj_create_ge(e, b'empty-space-object')
    if p.obj_owner(g)[0] != root:
        fail('abort-empty-space-owner')
    p.obj_close(g)
    print('RECOVERY-ABORT-FINISH PASS')
    return 1


def corrupt_flow():
    root = p.owner_info()[0]
    sh = p.obj_open_space('init')
    # The authentic unmigrated seeded Space still resolves to the seeded
    # legacy owner; a missing record there is not corruption.
    h = p.obj_open_ge_space(sh, MP_OID)
    if p.obj_owner(h)[0] != root:
        fail('legacy-owner')
    p.obj_close(h)
    sp = p.obj_open_space('/')
    labels = ('missing', 'badmagic', 'badlen', 'zeroowner')
    for i in range(4):
        oid = BASE + i
        label = labels[i]
        try:
            h = p.obj_open_ge_space(sp, oid)
        except OSError as e:
            msg = str(e)
            if 'owner' not in msg and 'corrupt' not in msg:
                fail(label + '-open:' + msg)
        else:
            p.obj_close(h)
            fail(label + '-open-allowed')
        try:
            p.obj_put_oid(sp, oid, b'x')
            p.obj_commit(sp)
        except OSError as e:
            msg = str(e)
            if 'owner' not in msg and 'corrupt' not in msg:
                fail(label + '-write:' + msg)
        else:
            fail(label + '-write-allowed')
    print('RECOVERY-CORRUPT-FINISH PASS')
    return 1


def run():
    if MODE.startswith('create-'):
        return create_flow(int(MODE.split('-')[1]))
    if MODE == 'migrate-104':
        return migrate_flow()
    if MODE == 'abort-migrate':
        return abort_migrate_flow()
    if MODE == 'corrupt':
        return corrupt_flow()
    fail('unknown-mode')
    return 0
