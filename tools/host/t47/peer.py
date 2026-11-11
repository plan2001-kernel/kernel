"""External raw L2 peer for the QEMU socket netdev; no IP stack involved.

Every received frame must be a complete Ethernet frame from the guest NIC to
this peer, with one of the EtherTypes exercised by T47.  The peer echoes each
frame back with the source/destination MACs swapped and the EtherType and
payload preserved, which lets the guest check the full L2 bytes on RX.
"""
import socket, sys

port, target = map(int, sys.argv[1:3])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('127.0.0.1', port))
peer = bytes.fromhex('020000001301')
guest = bytes.fromhex('525400123456')
etypes = (b'\x08\x00', b'\x86\xdd', b'\x08\x06', b'\x88\xb5')

while True:
    f, addr = s.recvfrom(65535)
    etype = f[12:14] if len(f) >= 14 else b''
    assert 60 <= len(f) <= 1514, len(f)
    assert f[:6] == peer, f.hex()
    assert f[6:12] == guest, f.hex()
    assert etype in etypes, f.hex()
    print('PEER-RX', len(f), etype.hex(), f.hex(), flush=True)
    out = f[6:12] + peer + f[12:]
    s.sendto(out, ('127.0.0.1', target))
    print('PEER-TX', len(out), etype.hex(), out.hex(), flush=True)
