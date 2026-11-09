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
    say(('T11-C '+n+' '+('PASS' if v else 'FAIL')+chr(10)).encode())

cid=0x4f45584543000003
rh=plan9.VSEND+plan9.VMETA+plan9.VDELEGATE
dh=plan9.VSUBSCRIBE+plan9.VMETA

gf=plan9.obj_fifocreate(4)
sg=plan9.obj_subscribe(gf,plan9.VRECV+plan9.VMETA,4)
gh,oid=plan9.obj_create_ge(sh,b'GE-PAYLOAD')
g0,g1,g2,g3,g4,g5,g6,g7,g8,g9,g10,g11,g12,g13,g14,g15=plan9.obj_info(gh)
ck('ge-create',g2==oid and g8==plan9.VKIND_GE)
rf=plan9.obj_fifocreate(4)
sr=plan9.obj_subscribe(rf,plan9.VRECV+plan9.VMETA,4)
cc=chr(10).join(('import plan9','d,r=plan9.obj_list()','s=plan9.obj_subscribe(d,plan9.VRECV+plan9.VMETA,2)','m,e=plan9.obj_receive(s,5000)','b=plan9.obj_read(m,64,0)','i0,i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15=plan9.obj_info(m)','o=plan9.obj_create(str(i2).encode()+bytes((58,))+b)','plan9.obj_publish(r,o)'))
p=space.spawn(cid,handle=sh,argv=('micropython','-c',cc),vhandles=((gf,dh),(rf,rh)))
t=0
q2=0
while q2!=2 and t!=200:
    q0,q1,q2,q3,q4,q5,q6=plan9.obj_stat(gf)
    t=t+1
    plan9.sleep(20)

ck('ge-sub',q2==2)
plan9.obj_publish(gf,gh)
m,e=plan9.obj_receive(sr,5000)
gr=plan9.obj_read(m,128,0)
plan9.obj_close(m)
ck('ge-interproc',gr==(str(oid).encode()+bytes((58,))+b'GE-PAYLOAD'))
a,bs=space.wait()
ck('ge-wait',bs=='')
plan9.obj_close(sg)
plan9.obj_fifo_shutdown(gf)
plan9.obj_close(gf)
plan9.obj_close(gh)

sec=plan9.obj_create(b'PARENT-SECRET')
z0,z1,soid,z3,z4,z5,z6,z7,z8,z9,z10,z11,z12,z13,z14,z15=plan9.obj_info(sec)
ctl=plan9.obj_create(b'AUTHORIZED-CTRL')
cf=plan9.obj_fifocreate(2)
sr2=plan9.obj_subscribe(cf,plan9.VRECV+plan9.VMETA,4)
ce=chr(10).join(('import plan9','hs=plan9.obj_list()','ctlh=0','rh=0','for h in hs:','    y0,y1,y2,y3,y4,y5,y6,y7,y8,y9,y10,y11,y12,y13,y14,y15=plan9.obj_info(h)','    if y8==plan9.VKIND_BYTES:','        ctlh=h','    if y8==plan9.VKIND_FIFO:','        rh=h','ctlok=0','try:','    if plan9.obj_read(ctlh,32,0)==bytes((65,85,84,72,79,82,73,90,69,68,45,67,84,82,76)):','        ctlok=1','except OSError:','    ctlok=0','ambok=0','try:','    plan9.obj_read('+str(sec)+',32,0)','    ambok=0','except OSError as e:','    if str(e).encode()==bytes((98,97,100,32,97,114,103,32,105,110,32,115,121,115,116,101,109,32,99,97,108,108)):','        ambok=1','oidfound=0','for h in hs:','    y0,y1,hoid,y3,y4,y5,y6,y7,y8,y9,y10,y11,y12,y13,y14,y15=plan9.obj_info(h)','    if hoid==' + str(soid) + ':','        oidfound=1','o=plan9.obj_create(str(ctlok).encode())','plan9.obj_publish(rh,o)','o=plan9.obj_create(str(ambok).encode())','plan9.obj_publish(rh,o)','o=plan9.obj_create(str(oidfound).encode())','plan9.obj_publish(rh,o)'))
pe=space.spawn(cid,handle=sh,argv=('micropython','-c',ce),vhandles=((ctl,plan9.VREAD+plan9.VMETA),(cf,rh)))
m,e=plan9.obj_receive(sr2,5000)
cres=plan9.obj_read(m,16,0)
plan9.obj_close(m)
m,e=plan9.obj_receive(sr2,5000)
ares=plan9.obj_read(m,16,0)
plan9.obj_close(m)
m,e=plan9.obj_receive(sr2,5000)
ores=plan9.obj_read(m,16,0)
plan9.obj_close(m)
say(('T11-C ambient-result '+cres.decode()+ares.decode()+ores.decode()+chr(10)).encode())
ck('ambient-control',cres==b'1')
ck('ambient-denied',ares==b'1')
ck('ambient-oid-unreachable',ores==b'0')
ck('no-ambient-handle',cres==b'1' and ares==b'1' and ores==b'0')
a,bs=space.wait()
ck('ambient-wait',a==pe and bs=='')
plan9.obj_close(ctl)
plan9.obj_close(sec)
plan9.obj_close(sr2)
plan9.obj_close(cf)

base=plan9.obj_create(b'BASE')
hs=()
n=0
try:
    while 1:
        hh=plan9.obj_create(b'H')
        hs=hs+(hh,)
        n=n+1
except OSError as e:
    say(('T11-C handle-limit '+str(n)+' '+str(e)+chr(10)).encode())

ck('handle-bound',n!=0)
for h in hs:
    plan9.obj_close(h)

plan9.obj_close(base)
hh=plan9.obj_create(b'AFTER')
ck('handle-recover',hh!=0)
plan9.obj_close(hh)

qf=plan9.obj_fifocreate(1024)
crf=plan9.obj_fifocreate(1)
cq=chr(10).join(('import plan9','d,c=plan9.obj_list()','ss=()','for i in range(7):','    ss=ss+(plan9.obj_subscribe(d,plan9.VRECV+plan9.VMETA,1024),)','cs=plan9.obj_subscribe(c,plan9.VRECV+plan9.VMETA,1)','plan9.obj_receive(cs)'))
kids=()
for i in range(8):
    kp=space.spawn(cid,handle=sh,argv=('micropython','-c',cq),vhandles=((qf,dh),(crf,dh)))
    kids=kids+(kp,)
wit=()
for i in range(8):
    wit=wit+(plan9.obj_subscribe(qf,plan9.VRECV+plan9.VMETA,1024),)

t=0
q2=0
while q2!=64 and t!=600:
    q0,q1,q2,q3,q4,q5,q6=plan9.obj_stat(qf)
    t=t+1
    plan9.sleep(20)

ck('quota-subs',q2==64)
qo=plan9.obj_create(b'Q')
for i in range(1024):
    plan9.obj_publish(qf,qo)
b0,b1,b2,b3,b4,b5,b6=plan9.obj_stat(qf)
wb=()
for w in wit:
    z0,z1,z2,z3,z4,z5,z6,z7,z8,z9,z10,wcap,wq,wd,z14,z15=plan9.obj_info(w)
    wb=wb+((wcap,wq,wd),)
ck('quota-sends',b0==1024)
qerr=b''
try:
    plan9.obj_publish(qf,qo)
except OSError as e:
    qerr=str(e).encode()
a0,a1,a2,a3,a4,a5,a6=plan9.obj_stat(qf)
wa=()
for w in wit:
    z0,z1,z2,z3,z4,z5,z6,z7,z8,z9,z10,wcap,wq,wd,z14,z15=plan9.obj_info(w)
    wa=wa+((wcap,wq,wd),)
say(('T11-C quota-error '+qerr.decode()+chr(10)).encode())
ck('quota-global-limit',qerr==b'kernel allocate failed')
ck('quota-no-partial-send',a0==b0)
ck('quota-no-partial-drop',a1==b1)
ck('quota-witness-count',len(wit)==8 and len(wa)==8)
wfull=1
for w in wa:
    wcap,wq,wd=w
    if not (wcap==1024 and wq==1024 and wd==0):
        wfull=0
ck('quota-witness-full',wfull==1)
ck('quota-queues-unchanged',wb==wa)

plan9.obj_close(qo)
plan9.obj_fifo_shutdown(qf)
for w in wit:
    plan9.obj_close(w)
rel=plan9.obj_create(b'REL')
plan9.obj_publish(crf,rel)
plan9.obj_close(rel)
qok=1
for kp in kids:
    a,bs=space.wait()
    if not (bs==''):
        qok=0

ck('quota-children',qok==1)
plan9.obj_close(qf)
plan9.obj_close(crf)
plan9.obj_close(sr)
plan9.obj_close(rf)
say(('T11-C-RESULT '+('PASS' if bad==0 and not (ok==0) else 'FAIL')+' '+str(ok)+' '+str(bad)+chr(10)).encode())
