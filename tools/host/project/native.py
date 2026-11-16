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
    print('PROJECT-NATIVE-PASS')
    shared.n+=1
    return shared.n
