# Focused T14 ordinary init module. Runner appends the unmodified node and
# child program as a source string; no host implementation substitutes for it.
class Counter:
    def __init__(self, n=0): self.n = n
shared = Counter()
alias = shared

def init(): shared.n = 0

def run():
    import plan9 as p
    import space
    n = [0]
    def ok(v, label):
        assert v, label
        n[0] += 1
        print('T14-PASS', n[0], label)
    cat = p.obj_devices_open(2184)
    entries = [e for e in p.obj_device_list(cat) if e[1] == 2]
    ok(len(entries) == 1, 'existing T13 NIC discovery')
    ctl = entries[0][2]
    sh = p.obj_open_space(b'init')
    child_stub = """import plan9 as p
for h in p.obj_list():
    try: i=p.obj_info(h)
    except OSError: continue
    if i[8]==0:
        source=p.obj_read(h,i[3]);p.obj_close(h)
        break
exec(source.decode())
"""
    def start():
        d = p.obj_device_open(cat,2,ctl,0)
        rf,_,_,_ = p.obj_device_recv(d)
        command, output = p.obj_fifocreate(4), p.obj_fifocreate(4)
        response = p.obj_subscribe(output,12,4)
        code = p.obj_create(NODE_SOURCE.encode())
        pid = space.spawn(0x4f45584543000003,handle=sh,cpu=1,
            argv=('micropython','-c',child_stub),
            vhandles=((d,46),(command,40),(output,10),(code,9)))
        p.obj_close(code)
        p.obj_close(d)  # child is sole device owner; FIFO cap is not ownership.
        return pid, command, output, response, rf
    def receive():
        h,_=p.obj_receive(response,10000)
        try:return eval(p.obj_read(h,8192).decode())
        finally:p.obj_close(h)
    def cmd(c):
        h=p.obj_create(c)
        p.obj_publish(command,h)
        p.obj_close(h)
        return receive()
    def until(test, attempts=250):
        for i in range(attempts):
            s=cmd(b'S')
            if test(s):return s
            p.sleep(20)
        raise AssertionError('bounded service condition timed out: '+repr(s))
    pid,command,output,response,rf=start()
    s=receive()
    ok(s[2]==b'\xfe\x80'+bytes(12)+b'\x01\x41' and s[3]==b'RT\x00\x124V','actual configured address and NIC MAC')
    ok(s[7]['bad_eth']==2,'parser-only truncated Ethernet/IPv6 checks (not NIC evidence)')
    s=cmd(b'P')
    s=until(lambda s:s[5] is not None)
    ok(s[5][0]=='reply' and s[5][3]==b'T14:START','guest-initiated echo round trip via real ND')
    ok(s[7]['nd_tx']==1 and s[7]['na']==1 and s[7]['echo_reply']==1,'guest resolves peer with solicited NS/NA')
    ok(any(a==b'\xfe\x80'+bytes(12)+b'\x01\x42' and v[0]==bytes((2,0,0,0,20,1)) for a,v in s[4]),'learned peer IPv6-to-MAC mapping')
    s=until(lambda s:s[6] is not None and s[6][2]==b'T14:PEER-DONE')
    print('T14-LIVE',repr(s))
    stats=s[7]
    ok(stats['echo_req']>=18 and stats['ns']>=7,'host initiated echo, multicast/DAD NS, and neighbor traffic handled')
    ok(stats['bad_ip']>=2 and stats['wrong_dst']>=2,'live version/length and destination rejection')
    ok(stats['unsupported']>=2 and stats['checksum']>=1,'live unsupported protocol/code and bad checksum rejection')
    ok(stats['bad_nd']>=7,'live ND options, hop limit and invalid advertisement rejection')
    ok(len(s[4])==4 and stats['evicted']>=2,'neighbor cache stays bounded at four entries')
    s=cmd(b'U')
    s=until(lambda s:s[5] is not None)
    ok(s[5][0]=='nd-timeout' and s[7]['nd_timeout']==1,'unanswered ND terminates')
    ok(s[7]['nd_tx']==4,'exactly three total probes (initial plus two retries) for unresolved peer')
    s=until(lambda s:len(s[4])==0)
    ok(s[7]['expired']>=4,'learned neighbors age out under real clock')
    s=cmd(b'A')
    s=until(lambda s:s[5] is not None)
    ok(s[5][0]=='reply' and s[7]['na']==2,'aged peer is resolved again and echo succeeds')
    s=cmd(b'E')
    s=until(lambda s:s[5] is not None)
    ok(s[5][0]=='echo-timeout' and s[7]['echo_timeout']==1,'no echo reply has finite timeout')
    ok(s[7]['nd_tx']==5,'cached peer skips redundant ND')
    s=cmd(b'C')
    ok(s[0] and s[1]=='stopped' and not s[4],'controlled shutdown clears neighbor state')
    got,status=p.wait()
    ok(got==pid and status=='','exact service child exit status')
    ok(p.obj_info(rf)[14]==1,'last device close closes surviving RX FIFO')
    for h in (command,output,response,rf):p.obj_close(h)
    # Real revoke closes the RX queue while the child polls it.
    pid,command,output,response,rf=start()
    receive()
    p.obj_revoke(rf)
    s=receive()
    ok(s[0] and s[1]=='transport-error' and s[7]['transport_error']==1,'RX revoke stops service and releases device')
    got,status=p.wait()
    ok(got==pid and status=='','revoked service child exits cleanly')
    for h in (command,output,response,rf):p.obj_close(h)
    d=p.obj_device_open(cat,2,ctl,0)
    ok(p.obj_device_ether_info(d)[6]==0,'close/revoke permit a new physical NIC owner')
    for h in (d,cat,sh):p.obj_close(h)
    # Ordinary GE/O persistent graph plus Counter alias contract.
    shared.n += 1
    ok(alias is shared and shared.n==1,'T8-style parent graph/alias survives child execution')
    print('T14-RESULT',n[0],'PASS')
    return shared.n
