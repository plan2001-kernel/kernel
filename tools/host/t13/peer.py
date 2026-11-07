"""External raw L2 peer for QEMU socket netdev; no IP protocol involved."""
import os, socket, sys, time
port, target = map(int,sys.argv[1:3])
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
s.bind(('127.0.0.1',port))
peer=bytes.fromhex('020000001301')
while True:
    f,addr=s.recvfrom(65535)
    assert len(f)==60 and f[:6]==peer and f[6:12]==bytes.fromhex('525400123456') and f[12:14]==b'\x88\xb5',f.hex()
    print('PEER-RX',len(f),'88b5',f.hex(),flush=True)
    cmd=f[14:].rstrip(b'\0')
    assert f[14:]==cmd+bytes(46-len(cmd)),f.hex()
    def tx(body,size=60):
        out=f[6:12]+peer+b'\x88\xb5'+body+bytes(size-14-len(body))
        s.sendto(out,('127.0.0.1',target))
        if size==60: print('PEER-TX',len(out),'88b5',out.hex(),flush=True)
    if cmd==b'T13:ONE': tx(b'T13:RX:one\0\xff')
    elif cmd==b'T13:BURST':
        for i in range(8):tx(('T13:F%02d'%i).encode());time.sleep(.03)
    elif cmd==b'T13:FLOOD':
        for i in range(8192):tx(b'T13:FLOOD:'+i.to_bytes(4,'big'),1514)
        print('PEER-FLOOD 8192 x 1514',flush=True)
    elif cmd==b'T13:LINKDOWN':
        def link(state):
            mon=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
            mon.settimeout(3)
            with open(os.environ['T13_MONFILE']) as fp: mon.connect(fp.read())
            def prompt():
                answer=b''
                while b'(qemu)' not in answer: answer+=mon.recv(4096)
                return answer
            prompt()
            mon.sendall(('set_link net0 '+state+'\n').encode())
            answer=prompt()
            assert b'Error' not in answer and b'not found' not in answer,answer
            print('PEER-LINK',state,repr(answer),flush=True)
            mon.close()
        link('off')
        time.sleep(2)
        link('on')
        time.sleep(.2)
        tx(b'T13:LINKUP')
    else: raise AssertionError(cmd)
