class Counter:
    def __init__(self): self.n=0
shared=Counter()
alias=shared

def checksum(p):
    h=14695981039346656037
    for b in p: h=((h^b)*1099511628211)&0xffffffffffffffff
    return h

def frame(raw):
    return raw+b'\xa0'+b'P2OTAIL1'+(1).to_bytes(4,'big')+bytes(4)+len(raw).to_bytes(8,'big')+(1).to_bytes(8,'big')

def empty():
    p=b'P2PROJ01'+(1).to_bytes(8,'big')+bytes(16)
    return frame(p+checksum(p).to_bytes(8,'big'))

def init(): pass

def run():
    import plan9 as p
    v=p.obj_open_volume()
    rights=1|8|64|128|256|2048|4096|8192
    print("PROJECT STEP root")
    root=p.project_open(v,0,rights,empty())
    print("PROJECT STEP child")
    p.project_call(root,2,'src',0,0,empty(),19,rights)
    print("PROJECT STEP resolve")
    sub=p.project_call(root,7,'src')
    p.project_call(sub,2,'moi',0,0,frame(b'moi\n'),3,rights)
    print("PROJECT STEP file resolve")
    f=p.project_call(sub,7,'moi')
    print("PROJECT STEP info")
    before=p.obj_info(f)[2]
    assert p.project_call(f,1).startswith(b'moi\n')
    p.project_call(f,6,None,0,0,frame(b'updated\n'))
    assert p.obj_info(f)[2]==before
    ro=p.project_call(root,8,None,0,0,None,0,1|8|128)
    rsub=p.project_call(ro,7,'src')
    rf=p.project_call(rsub,7,'moi')
    assert p.project_call(rf,1).startswith(b'updated\n')
    try:p.project_call(rf,6,None,0,0,frame(b'forbidden'))
    except OSError:pass
    else:raise AssertionError('read-only project escalated')
    import project
    ns=project.from_handle(root)
    module=ns.create('hello.py',b'value=42\n');module.close()
    ns.activate()
    import hello
    assert hello.value==42
    pkg=ns.mkdir('pkg')
    obj=pkg.create('__init__.py',b'from . import part\nvalue=part.value\n');obj.close()
    obj=pkg.create('part.py',b'value=73\n');obj.close()
    import pkg
    assert pkg.value==73
    left=ns.mkdir('left');right=ns.mkdir('right')
    sharedfile=left.create('shared',b'same')
    right.link('shared',sharedfile)
    union=ns.compose('union',[left,right])
    intersection=ns.compose('intersection',[left,right],operation='intersection')
    assert union.resolve('shared').identity()==sharedfile.identity()
    assert intersection.resolve('shared').raw()==b'same'
    conflict=left.create('conflict',b'left');conflict.close()
    conflict=right.create('conflict',b'right');conflict.close()
    try:union.resolve('conflict')
    except OSError:pass
    else:raise AssertionError('union selected ambiguous label')
    try:union.create('denied',b'no')
    except OSError:pass
    else:raise AssertionError('read-only union wrote')
    writable=ns.compose('writable',[left,right],write_source=0)
    created=writable.create('newfile',b'written');created.close()
    assert left.resolve('newfile').raw()==b'written'
    print('PROJECT-COMPOSITION-PASS')
    print('PROJECT-IMPORT-PASS')
    print('PROJECT-NATIVE-PASS')
    shared.n+=1
    return shared.n
