"""Independent external wire peer. Does not import or run the guest node.
UDP is only QEMU socket-netdev encapsulation; guest wire traffic is IPv6/ICMPv6.
"""
import json, select, socket, struct, sys, time
from pathlib import Path
PORT, DEST = map(int,sys.argv[1:3])
REPORT=Path(sys.argv[3])
SERIAL=Path(sys.argv[4])
G=bytes.fromhex('fe800000000000000000000000000141')
P=bytes.fromhex('fe800000000000000000000000000142')
GM=bytes.fromhex('525400123456'); PM=bytes.fromhex('020000001401')
ALL=bytes.fromhex('ff020000000000000000000000000001')
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',PORT))
checks=0; events=[]; seq=0; unknown_probes=0

def ok(v, name):
    global checks
    assert v,name
    checks+=1
    print('PEER-PASS',checks,name,flush=True)

def csum(src,dst,data):
    block=src+dst+struct.pack('!I3xB',len(data),58)+data
    block+=b'\0'*(len(block)%2)
    value=sum(struct.unpack('!%dH'%(len(block)//2),block))
    while value>65535:value=(value&65535)+(value>>16)
    return 65535-value

def group(ip):return bytes.fromhex('ff0200000000000000000001ff')+ip[-3:]
def mm(ip):return b'\x33\x33'+ip[-4:]

def frame(body,src=P,dst=G,mac=GM,hop=64,nh=58,version=6,padding=0):
    body=body[:2]+struct.pack('!H',csum(src,dst,body))+body[4:]
    ip=struct.pack('!IHBB',version<<28,len(body),nh,hop)+src+dst
    f=mac+PM+b'\x86\xdd'+ip+body+bytes(padding)
    return f+bytes(max(0,60-len(f)))

def send(f,label):
    print('WIRE-TX',label,len(f),f.hex(),flush=True)
    sock.sendto(f,('127.0.0.1',DEST))

def get(timeout=5):
    if not select.select([sock],[],[],timeout)[0]:raise TimeoutError('guest response')
    f,_=sock.recvfrom(65535)
    print('WIRE-RX',len(f),f.hex(),flush=True)
    ok(60<=len(f)<=1294 and f[6:12]==GM and f[12:14]==b'\x86\xdd','real NIC whole Ethernet / EtherType86dd')
    v,l,nh,hop=struct.unpack('!IHBB',f[14:22]);src=f[22:38];dst=f[38:54]
    ok(v>>28==6 and nh==58 and l>=4 and 54+l<=len(f),'IPv6 NH58 / bounded exact payload length')
    body=f[54:54+l]
    ok(src==G and csum(src,dst,body)==0,'configured guest source / valid ICMPv6 pseudoheader checksum')
    ok(f[:6]==(mm(dst) if dst[0]==255 else PM),'correct Ethernet destination mapping')
    return f,dst,hop,body

def echo(label,padding=0):
    global seq
    seq+=1;body=struct.pack('!BBHHH',128,0,0,0x4242,seq)+label
    send(frame(body,padding=padding),'echo:'+repr(label))
    f,dst,hop,b=get()
    ok(dst==P and hop==64 and b==b'\x81\0'+struct.pack('!H',csum(G,P,b'\x81\0\0\0'+body[4:]))+body[4:] and len(f)==54+len(body),'exact full Echo Reply including id/sequence/data')

def ns(src=P,options=None,hop=255,target=G):
    if options is None:options=b'\x01\x01'+PM
    return frame(b'\x87\0\0\0'+bytes(4)+target+options,src=src,dst=group(G),mac=mm(group(G)),hop=hop)

def expect_na(dad=False):
    f,dst,hop,b=get()
    flags=b'\x20\0\0\0' if dad else b'\x60\0\0\0'
    target=ALL if dad else P
    want=b'\x88\0\0\0'+flags+G+b'\x02\x01'+GM
    want=want[:2]+struct.pack('!H',csum(G,target,want))+want[4:]
    ok(dst==target and hop==255 and b==want and len(f)==86,'exact solicited/DAD NA, flags, hop255 and TLLA')

def silence(label,f):
    send(f,label)
    ok(not select.select([sock],[],[],.08)[0],'no response to '+label)
    events.append(label)
    echo(('barrier:'+label).encode())

def campaign():
    send(ns(),'multicast NS for guest');expect_na()
    echo(b'HOST:echo\x00\xff')
    echo(b'padding',padding=17)
    send(ns(options=b'\x01\x01'+PM+b'\x1f\x01'+bytes(6)),'NS with bounded unknown option');expect_na()
    send(ns(src=bytes(16),options=b''),'DAD NS without SLLA');expect_na(True)
    body=struct.pack('!BBHHH',128,0,0,0x3333,1)+b'bad'
    good=frame(body)
    cases=[
        ('bad-version',frame(body,version=4)),
        ('truncated-payload',good[:18]+b'\x04\x00'+good[20:]),
        ('wrong-ip-destination',frame(body,dst=P)),
        ('wrong-ethernet-destination',frame(body,mac=PM)),
        ('unsupported-next-header',frame(body,nh=17)),
        ('bad-checksum',good[:-1]+bytes((good[-1]^1,))),
        ('invalid-icmp-code',frame(bytes((128,1))+body[2:])),
        ('unspecified-echo-source',frame(body,src=bytes(16))),
        ('loopback-echo-source',frame(body,src=bytes(15)+b'\x01')),
        ('multicast-echo-source',frame(body,src=ALL)),
        ('multicast-unicast-mac-mismatch',frame(body,mac=mm(ALL))),
        ('nd-hop254',ns(hop=254)),
        ('nd-zero-option',ns(options=b'\x01\0'+PM)),
        ('nd-option-overrun',ns(options=b'\x01\x02'+PM)),
        ('nd-duplicate-option',ns(options=(b'\x01\x01'+PM)*2)),
        ('nd-wrong-option-mac',ns(options=b'\x01\x01'+GM)),
        ('nd-multicast-target',ns(target=ALL)),
        ('nd-dad-with-slla',ns(src=bytes(16))),
        ('nd-missing-slla',ns(options=b'')),
        ('nd-nonzero-reserved',frame(b'\x87\0\0\0'+b'\0\0\0\x01'+G+b'\x01\x01'+PM,dst=group(G),mac=mm(group(G)),hop=255)),
        ('unsolicited-na',frame(b'\x88\0\0\0\x60\0\0\0'+P+b'\x02\x01'+PM,hop=255)),
        ('oversized-ipv6',frame(b'\x80\0\0\0'+bytes(4)+b'Z'*1233)),
        ('short-ethernet-driver-boundary',good[:13]),
        ('short-ipv6-driver-boundary',good[:50]),
    ]
    for label,f in cases:silence(label,f)
    # Valid on-link neighbors force deterministic eviction at capacity four.
    for i in range(5):
        address=P[:-2]+bytes((1,0x50+i))
        send(ns(src=address),'bounded neighbor '+str(i))
        f,dst,hop,b=get()
        ok(dst==address and hop==255 and b[0]==136 and b[8:24]==G,'valid neighbor NS/NA exchange')
    echo(b'T14:PEER-DONE')
    REPORT.write_text(json.dumps({'result':'PASS','checks':checks,'negative_cases':events,'wire':{'ethertype':'86dd','next_header':58,'guest':G.hex(),'peer':P.hex()},'limits':'short L2/IP frames rejected at driver/T13 boundary, not claimed delivered to parser'},indent=2)+'\n')
    print('PEER-CAMPAIGN PASS',checks,flush=True)

started=False
while True:
    f,dst,hop,b=get(180)
    if b[0]==135:
        target=b[8:24]
        ok(hop==255 and dst==group(target) and b[24:]==b'\x01\x01'+GM,'guest multicast NS hop255 / target mapping / SLLA')
        if target==P:
            send(frame(b'\x88\0\0\0\x60\0\0\0'+P+b'\x02\x01'+PM,hop=255),'peer solicited NA')
        else:
            ok(target==P[:-2]+b'\xde\xad','expected unresolved target')
            unknown_probes+=1
    elif b[0]==128:
        ok(dst==P and hop==64 and b[1]==0,'guest initiated Echo Request')
        if b[8:]==b'T14:TIMEOUT':
            print('PEER intentional Echo timeout',flush=True)
            # Synchronize only teardown timing with the completed guest
            # fixture; packet handling still uses the real NIC exclusively.
            deadline=time.monotonic()+15
            while b'T14-RESULT 24 PASS' not in SERIAL.read_bytes():
                assert time.monotonic()<deadline,'guest teardown not completed'
                time.sleep(.02)
            send(frame(b'\x80\0\0\0\x42\x42\xff\xffAFTER-CLOSE'),'probe after all node owners closed')
            ok(not select.select([sock],[],[],.3)[0],'no competing native/other responder after child close')
            r=json.loads(REPORT.read_text());r['unknown_probes']=unknown_probes;r['final_checks']=checks
            r['post_close_silence']=True
            REPORT.write_text(json.dumps(r,indent=2)+'\n')
            continue
        reply=b'\x81\0\0\0'+b[4:]
        send(frame(reply),'peer Echo Reply')
        if not started:
            ok(b[8:]==b'T14:START','actual guest startup ping payload')
            started=True;campaign()
        else:
            ok(b[8:]==b'T14:AGAIN','guest re-resolved ping payload')
    else:raise AssertionError('unexpected guest message outside campaign: '+repr(b))
