class Counter:
    def __init__(self,n=0):self.n=n
shared=Counter()
alias=shared
def init():shared.n=0

def run():
    import plan9 as p
    import space
    count=[0]
    def ok(v,name):
        assert v,name
        count[0]+=1;print('T15-PASS',count[0],name)
    cat=p.obj_devices_open(2184)
    entries=[e for e in p.obj_device_list(cat) if e[1]==2]
    ok(len(entries)==1,'existing raw Ethernet device discovery')
    d=p.obj_device_open(cat,2,entries[0][2],0)
    rx,_,_,_=p.obj_device_recv(d)
    ctl,out=p.obj_fifocreate(4),p.obj_fifocreate(4)
    response=p.obj_subscribe(out,12,4)
    code=p.obj_create(NODE_SOURCE.encode())
    sh=p.obj_open_space(b'init')
    stub="""import plan9 as p
for h in p.obj_list():
    try:i=p.obj_info(h)
    except OSError:continue
    if i[8]==0:
        code=p.obj_read(h,i[3]);p.obj_close(h);break
exec(code.decode())
"""
    pid=space.spawn(0x4f45584543000003,handle=sh,cpu=1,argv=('micropython','-c',stub),vhandles=((d,46),(ctl,40),(out,10),(code,9)))
    p.obj_close(code);p.obj_close(d)
    def receive():
        h,_=p.obj_receive(response,10000)
        try:return eval(p.obj_read(h,8192).decode())
        finally:p.obj_close(h)
    def cmd(c):
        h=p.obj_create(c);p.obj_publish(ctl,h);p.obj_close(h)
        return receive()
    s=receive()
    ok(not s[0],'ordinary MPY child and AES-GCM known-answer/tag checks ready')
    ok(s[2]==b'\xfe\x80'+bytes(12)+b'\x01\x51','static local address')
    cmd(b'P')
    for i in range(250):
        s=cmd(b'S')
        if s[6] is not None and s[6][2]==b'T15:done':break
        p.sleep(20)
    else:raise AssertionError('bounded live ESP exchange timed out: '+repr(s))
    print('T15-LIVE',repr(s))
    ok(s[5] is not None and s[5][0]=='reply' and s[5][3]==b'T15:guest','guest-initiated protected echo round trip')
    ok(s[6][2]==b'T15:done','peer-initiated protected echo delivered')
    ok(s[7]['esp_tx']==4 and s[7]['esp_rx']==4,'four encrypted TX/RX frames through existing device FIFO')
    ok(s[7]['na']==1 and s[7]['ns']==1,'ordinary ND resolves both peers')
    ok(s[7]['esp_bad']==1,'corrupt tag is rejected before echo')
    ok(s[7]['plain_drop']==1,'plaintext echo cannot bypass ESP')
    s=cmd(b'C')
    ok(s[0] and s[1]=='stopped','controlled userspace shutdown')
    got,status=p.wait()
    ok(got==pid and status=='','exact successful MPY child exit')
    ok(p.obj_info(rx)[14]==1,'last device close drains existing RX FIFO')
    for h in (rx,ctl,out,response,cat,sh):p.obj_close(h)
    shared.n+=1
    ok(alias is shared and shared.n==1,'normal parent GE/O graph and alias')
    print('T15-RESULT',count[0],'PASS')
    return shared.n
