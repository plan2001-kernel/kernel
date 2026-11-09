import plan9,space
ok=0
bad=0
def say(s):
    plan9.obj_write(ow,s)
def ck(n,v):
    global ok,bad
    if v:
        ok=ok+1
    else:
        bad=bad+1
    say(('T11-A '+n+' '+('PASS' if v else 'FAIL')+chr(10)).encode())

cc=chr(10).join(('import plan9','d,r=plan9.obj_list()','s=plan9.obj_subscribe(d,plan9.VRECV+plan9.VMETA,%d)','plan9.sleep(%d)','out=bytes()','try:','    for j in range(%d):','        m,e=plan9.obj_receive(s,2000)','        out=out+plan9.obj_read(m,64,0)+bytes((44,))','        plan9.obj_close(m)','except OSError:','    pass','q0,q1,q2,q3,q4,q5,q6,q7,q8,q9,q10,q11,q12,q13,q14,q15=plan9.obj_info(s)','b=out+str(q13).encode()','o=plan9.obj_create(b)','plan9.obj_publish(r,o)'))
say(b'M0'+chr(10).encode())
cid=0x4f45584543000003
f=plan9.obj_fifocreate(8)
rr=plan9.obj_fifocreate(8)
s=plan9.obj_subscribe(f,plan9.VRECV+plan9.VMETA,8)
sr=plan9.obj_subscribe(rr,plan9.VRECV+plan9.VMETA,8)
dh=plan9.VSEND+plan9.VSUBSCRIBE+plan9.VMETA+plan9.VDELEGATE
rh=plan9.VSEND+plan9.VMETA+plan9.VDELEGATE
say(b'M1'+chr(10).encode())
p1=space.spawn(cid,handle=sh,argv=('micropython','-c',cc%(2,1600,2)),vhandles=((f,dh),(rr,rh)))
p2=space.spawn(cid,handle=sh,argv=('micropython','-c',cc%(8,0,6)),vhandles=((f,dh),(rr,rh)))
say(b'M2'+chr(10).encode())
n=0
q2=0
while q2!=3 and n!=200:
    q0,q1,q2,q3,q4,q5,q6=plan9.obj_stat(f)
    n=n+1
    plan9.sleep(20)

say(b'M3 '+str(q2).encode()+chr(10).encode())
ck('tee-subs',q2==3)
got=b''
pfirst=()
for i in range(6):
    o=plan9.obj_create(('A%d'%i).encode())
    i0,i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15=plan9.obj_info(o)
    if i==0:
        pfirst=(i0,i1,i2)
    plan9.obj_publish(f,o,b'E%d'%i)
    plan9.obj_close(o)

gid=()
for i in range(6):
    m,e=plan9.obj_receive(s,500)
    got=got+plan9.obj_read(m,64,0)+b','
    j0,j1,j2,j3,j4,j5,j6,j7,j8,j9,j10,j11,j12,j13,j14,j15=plan9.obj_info(m)
    if i==0:
        gid=(j0,j1,j2)
    ck('env%d'%i,e==b'E%d'%i)
    plan9.obj_close(m)

say(b'M4'+chr(10).encode())
ck('parent-order',got==b'A0,A1,A2,A3,A4,A5,')
ck('identity',gid==pfirst)
m,e=plan9.obj_receive(sr,5000)
x=plan9.obj_read(m,128,0)
plan9.obj_close(m)
m,e=plan9.obj_receive(sr,5000)
y=plan9.obj_read(m,128,0)
plan9.obj_close(m)
if x.startswith(b'A0'):
    fast=x
    slow=y
else:
    fast=y
    slow=x

say(b'M5'+chr(10).encode())
say(b'FAST '+fast+chr(10).encode())
ck('fast',fast==b'A0,A1,A2,A3,A4,A5,0')
say(b'SLOW '+slow+chr(10).encode())
ck('slow',slow==b'A4,A5,4')

try:
    plan9.obj_receive(s,0)
    ck('empty',False)
except OSError:
    ck('empty',True)

try:
    plan9.obj_receive(s,80)
    ck('timeout',False)
except OSError:
    ck('timeout',True)

a,bs=space.wait()
c,ds=space.wait()
ck('waitpids',(a==p1 and c==p2) or (a==p2 and c==p1))
ck('waitok',bs=='' and ds=='')
say(b'M6'+chr(10).encode())

plan9.obj_close(sr)
plan9.obj_close(s)
plan9.obj_fifo_shutdown(f)
plan9.obj_close(f)
plan9.obj_close(rr)
say(('T11-A-RESULT '+('PASS' if bad==0 and not (ok==0) else 'FAIL')+' '+str(ok)+' '+str(bad)+chr(10)).encode())
