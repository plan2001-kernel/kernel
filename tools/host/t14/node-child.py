# Appended to the actual node source, delivered as an authorized bytes Handle
# to a separate ordinary MicroPython OSPAWN process. Local test control only.
import plan9 as p
hs = []
for h in p.obj_list():
    try:
        hs.append((h, p.obj_info(h)))
    except OSError:
        pass
d = next(h for h, i in hs if i[8] == 14)
control = next(h for h, i in hs if i[8] == 1 and i[10] & 32)
output = next(h for h, i in hs if i[8] == 1 and i[10] & 2)
cs = p.obj_subscribe(control, 12, 4)
local = b'\xfe\x80' + bytes(12) + b'\x01\x41'
peer = b'\xfe\x80' + bytes(12) + b'\x01\x42'
unknown = b'\xfe\x80' + bytes(12) + b'\xde\xad'
node = Node(d, local)

def answer():
    h = p.obj_create(repr(node.snapshot()).encode())
    try:
        p.obj_publish(output, h)
    finally:
        p.obj_close(h)

# These parser-only checks cover sizes the real T13 min-frame validation
# cannot deliver. They must never be counted as live NIC packet evidence.
node.handle(b'x'*13)
node.handle(bytes(12)+b'\x86\xdd'+bytes(39))
assert node.stats['bad_eth'] == 2
answer()
sequence = 0
try:
    while node.poll(20):
        try:
            h, _ = p.obj_receive(cs, 0)
        except OSError as e:
            assert str(e) == 'vobj: receive would block', str(e)
            continue
        try:
            command = p.obj_read(h, 1)
        finally:
            p.obj_close(h)
        sequence += 1
        if command in (b'P', b'A', b'U', b'E'):
            target = unknown if command == b'U' else peer
            payload = b'T14:TIMEOUT' if command == b'E' else b'T14:START' if command == b'P' else b'T14:AGAIN'
            node.ping(target, payload, sequence=sequence)
        elif command == b'C':
            node.close()
        else:
            assert command == b'S'
        answer()
    if node.reason != 'stopped':
        answer()
finally:
    node.close()
    p.obj_close(cs)
