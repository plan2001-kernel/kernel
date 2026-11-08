import plan9 as p
hs=[]
for h in p.obj_list():
    try:hs.append((h,p.obj_info(h)))
    except OSError:pass
d=next(h for h,i in hs if i[8]==14)
ctl=next(h for h,i in hs if i[8]==1 and i[10]&32)
out=next(h for h,i in hs if i[8]==1 and i[10]&2)
sub=p.obj_subscribe(ctl,12,4)
# Tiny in-process primitive checks, not network evidence.
tag=b'\x58\xe2\xfc\xce\xfa\x7e\x30\x61\x36\x7f\x1d\x57\xa4\xe7\x45\x5a'
assert aesgcm.encrypt(bytes(16),bytes(12),b'',b'')==tag
sealed=b'\x03\x88\xda\xce\x60\xb6\xa3\x92\xf3\x28\xc2\xb9\x71\xb2\xfe\x78\xab\x6e\x47\xd4\x2c\xec\x13\xbd\xf5\x3a\x67\xb2\x12\x57\xbd\xdf'
assert aesgcm.encrypt(bytes(16),bytes(12),b'',bytes(16))==sealed
assert aesgcm.decrypt(bytes(16),bytes(12),b'',sealed)==bytes(16)
try:aesgcm.decrypt(bytes(16),bytes(12),b'',sealed[:-1]+b'\0')
except ValueError:pass
else:raise AssertionError('invalid tag accepted')
node=EspNode(d,LOCAL,PEER,GUEST_TX,PEER_TX)
def answer():
    h=p.obj_create(repr(node.snapshot()).encode())
    p.obj_publish(out,h);p.obj_close(h)
answer()
try:
    while node.poll(20):
        try:h,_=p.obj_receive(sub,0)
        except OSError as e:
            assert str(e)=='vobj: receive would block'
            continue
        c=p.obj_read(h,1);p.obj_close(h)
        if c==b'P':node.ping(PEER,b'T15:guest')
        elif c==b'C':node.close()
        else:assert c==b'S'
        answer()
finally:
    node.close();p.obj_close(sub)
