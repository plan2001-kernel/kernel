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
    say(('T11-B '+n+' '+('PASS' if v else 'FAIL')+chr(10)).encode())

def tries(n,fn):
    try:
        fn()
    except OSError as e:
        ck(n,True)
        return
    ck(n,False)

cid=0x4f45584543000003
f=plan9.obj_fifocreate(4)
s=plan9.obj_subscribe(f,plan9.VRECV+plan9.VMETA,4)
o=plan9.obj_create(b'AUTH')
sendh=plan9.obj_open_handle(f,plan9.VSEND)
plan9.obj_publish(sendh,o)
m,e=plan9.obj_receive(s,500)
ck('send-vsend',plan9.obj_read(m,16,0)==b'AUTH')
plan9.obj_close(m)
meta=plan9.obj_open_handle(f,plan9.VMETA)
try:
    plan9.obj_publish(meta,o)
    ck('need-vsend',False)
except OSError:
    ck('need-vsend',True)

roo=plan9.obj_open_handle(o,plan9.VREAD)
try:
    plan9.obj_publish(f,roo)
    ck('need-vdelegate',False)
except OSError:
    ck('need-vdelegate',True)

g=plan9.obj_grant(f,plan9.VSEND+plan9.VMETA)
a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15=plan9.obj_info(g)
ck('grant-rights',a10==(plan9.VSEND+plan9.VMETA))
try:
    plan9.obj_grant(g,plan9.VSEND+plan9.VCONTROL)
    ck('grant-attenuate',False)
except OSError:
    ck('grant-attenuate',True)

o2=plan9.obj_create(b'DELEG')
od=plan9.obj_open_handle(o2,plan9.VREAD+plan9.VMETA+plan9.VDELEGATE)
sd=plan9.obj_subscribe(f,plan9.VRECV+plan9.VMETA+plan9.VDELEGATE,4)
plan9.obj_publish(f,od)
m,e=plan9.obj_receive(sd,500)
b0,b1,b2,b3,b4,b5,b6,b7,b8,b9,b10,b11,b12,b13,b14,b15=plan9.obj_info(m)
ck('recv-delegate',(b10 & plan9.VDELEGATE)==plan9.VDELEGATE)
h2=plan9.obj_grant(m,plan9.VREAD)
ck('recv-attenuate',h2!=0)
try:
    plan9.obj_grant(m,plan9.VWRITE)
    ck('recv-no-add',False)
except OSError:
    ck('recv-no-add',True)
plan9.obj_close(h2)
plan9.obj_close(m)

plan9.obj_publish(f,od)
m,e=plan9.obj_receive(s,500)
c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15=plan9.obj_info(m)
ck('default-no-delegate',(c10 & plan9.VDELEGATE)==0)
try:
    plan9.obj_grant(m,plan9.VREAD)
    ck('default-grant-deny',False)
except OSError:
    ck('default-grant-deny',True)
plan9.obj_close(m)

f2=plan9.obj_fifocreate(2)
try:
    plan9.obj_publish(f2,f)
    ck('no-fifo-ref',False)
except OSError:
    ck('no-fifo-ref',True)
try:
    plan9.obj_publish(f2,f2)
    ck('no-self-ref',False)
except OSError:
    ck('no-self-ref',True)

try:
    plan9.obj_publish(f,o,b'x'*65)
    ck('env-limit',False)
except OSError:
    ck('env-limit',True)

tmp=plan9.obj_create(b'TMP')
plan9.obj_close(tmp)
try:
    plan9.obj_read(tmp,1,0)
    ck('stale-handle',False)
except OSError:
    ck('stale-handle',True)
try:
    plan9.obj_close(tmp)
    ck('double-close',False)
except OSError:
    ck('double-close',True)

s2=plan9.obj_subscribe(f,plan9.VRECV+plan9.VMETA,2)
plan9.obj_close(s2)
try:
    plan9.obj_receive(s2,0)
    ck('closed-sub',False)
except OSError:
    ck('closed-sub',True)

try:
    plan9.obj_fifocreate(-1)
    ck('bad-cap',False)
except OSError:
    ck('bad-cap',True)
try:
    plan9.obj_subscribe(f,plan9.VRECV+plan9.VMETA,1025)
    ck('bad-subcap',False)
except OSError:
    ck('bad-subcap',True)

try:
    plan9.obj_subscribe(f,plan9.VRECV+plan9.VCONTROL,1)
    ck('no-sub-control',False)
except OSError:
    ck('no-sub-control',True)
try:
    plan9.obj_subscribe(f,plan9.VRECV+plan9.VSUBSCRIBE,1)
    ck('no-sub-subscribe',False)
except OSError:
    ck('no-sub-subscribe',True)

att=plan9.obj_open_handle(f,plan9.VSUBSCRIBE+plan9.VMETA)
try:
    plan9.obj_subscribe(att,plan9.VRECV+plan9.VSEND,1)
    ck('no-sub-send-lift',False)
except OSError:
    ck('no-sub-send-lift',True)
try:
    plan9.obj_grant(s,plan9.VREAD)
    ck('no-grant-sub',False)
except OSError:
    ck('no-grant-sub',True)

fp=plan9.obj_fifocreate(2)
sp=plan9.obj_subscribe(fp,plan9.VRECV+plan9.VMETA,4)
op=plan9.obj_create(b'PEND')
try:
    plan9.obj_publish(fp,op)
    plan9.obj_close(op)
    m,e=plan9.obj_receive(sp,500)
    ck('pending-sender-close',plan9.obj_read(m,16,0)==b'PEND')
    plan9.obj_close(m)
except OSError as e:
    say(('T11-B pending-err '+str(e)+chr(10)).encode())
    ck('pending-sender-close',False)
plan9.obj_close(sp)
plan9.obj_close(fp)

fc=plan9.obj_fifocreate(4)
rf=plan9.obj_fifocreate(4)
sr=plan9.obj_subscribe(rf,plan9.VRECV+plan9.VMETA,4)
cc=chr(10).join(('import plan9','d,r=plan9.obj_list()','s=plan9.obj_subscribe(d,plan9.VRECV+plan9.VMETA,2)','o=plan9.obj_create(bytes((82,69,65,68,89)))','plan9.obj_publish(r,o)','b=bytes((78,79,69,82,82,79,82))','try:','    plan9.obj_receive(s)','except OSError as e:','    b=str(e).encode()','o=plan9.obj_create(b)','plan9.obj_publish(r,o)'))
dh=plan9.VSUBSCRIBE+plan9.VMETA
rh=plan9.VSEND+plan9.VMETA+plan9.VDELEGATE
p=space.spawn(cid,handle=sh,argv=('micropython','-c',cc),vhandles=((fc,dh),(rf,rh)))
m,e=plan9.obj_receive(sr,3000)
ck('blocked-ready',plan9.obj_read(m,16,0)==b'READY')
plan9.obj_close(m)
early=b'OUTCOME-BEFORE-REVOKE'
try:
    m,e=plan9.obj_receive(sr,300)
    early=plan9.obj_read(m,64,0)
    plan9.obj_close(m)
except OSError as e:
    early=str(e).encode()
ck('blocked-settle',early==b'vobj: receive timed out')
plan9.obj_fifo_shutdown(fc)
m,e=plan9.obj_receive(sr,3000)
rmsg=plan9.obj_read(m,64,0)
plan9.obj_close(m)
say(('T11-B revoke-err '+rmsg.decode()+chr(10)).encode())
ck('revoke-wakes',rmsg==b'i/o on hungup channel')
a,bs=space.wait()
ck('revoke-wait',a==p and bs=='')

fe=plan9.obj_fifocreate(4)
re=plan9.obj_fifocreate(4)
sr2=plan9.obj_subscribe(re,plan9.VRECV+plan9.VMETA,4)
cc2=chr(10).join(('import plan9','d,r=plan9.obj_list()','s=plan9.obj_subscribe(d,plan9.VRECV+plan9.VMETA,2)','o=plan9.obj_create(bytes((82,69,65,68,89)))','plan9.obj_publish(r,o)','plan9.obj_receive(s)'))
p2=space.spawn(cid,handle=sh,argv=('micropython','-c',cc2),vhandles=((fe,dh),(re,rh)))
m,e=plan9.obj_receive(sr2,3000)
ck('exit-ready',plan9.obj_read(m,16,0)==b'READY')
plan9.obj_close(m)
q0,q1,q2,q3,q4,q5,q6=plan9.obj_stat(fe)
ck('exit-subscribed',q2==1)
space.terminate(p2)
a,bs=space.wait()
say(('T11-B exit-status '+bs+chr(10)).encode())
ck('exit-pid',a==p2)
ck('exit-status','terminated' in bs)
ck('exit-reaped',a==p2 and 'terminated' in bs)
q0,q1,q2,q3,q4,q5,q6=plan9.obj_stat(fe)
ck('exit-teardown',q2==0)

fq=plan9.obj_fifocreate(4)
sq=plan9.obj_subscribe(fq,plan9.VRECV+plan9.VMETA,4)
oq=plan9.obj_create(b'Q')
for i in range(3):
    plan9.obj_publish(fq,oq)

plan9.obj_close(oq)
plan9.obj_close(sq)
q0,q1,q2,q3,q4,q5,q6=plan9.obj_stat(fq)
ck('sub-close-teardown',q2==0)

for i in range(50):
    fr=plan9.obj_fifocreate(2)
    sr3=plan9.obj_subscribe(fr,plan9.VRECV+plan9.VMETA,2)
    orr=plan9.obj_create(b'R')
    plan9.obj_publish(fr,orr)
    plan9.obj_close(orr)
    plan9.obj_close(sr3)
    plan9.obj_fifo_shutdown(fr)
    plan9.obj_close(fr)

ck('repeat-50',True)

plan9.obj_close(att)
plan9.obj_close(f2)
plan9.obj_close(sd)
plan9.obj_close(od)
plan9.obj_close(o2)
plan9.obj_close(g)
plan9.obj_close(meta)
plan9.obj_close(roo)
plan9.obj_close(sendh)
plan9.obj_close(s)
plan9.obj_fifo_shutdown(f)
plan9.obj_close(f)
plan9.obj_close(sr)
plan9.obj_close(rf)
plan9.obj_close(sr2)
plan9.obj_close(re)
plan9.obj_close(fq)
say(('T11-B-RESULT '+('PASS' if bad==0 and not (ok==0) else 'FAIL')+' '+str(ok)+' '+str(bad)+chr(10)).encode())
