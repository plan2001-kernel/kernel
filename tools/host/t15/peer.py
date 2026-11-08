"""Disposable external ESP peer; independent OpenSSL-backed AESGCM.
QEMU UDP socket carries raw Ethernet, never guest UDP or a crypto service.
"""
import json,runpy,select,socket,struct,sys
from pathlib import Path
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
cfg=runpy.run_path(str(Path(__file__).with_name('demo-config.py')))
G,P=cfg['LOCAL'],cfg['PEER'];TX,RX=cfg['PEER_TX'],cfg['GUEST_TX']
GM=bytes.fromhex('525400123456');PM=bytes.fromhex('020000001501')
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('127.0.0.1',int(sys.argv[1])));dest=('127.0.0.1',int(sys.argv[2]))
report=Path(sys.argv[3]);count=0;seq=0;seen=set();txivs=[];rxivs=[]
def ok(v,name):
    global count
    assert v,name
    count+=1;print('PEER-PASS',count,name,flush=True)
def checksum(src,dst,b):
    data=src+dst+struct.pack('!I3xB',len(b),58)+b
    data+=bytes(len(data)%2)
    n=sum(struct.unpack('!%dH'%(len(data)//2),data))
    while n>65535:n=(n&65535)+(n>>16)
    return 65535-n
def group(a):return bytes.fromhex('ff0200000000000000000001ff')+a[-3:]
def mm(a):return b'\x33\x33'+a[-4:]
def wrap(b,nh=50,ipdst=G,mac=GM,hop=64):
    f=mac+PM+b'\x86\xdd'+struct.pack('!IHBB',6<<28,len(b),nh,hop)+P+ipdst+b
    return f+bytes(max(0,60-len(f)))
def send(f,label):
    print('WIRE-TX',label,len(f),f.hex(),flush=True);s.sendto(f,dest)
def icmp(typ,data=b'',tag=b'\x15\x15\0\x01'):
    b=bytes((typ,0))+bytes(2)+tag+data
    return b[:2]+struct.pack('!H',checksum(P,G,b))+b[4:]
def seal(body):
    global seq
    seq+=1;spi,key,salt=TX;iv=seq.to_bytes(8,'big');aad=struct.pack('!II',spi,seq)
    txivs.append(iv.hex());pad=(-(len(body)+2))&3
    plain=body+bytes(range(1,pad+1))+bytes((pad,58))
    return wrap(aad+iv+AESGCM(key).encrypt(salt+iv,plain,aad))
def get(timeout=5):
    assert select.select([s],[],[],timeout)[0],'guest packet timeout'
    f,_=s.recvfrom(65535);print('WIRE-RX',len(f),f.hex(),flush=True)
    ok(60<=len(f)<=1294 and f[6:12]==GM and f[12:14]==b'\x86\xdd','real T13 whole Ethernet frame')
    v,n,nh,hop=struct.unpack('!IHBB',f[14:22]);a,b=f[22:38],f[38:54]
    ok(v>>28==6 and a==G and 54+n<=len(f),'IPv6 address and length')
    payload=f[54:54+n]
    if nh==58:
        ok(hop==255 and payload[0] in (135,136) and checksum(a,b,payload)==0,'only ordinary ND may be plaintext')
        return nh,b,payload
    ok(nh==50 and b==P and f[:6]==PM and hop==64,'protected traffic uses IPv6 NH50 transport')
    spi,sequence=struct.unpack('!II',payload[:8]);iv=payload[8:16]
    ok(spi==RX[0] and sequence>0 and iv==sequence.to_bytes(8,'big') and iv not in seen,'directional SPI and distinct in-test packet nonce')
    seen.add(iv);rxivs.append(iv.hex())
    plain=AESGCM(RX[1]).decrypt(RX[2]+iv,payload[16:],payload[:8])
    pad,inner=plain[-2:];body=plain[:len(plain)-pad-2]
    ok(inner==58 and plain[len(plain)-pad-2:-2]==bytes(range(1,pad+1)),'verified GCM tag and ESP padding/trailer')
    ok(checksum(a,b,body)==0 and body[0] in (128,129),'decrypted ICMPv6 checksum/type')
    ok(b'T15:' not in f,'intended echo bytes absent from ciphertext frame')
    return nh,b,body
# Resolve real guest-originated ND before its initial protected ping.
nh,dst,b=get(180);ok(nh==58 and b[0]==135 and b[8:24]==P and dst==group(P),'actual guest NS resolves peer')
na=b'\x88\0\0\0\x60\0\0\0'+P+b'\x02\x01'+PM
na=na[:2]+struct.pack('!H',checksum(P,G,na))+na[4:]
send(wrap(na,58,hop=255),'ordinary solicited NA')
nh,dst,b=get();ok(nh==50 and b[0]==128 and b[8:]==b'T15:guest','decrypted guest-initiated echo')
reply=icmp(129,b[8:],b[4:8]);send(seal(reply),'protected reply to guest')
# The opposite endpoint also performs real ND, outside the protected echo.
ndst=group(G);ns=b'\x87\0\0\0'+bytes(4)+G+b'\x01\x01'+PM
ns=ns[:2]+struct.pack('!H',checksum(P,ndst,ns))+ns[4:]
send(wrap(ns,58,ndst,mm(ndst),255),'ordinary peer NS')
nh,dst,b=get();ok(nh==58 and b[0]==136 and b[8:24]==G and b[24:]==b'\x02\x01'+GM,'actual guest NA resolves guest MAC')
def echo(data):
    request=icmp(128,data);send(seal(request),'protected peer echo')
    nh,dst,b=get();expected=bytes((129,0))+bytes(2)+request[4:]
    expected=expected[:2]+struct.pack('!H',checksum(G,P,expected))+expected[4:]
    ok(nh==50 and b==expected,'exact peer-initiated decrypted echo reply')
echo(b'T15:peer\0\xff')
bad=seal(icmp(128,b'T15:bad-tag'));bad=bad[:-1]+bytes((bad[-1]^1,))
send(bad,'negative corrupted tag');ok(not select.select([s],[],[],.15)[0],'invalid tag gets no echo')
send(wrap(icmp(128,b'NEGATIVE-PLAINTEXT'),58),'negative explicit plaintext probe, not protected exchange')
ok(not select.select([s],[],[],.15)[0],'no inherited plaintext echo fallback')
echo(b'T15:after-bad-tag');echo(b'T15:done')
ok(len(rxivs)==4 and len(set(txivs))==len(txivs)==5,'expected bidirectional exchanges / no SA nonce repeat within running test')
report.write_text(json.dumps({'result':'PASS','checks':count,'guest_esp_frames':4,'peer_esp_frames':5,'guest_ivs':rxivs,'peer_ivs':txivs,'protected_next_header':50,'plaintext_echo_responses':0,'limits':'PUBLIC fixed TEST-ONLY SAs; no security across restart/reuse, lifecycle or replay window'},indent=2)+'\n')
print('PEER-RESULT',count,'PASS',flush=True)
