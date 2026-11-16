class Counter:
    def __init__(self): self.n=0; self.root_oid=0
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
    if shared.root_oid:
        import project
        restored=project.from_handle(p.project_open(v,shared.root_oid,rights))
        with restored.open('left/shared') as item:
            assert item.raw()==b'same'
            ident=item.identity()
        with restored.open('right/shared') as item:assert item.identity()==ident
        with restored.open('writable/newfile') as item:assert item.raw()==b'written'
        restored.activate()
        import pkg
        assert pkg.value==73
        print('PROJECT-RESTORE-PASS')
        shared.n+=1
        return shared.n
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
    assert ns.links(sharedfile)==2
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
    wide=ns.mkdir('wide')
    for i in range(70):
        item=wide.create('n'+str(i),b'x');item.close()
    assert len(wide.entries())==70
    longname='L'*255
    item=wide.create(longname,b'long');item.close()
    with wide.resolve(longname) as item:assert item.raw()==b'long'
    chain=ns.mkdir('chain')
    for i in range(18):
        next=chain.mkdir('d');chain.close();chain=next
    chain.close()
    # Native ObjectSpace operations address the same durable bindings.
    rows,cursor,epoch=p.space_list(left.handle)
    assert any(row[1]=='shared' for row in rows)
    before=sharedfile.identity()
    right.rename('shared',left,'alias',False)
    with left.resolve('alias') as item:assert item.identity()==before
    # Restore fixture keeps both aliases to the one object.
    right.link('shared',sharedfile)
    left.remove('alias')
    assert ns.links(sharedfile)==2
    shared.root_oid=p.project_identity(root)[1]
    p.program_register('project-reader','project-reader',v,0x4f45584543000003)
    reader=p.project_call(root,8,None,0,0,None,0,1|8|128|8192)
    childcode="import plan9 as p; import project; roots=p.space_roots(); assert len(roots)==1; assert len(p.obj_list())==1; ns=project.from_handle(roots[0]); assert ns.open('src/moi').raw()==b'updated\\n'; ns.activate(); import pkg; assert pkg.value==73; p.exits('')"
    process=p.program_launch('project-reader',('project-reader','-c',childcode),((reader,1|8|128|8192),))
    state=p.process_wait(process,30000)
    assert state[1]=='',state
    p.obj_close(process);p.obj_close(reader)
    print('PROJECT-ISOLATED-READER-PASS')
    print('PROJECT-SCALE-PASS')
    print('PROJECT-COMPOSITION-PASS')
    print('PROJECT-IMPORT-PASS')
    print('PROJECT-NATIVE-PASS')
    shared.n+=1
    return shared.n
