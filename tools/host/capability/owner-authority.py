"""Capability authority acceptance: OwnerID only describes provenance.

Real registered MicroPython processes with distinct OwnerIDs validate:
  - matching OwnerID alone grants no GE/O authority,
  - an attenuated GE object grant permits cross-owner reads, not writes,
  - an explicit write grant permits cross-owner mutation, preserving Kowner,
  - a delegated process-control cap permits cross-owner termination,
  - a delegated region writer cap permits cross-owner writable mappings,
  - unregistered open and program registration cannot mint authority.
"""
import plan9 as p

MPY=0x4f45584543000003
AOBJ=0x4f45584543000201
COMMON=r"""
import plan9 as p

def fail(s):
    p.exits('ERR:'+s)

def findkind(kind, required):
    for h in p.obj_list():
        try:
            i=p.obj_info(h)
        except OSError:
            continue
        if i[8]==kind and (i[10] & required)==required:
            return h
    return 0
"""

CREATE=COMMON+r"""
if p.owner_info()[1]!='ownerA':
    fail('creator-identity')
sh=findkind(p.VKIND_SPACE,p.VWRITE|p.VCOMMIT)
if not sh:
    fail('creator-missing-space')
p.obj_put_oid(sh,@AOID@,b'A-OBJECT')
p.obj_commit(sh)
p.exits('CREATED')
"""

NO_GRANT=COMMON+r"""
if p.owner_info()[1]!='ownerA':
    fail('nogrant-identity')
if findkind(p.VKIND_SPACE,0) or findkind(p.VKIND_GE,0):
    fail('inherited-ambient-space')
try:
    p.obj_open_space('init')
except OSError:
    pass
else:
    fail('ambient-open-space')
try:
    p.obj_open_ge_space(1,@AOID@)
except OSError:
    pass
else:
    fail('owner-granted-oid-access')
try:
    p.program_register('forge','root',1,@MPY@)
except OSError:
    pass
else:
    fail('untrusted-program-register')
p.exits('NO-GRANT')
"""

READ_ONLY=COMMON+r"""
if p.owner_info()[1]!='ownerB':
    fail('read-owner')
h=findkind(p.VKIND_GE,p.VREAD|p.VMETA)
if not h:
    fail('read-grant-missing')
if p.obj_owner(h)[1]!='ownerA':
    fail('read-provenance')
if p.obj_read(h,8)!=b'A-OBJECT':
    fail('read-delegated-bytes')
try:
    p.obj_write(h,b'bad')
except OSError:
    pass
else:
    fail('attenuation-write')
try:
    p.obj_commit(h)
except OSError:
    pass
else:
    fail('attenuation-commit')
try:
    p.obj_open_handle(h,p.VWRITE)
except OSError:
    pass
else:
    fail('attenuation-escalation')
try:
    p.obj_open_space('init')
except OSError:
    pass
else:
    fail('read-grant-minted-space')
p.exits('READ-ONLY')
"""

WRITE_CAP=COMMON+r"""
if p.owner_info()[1]!='ownerB':
    fail('write-owner')
h=findkind(p.VKIND_GE,p.VREAD|p.VWRITE|p.VCOMMIT)
if not h:
    fail('write-grant-missing')
if p.obj_owner(h)[1]!='ownerA':
    fail('write-provenance')
p.obj_write(h,b'B-UPDATE')
p.obj_commit(h)
if p.obj_owner(h)[1]!='ownerA':
    fail('mutable-provenance')
if p.obj_read(h,8)!=b'B-UPDATE':
    fail('write-result')
p.exits('WRITE-CAP')
"""

OBSERVE_ONLY=COMMON+r"""
h=findkind(p.VKIND_PROCESS,p.VOBSERVE)
if not h:
    fail('observe-missing')
try:
    p.process_terminate(h)
except OSError:
    pass
else:
    fail('observe-escalation')
p.exits('OBSERVE-ONLY')
"""

PROCESS_CONTROL=COMMON+r"""
if p.owner_info()[1]!='ownerB':
    fail('control-owner')
h=findkind(p.VKIND_PROCESS,p.VCONTROL)
if not h:
    fail('control-missing')
if p.obj_owner(h)[1]!='root':
    fail('control-target-provenance')
p.process_terminate(h)
p.exits('CROSS-CONTROL')
"""

REGION_WRITE=COMMON+r"""
if p.owner_info()[1]!='ownerB':
    fail('region-owner')
h=findkind(p.VKIND_REGION,p.VWRITE|p.VREAD)
if not h:
    fail('region-missing')
if p.obj_owner(h)[1]!='root':
    fail('region-provenance')
try:
    p.obj_revoke(h)
except OSError:
    pass
else:
    fail('region-control-escalation')
b=p.region_map(h,0x51000000)
p.region_write(b+64,b'FRAME')
p.region_atomic_store(b,1)
p.region_unmap(b)
p.exits('REGION-WRITE')
"""

REVOKE_OTHER_OWNER=COMMON+r"""
if p.owner_info()[1]!='ownerB':
    fail('revoke-other-owner')
ge=findkind(p.VKIND_GE,p.VREAD|p.VMETA)
ready=findkind(p.VKIND_REGION,p.VWRITE)
go=0
for h in p.obj_list():
    i=p.obj_info(h)
    if i[8]==p.VKIND_REGION and (i[10]&p.VREAD) and not (i[10]&p.VWRITE):
        go=h
        break
if not ge or not ready or not go:
    fail('revoke-inherited-handles')
if p.obj_read(ge,8)!=b'B-UPDATE':
    fail('revoke-before-read')
bready=p.region_map(ready,0x56000000)
bgo=p.region_map(go,0x57000000)
p.region_atomic_store(bready,1)
p.region_wake(bready,0,0x7fffffff)
while p.region_atomic_load(bgo)==0:
    p.region_wait(bgo,0,0,200)
try:
    p.obj_read(ge,8)
except OSError:
    pass
else:
    fail('revoked-grant-still-works')
try:
    p.obj_owner(ge)
except OSError:
    pass
else:
    fail('revoked-metadata-still-works')
p.region_unmap(bready)
p.region_unmap(bgo)
p.exits('CROSS-REVOKED')
"""

SLEEP=COMMON+r"""
p.sleep(60000)
p.exits('SLEEP-UNEXPECTED')
"""

# The native boot reporter requires an initial persistent alias graph.
# Keep it separate from security operations exercised in run().
class Counter:
    def __init__(self):
        self.n = 0

shared = Counter()
alias = shared

def init():
    shared.n = 0

def run():
    import space

    seq=[0]
    def ok(cond,label):
        if not cond:
            raise AssertionError(label)
        seq[0]+=1
        print('CAPAUTH-PASS',seq[0],label)
    def built(t,**kw):
        for key,val in kw.items():
            t=t.replace('@'+key+'@',repr(val))
        return t
    def launched(name,code,grants=()):
        return p.program_launch(name,('micropython','-c',code),grants)
    def waited(h,status,label):
        r=p.process_wait(h,-1)
        ok(r[8]==0 and r[2]==p.OBJ_PROC_CAUSE_EXIT and r[1]==status,
           label+':'+r[1])
        p.obj_close(h)

    root=p.owner_info()
    ok(root[1]=='root','trusted boot OwnerID')
    sh=p.obj_open_space('init')
    aown,aoid=p.program_register('A','ownerA',sh,MPY)
    bown,boid=p.program_register('B','ownerB',sh,MPY)
    ok(aown!=bown and aown!=root[0] and bown!=root[0],
       'registered identities are distinct from root')
    full=(sh,p.VREAD|p.VWRITE|p.VCREATE|p.VCOMMIT|
             p.VEXECUTE|p.VMETA|p.VDELEGATE)
    waited(launched('A',built(CREATE,AOID=AOBJ),(full,)),
           'CREATED','owner A creates an A-owned GE/O object')
    gh=p.obj_open_ge_space(sh,AOBJ)
    ok(p.obj_owner(gh)[0]==aown and p.obj_read(gh,8)==b'A-OBJECT',
       'boot may open A object through its explicit root Space grant')
    waited(launched('A',built(NO_GRANT,AOID=AOBJ,MPY=MPY)),
           'NO-GRANT','OwnerID alone never grants the matching object')
    ro=(gh,p.VREAD|p.VMETA)
    waited(launched('B',READ_ONLY,(ro,)),
           'READ-ONLY','cross-owner read works and Vwrite escalation fails')
    rw=(gh,p.VREAD|p.VWRITE|p.VCOMMIT|p.VMETA)
    waited(launched('B',WRITE_CAP,(rw,)),
           'WRITE-CAP','cross-owner GE write/commit works without changing OwnerID')
    p.obj_close(gh)
    gh=p.obj_open_ge_space(sh,AOBJ)
    ok(p.obj_owner(gh)[0]==aown and p.obj_read(gh,8)==b'B-UPDATE',
       'GE/O Kowner remains owner A after authorized owner B write')
    p.obj_close(gh)

    # A source revokes an already inherited grandchild in owner B. Separate
    # region channels provide an explicit READY/GO barrier (no sleep race).
    gh=p.obj_open_ge_space(sh,AOBJ)
    share=p.obj_open_handle(gh,p.VREAD|p.VMETA|p.VDELEGATE)
    ready=p.region_create(8192)
    go=p.region_create(8192)
    ready_ro=p.obj_open_handle(ready,p.VREAD|p.VMETA)
    ready_base=p.region_map(ready_ro,0x54000000)
    go_base=p.region_map(go,0x55000000)
    delegated=((share,p.VREAD|p.VMETA),
               (ready,p.VREAD|p.VWRITE|p.VMETA),
               (go,p.VREAD|p.VMETA))
    bh=launched('B',REVOKE_OTHER_OWNER,delegated)
    deadline=p.time_ns()+20000000000
    while p.region_atomic_load(ready_base)==0:
        p.region_wait(ready_base,0,0,200)
        if p.time_ns()>deadline:
            raise AssertionError('cross-owner revoke handshake timed out')
    p.cap_revoke(share)
    p.region_atomic_store(go_base,1)
    p.region_wake(go_base,0,0x7fffffff)
    waited(bh,'CROSS-REVOKED',
           'cross-owner revoked descendants cannot read or inspect object')
    ok(p.obj_read(share,8)==b'B-UPDATE' and p.obj_read(gh,8)==b'B-UPDATE',
       'source and its ancestor remain authorized after descendant revoke')
    p.region_unmap(ready_base)
    p.region_unmap(go_base)
    for h in (ready_ro,ready,go,share,gh):
        p.obj_close(h)

    # Root-owned independent target; process handles are the only control.
    target=space.spawn(MPY,handle=sh,
                       argv=('micropython','-c',SLEEP))
    ok(p.obj_owner(target)[0]==root[0],
       'independent child retains root provenance')
    obs=(target,p.VOBSERVE|p.VMETA)
    waited(launched('B',OBSERVE_ONLY,(obs,)),
           'OBSERVE-ONLY','observation cannot escalate to process control')
    ctl=(target,p.VCONTROL|p.VMETA)
    waited(launched('B',PROCESS_CONTROL,(ctl,)),
           'CROSS-CONTROL','B can terminate root process with delegated control')
    result=p.process_wait(target,-1)
    ok(result[2]==p.OBJ_PROC_CAUSE_TERMINATED,
       'root observes cross-owner termination through its own grant')
    p.obj_close(target)

    reg=p.region_create(8192)
    ok(p.obj_owner(reg)[0]==root[0],'region provenance is root')
    writer=(reg,p.VREAD|p.VWRITE|p.VMETA)
    waited(launched('B',REGION_WRITE,(writer,)),
           'REGION-WRITE','B can publish via delegated root region Vwrite')
    view=p.obj_open_handle(reg,p.VREAD|p.VMETA)
    base=p.region_map(view,0x52000000)
    ok(p.region_atomic_load(base)==1 and
       p.region_read(base+64,5)==b'FRAME',
       'root RO view observes cross-owner published payload')
    p.region_unmap(base)
    p.obj_close(view)
    p.obj_close(reg)
    p.obj_close(sh)
    print('CAPAUTH-FINISH',seq[0],'PASS')
    return 1
