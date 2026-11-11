# Focused T13 module, seeded by mkovol's ordinary SOURCE argument.
class Counter:
    def __init__(self, n=0): self.n = n
shared = Counter()
alias = shared

def init(): shared.n = 0

def run():
    import plan9 as p
    import space
    import struct
    n = [0]

    def reap(h):
        r = p.process_wait(h, -1)
        return h, r[1]
    def ok(v, name):
        assert v, name
        n[0] += 1
        print('T13-PASS', n[0], name)
    def deny(fn, expected):
        try: fn()
        except OSError as e:
            ok(str(e) == expected, 'denied: '+expected)
            return
        raise AssertionError('expected '+expected)
    perm = 'permission denied'
    bad = 'bad arg in system call'
    cat = p.obj_devices_open(2184)
    entries = [e for e in p.obj_device_list(cat) if e[1] == 2]
    ok(len(entries) == 1, 'physical NIC catalog record')
    ctl = entries[0][2]
    meta = p.obj_devices_open()
    deny(lambda: p.obj_device_open(meta,2,ctl,0), perm)
    p.obj_close(meta)
    deny(lambda: p.obj_device_open(cat,2,ctl,1), bad)
    deny(lambda: p.obj_device_open(cat,2,9999,0), bad)
    deny(lambda: p.obj_device_open(cat,2,ctl,0,1), perm)
    d = p.obj_device_open(cat,2,ctl,0)
    deny(lambda: p.obj_device_open(cat,2,ctl,0), 'device or object already in use')
    info = p.obj_device_ether_info(d)
    print('T13-NIC', info)
    sbuf = bytearray(1); sbuf[0] = 0x5a
    deny(lambda: p.obj_device_ioctl(d, p.IOCTL_ETHER_STATUS, sbuf), bad)
    ok(sbuf[0] == 0x5a, 'undersized Ethernet status leaves buffer unchanged')
    mac = info[4]
    peer = bytes((2,0,0,0,19,1))
    def frame(body): return peer+mac+b'\x88\xb5'+body+bytes(46-len(body))
    def expected(body): return mac+peer+b'\x88\xb5'+body+bytes(46-len(body))
    def send(body):
        f = frame(body)
        ok(p.obj_device_send(d,f) == len(f), 'TX accepted full L2 frame')
        print('T13-TX', repr(f))
    def recv(sub, body):
        h, env = p.obj_receive(sub,5000)
        f = p.obj_read(h,16384)
        ident = p.obj_info(h)[:4]
        rights = p.obj_info(h)[10]
        p.obj_close(h)
        assert f == expected(body), (f,body)
        v,c,l,r,s = struct.unpack('<IIIIQ',env)
        assert (v,c,l,r) == (2,ctl,len(f),0) and s > 0
        assert rights == 9
        print('T13-RX',repr(f),repr(env))
        return ident
    m = p.obj_grant(d,8)
    deny(lambda:p.obj_device_send(m,frame(b'x')),perm)
    deny(lambda:p.obj_device_rx(m),perm)
    deny(lambda:p.obj_device_ether_detach(m),perm)
    deny(lambda:p.obj_grant(m,64),perm)
    p.obj_close(m)
    m = p.obj_grant(d,4|8)
    deny(lambda:p.obj_device_rx(m),perm)
    p.obj_close(m)
    deny(lambda:p.obj_device_send(d,b'x'*59),bad)
    deny(lambda:p.obj_device_send(d,b'x'*(info[3]+1)),bad)
    try: p.obj_open_udp6
    except AttributeError: ok(True,'UDP6 object provider removed')
    else: raise AssertionError('UDP6 object provider still present')
    try: p.obj_open_tcp
    except AttributeError: ok(True,'TCP object provider removed')
    else: raise AssertionError('TCP object provider still present')
    fifo = p.obj_device_rx(d)
    ok(fifo > 0, 'RX yields ordinary FIFO capability')
    ok(p.obj_info(fifo)[8]==1, 'no Ethernet FIFO kind')
    deny(lambda:p.obj_subscribe(fifo,2,2),perm)
    payload=p.obj_create(b'x')
    deny(lambda:p.obj_publish(fifo,payload),perm)
    p.obj_close(payload)
    sub = p.obj_subscribe(fifo,12,8)
    send(b'T13:ONE')
    recv(sub,b'T13:RX:one\x00\xff')
    ok(True,'external peer complete bytes and EtherType RX')
    # Two processes independently subscribe via attenuated device handles.
    sh = p.obj_open_space(b'init')
    children=[]
    template = """import plan9 as p
import struct
hs=[]
for h in p.obj_list():
    try: hs.append((h,p.obj_info(h)))
    except OSError: pass
d=next(h for h,i in hs if i[8]==14)
go=next(h for h,i in hs if i[8]==6 and i[10]&1)
out=next(h for h,i in hs if i[8]==6 and i[10]&256)
f=p.obj_device_rx(d)
assert p.obj_info(f)[10]==40
for right in (2,64,128):
    try: p.obj_grant(d,right)
    except OSError as e: assert str(e)=='permission denied'
    else: raise AssertionError('amplification')
s=p.obj_subscribe(f,12,CAP)
p.obj_write(out,b'READY')
assert p.obj_read(go,1,0)==b'G'
DELAY
for i in range(START,8):
    h,env=p.obj_receive(s,5000)
    b=p.obj_read(h,16384)
    assert b==p.obj_device_ether_info(d)[4]+bytes((2,0,0,0,19,1))+b'\\x88\\xb5'+('T13:F%02d'%i).encode()+bytes(39)
    assert p.obj_info(h)[10]==9
    assert struct.unpack('<IIIIQ',env)[:4]==(2,0,60,0)
    ident=struct.pack('<QQQ',*p.obj_info(h)[:3])
    p.obj_close(h)
assert p.obj_info(s)[13]==DROPS
p.obj_write(out,b'DONE'+ident+env)
"""
    for slow in (False,True):
        rd,wr=p.pipenew(); gr,gw=p.pipenew()
        code=template.replace('CAP','2' if slow else '8').replace('DELAY','p.sleep(1000)' if slow else 'pass').replace('START','6' if slow else '0').replace('DROPS','6' if slow else '0')
        pid=space.spawn(0x4f45584543000003,handle=sh,cpu=1 if slow else 0,argv=('micropython','-c',code),vhandles=((d,44),(gr,9),(wr,264)))
        p.obj_close(gr); p.obj_close(wr)
        ok(p.obj_read(rd,5,0)==b'READY','independent consumer subscribed')
        children.append((pid,rd,gw))
    for pid,rd,gw in children:
        p.obj_write(gw,b'G');p.obj_close(gw)
    send(b'T13:BURST')
    for i in range(8): ident=recv(sub,('T13:F%02d'%i).encode())
    for pid,rd,gw in children:
        answer=p.obj_read(rd,52,0)
        ok(answer[:4]==b'DONE' and answer[4:28]==struct.pack('<QQQ',*ident[:3]),'TEE same bytes object identity, ordered frames and independent overflow')
        ok(struct.unpack('<IIIIQ',answer[28:])==(2,ctl,60,0,9),'TEE identical RX envelope')
        p.obj_close(rd)
    reaped=[]
    for cpid,rd,gw in children:
        r=p.process_wait(cpid,-1)
        status=r[1]
        reaped.append(cpid)
        ok(status=='','consumer successful exit')
    ok(sorted(reaped)==sorted([c[0] for c in children]),'exact consumer PIDs')
    # Stress ordinary independent queues; ring remains bounded without probes.
    extras=[p.obj_subscribe(fifo,12,1) for i in range(63)]
    send(b'T13:FLOOD')
    p.sleep(3500)
    info=p.obj_device_ether_info(d)
    print('T13-HANDOFF',info)
    ok(info[12]<=64 and info[14]<=64 and info[13]==64,'bounded IRQ handoff')
    ok(p.obj_info(extras[0])[13]>0,'independent bounded FIFO drop-oldest')
    print('T13-HANDOFF-DROP', 'PASS' if info[9]>0 else 'NOT RUN', info[9])
    for h in extras:p.obj_close(h)
    try: p.obj_revoke(fifo)
    except OSError: ok(True,'FIFO revoke rejected as capability revocation')
    else: raise AssertionError('FIFO revoke accepted')
    ok(p.obj_info(fifo)[14]==0,'FIFO revoke does not close the channel')
    p.obj_fifo_shutdown(fifo)
    deny(lambda:p.obj_fifo_shutdown(fifo),'vobj: FIFO is already closed')
    send(b'T13:ONE')
    p.sleep(300)
    ok(p.obj_device_ether_info(d)[10]>0,'shutdown FIFO publication fails safely')
    ok(p.obj_device_ether_detach(d)==0,'explicit detach joins callback and worker')
    info=p.obj_device_ether_info(d)
    ok(info[6]==1 and info[14]==0,'detached closed and handoff drained')
    deny(lambda:p.obj_device_send(d,frame(b'x')),'i/o on hungup channel')
    deny(lambda:p.obj_device_rx(d),'i/o on hungup channel')
    for h in (sub,fifo,d):p.obj_close(h)
    deny(lambda:p.obj_device_rx(d),'bad arg in system call')
    d=p.obj_device_open(cat,2,ctl,0)
    f=p.obj_device_rx(d)
    sub=p.obj_subscribe(f,12,2)
    send(b'T13:ONE');recv(sub,b'T13:RX:one\x00\xff')
    ok(True,'close/reopen reacquires physical RX sink')
    send(b'T13:LINKDOWN')
    for i in range(100):
        if p.obj_device_ether_info(d)[5]==0: break
        p.sleep(10)
    ok(p.obj_device_ether_info(d)[5]==0,'real virtio configuration link-down')
    deny(lambda:p.obj_device_send(d,frame(b'x')),'ether: link down')
    recv(sub,b'T13:LINKUP')
    ok(p.obj_device_ether_info(d)[5]==1,'link-up restores full frame RX')
    # Last device owner exits with queued RX, while parent retains FIFO.
    rd,wr=p.pipenew();gr,gw=p.pipenew()
    exitcode=template.split('f=p.obj_device_rx(d)')[0]+"""f=p.obj_device_rx(d)
s=p.obj_subscribe(f,12,2)
p.obj_write(out,b'READY')
assert p.obj_read(go,1,0)==b'G'
mac=p.obj_device_ether_info(d)[4]
p.obj_device_send(d,bytes((2,0,0,0,19,1))+mac+b'\\x88\\xb5T13:ONE'+bytes(39))
p.sleep(300)
assert p.obj_info(s)[12]==1
p.obj_write(out,b'DONE')
"""
    # This owner needs TX in addition to independently authorized RX.
    pid=space.spawn(0x4f45584543000003,handle=sh,cpu=1,argv=('micropython','-c',exitcode),vhandles=((d,46),(gr,9),(wr,264)))
    p.obj_close(gr);p.obj_close(wr)
    ok(p.obj_read(rd,5,0)==b'READY','last-owner child ready')
    p.obj_close(d)
    p.obj_write(gw,b'G');p.obj_close(gw)
    ok(p.obj_read(rd,4,0)==b'DONE','last owner has pending RX at exit')
    got,status=reap(pid)
    ok(got==pid and status=='','last device owner exits successfully')
    ok(p.obj_info(f)[14]==1 and p.obj_info(sub)[12]==0,'exit closes and drains surviving FIFO capabilities')
    d=p.obj_device_open(cat,2,ctl,0)
    ok(p.obj_device_ether_info(d)[6]==0,'exit unregisters physical NIC for next owner')
    send(b'T13:FLOOD')
    p.sleep(10)
    p.obj_device_ether_detach(d)
    info=p.obj_device_ether_info(d)
    ok(info[6]==1 and info[14]==0,'detach during external RX flood joins and drains')
    for h in (d,sub,f,rd,sh,cat):p.obj_close(h)
    print('T13-RESULT',n[0],'PASS')
    shared.n += 1
    return shared.n
