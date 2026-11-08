"""T14 minimal static IPv6 host in MicroPython, over the existing T13 ABI.

No filesystem, sockets, kernel protocols, routing, fragmentation or crypto.
Node owns the supplied process-local device Handle and its RX capabilities.
The caller supplies an already unique static link-local address (no SLAAC/DAD
address assignment). It must drive poll() regularly and explicitly close().
"""

ZERO = bytes(16)
ALL_NODES = b'\xff\x02' + bytes(13) + b'\x01'


def unicast(address):
    return len(address) == 16 and address != ZERO and address[0] != 255


def linklocal(address):
    return unicast(address) and address[:8] == b'\xfe\x80'+bytes(6)


def solicited(address):
    return b'\xff\x02' + bytes(9) + b'\x01\xff' + address[-3:]


def multicast_mac(address):
    return b'\x33\x33' + address[-4:]


def mac_ok(mac):
    return len(mac) == 6 and mac != bytes(6) and not mac[0] & 1


def checksum(source, destination, payload):
    # IPv6 pseudoheader: source, destination, u32 length, zeros, NH=58.
    data = source + destination + len(payload).to_bytes(4, 'big') + b'\0\0\0\x3a' + payload
    if len(data) & 1:
        data += b'\0'
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) | data[i+1]
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


class Node:
    def __init__(self, device, address, api=None, neighbor_limit=4,
                 neighbor_ms=3000, retry_ms=250, probe_limit=3, echo_ms=600):
        if api is None:
            import plan9 as api
        self.api = api
        self.device = device
        self.fifo = self.sub = None
        self.closed = False
        self.reason = ''
        self.neighbors = {}
        self.pending = None
        self.last_ping = None
        self.last_request = None
        self.now = 0
        self.stats = dict((k, 0) for k in (
            'rx', 'tx', 'bad_eth', 'bad_ip', 'wrong_dst', 'unsupported',
            'checksum', 'bad_nd', 'echo_req', 'echo_reply', 'ns', 'na',
            'learned', 'expired', 'evicted', 'nd_tx', 'nd_timeout',
            'echo_timeout', 'unmatched_reply', 'transport_error'))
        try:
            if not linklocal(address):
                raise ValueError('static link-local address required')
            if not (1 <= neighbor_limit <= 8 and 100 <= retry_ms <= 2000
                    and 1 <= probe_limit <= 3 and 1000 <= neighbor_ms <= 60000
                    and 100 <= echo_ms <= 5000):
                raise ValueError('bounded node limits required')
            self.address = bytes(address)
            self.group = solicited(address)
            self.limit = neighbor_limit
            self.neighbor_ms = neighbor_ms
            self.retry_ms, self.probe_limit, self.echo_ms = retry_ms, probe_limit, echo_ms
            info = api.obj_device_ether_info(device)
            self.mac, self.minframe = info[4], info[2]
            # IPv6 minimum MTU; no jumbo/fragmentation/extension support.
            self.maxframe = min(info[3], 1294)
            if not mac_ok(self.mac) or info[3] < 1294 or self.minframe > 60:
                raise ValueError('Ethernet MTU unsupported')
            self.fifo, _, family, _ = api.obj_device_recv(device)
            if family != 2:
                raise ValueError('Ethernet device required')
            self.sub = api.obj_subscribe(self.fifo, 12, 16)
        except Exception:
            self.close('open-failed')
            raise

    def close(self, reason='stopped'):
        if self.closed:
            return
        self.closed, self.reason = True, reason
        self.pending = None
        self.neighbors.clear()
        for name in ('sub', 'fifo', 'device'):
            h = getattr(self, name)
            setattr(self, name, None)
            if h is not None:
                try:
                    self.api.obj_close(h)
                except OSError:
                    pass

    def snapshot(self):
        return (self.closed, self.reason, self.address, self.mac,
                tuple(self.neighbors.items()), self.last_ping,
                self.last_request, self.stats.copy())

    def _time(self):
        self.now = max(self.now, self.api.time_ns() // 1000000)
        return self.now

    def _send(self, destination, mac, payload, hop=64):
        check = checksum(self.address, destination, payload)
        payload = payload[:2] + check.to_bytes(2, 'big') + payload[4:]
        ip = b'\x60\0\0\0' + len(payload).to_bytes(2, 'big') + bytes((58, hop))
        frame = mac + self.mac + b'\x86\xdd' + ip + self.address + destination + payload
        if len(frame) > self.maxframe:
            raise ValueError('IPv6 minimum MTU exceeded')
        if len(frame) < self.minframe:
            frame += bytes(self.minframe-len(frame))
        if self.api.obj_device_send(self.device, frame) != len(frame):
            raise OSError('short Ethernet send')
        self.stats['tx'] += 1

    def _learn(self, address, mac):
        if address not in self.neighbors and len(self.neighbors) >= self.limit:
            oldest = min(self.neighbors, key=lambda a: self.neighbors[a][1])
            del self.neighbors[oldest]
            self.stats['evicted'] += 1
        self.neighbors[address] = (mac, self.now+self.neighbor_ms)
        self.stats['learned'] += 1

    def _probe(self):
        p = self.pending
        destination = solicited(p[0])
        self._send(destination, multicast_mac(destination),
                   b'\x87\0\0\0'+bytes(4)+p[0]+b'\x01\x01'+self.mac, 255)
        p[4] += 1
        p[5] = self.now+self.retry_ms
        self.stats['nd_tx'] += 1

    def _echo(self):
        p = self.pending
        self._send(p[0], self.neighbors[p[0]][0], b'\x80\0\0\0'+p[1]+p[2])
        p[3], p[5] = 'echo', self.now+self.echo_ms

    def ping(self, peer, data=b'T14', identifier=0x1414, sequence=1):
        if self.closed:
            raise OSError('IPv6 node closed')
        if self.pending is not None:
            raise ValueError('one pending ping only')
        if not linklocal(peer) or peer == self.address:
            raise ValueError('on-link unicast peer required')
        if len(data) > 1232 or not (0 <= identifier <= 65535 and 0 <= sequence <= 65535):
            raise ValueError('bounded ping required')
        self._time()
        self.tick()
        tag = identifier.to_bytes(2, 'big')+sequence.to_bytes(2, 'big')
        self.pending = [bytes(peer), tag, bytes(data), 'nd', 0, self.now]
        self.last_ping = None
        try:
            if peer in self.neighbors:
                self._echo()
            else:
                self._probe()
        except OSError:
            self.stats['transport_error'] += 1
            self.close('transport-error')
            raise

    def tick(self):
        self._time()
        for address in tuple(self.neighbors):
            if self.neighbors[address][1] <= self.now:
                del self.neighbors[address]
                self.stats['expired'] += 1
        p = self.pending
        if p is None or self.now < p[5] or self.closed:
            return
        if p[3] == 'nd' and p[4] < self.probe_limit:
            self._probe()
        else:
            result = 'nd-timeout' if p[3] == 'nd' else 'echo-timeout'
            self.stats['nd_timeout' if p[3] == 'nd' else 'echo_timeout'] += 1
            self.last_ping = (result, p[0], p[1], p[2])
            self.pending = None

    def _drop(self, reason):
        self.stats[reason] += 1

    def handle(self, frame):
        # Bounded parser. Poll is the real NIC entry; direct calls are useful
        # only for parser tests and are not live-network evidence.
        self.stats['rx'] += 1
        if len(frame) < 54 or len(frame) > self.maxframe or frame[12:14] != b'\x86\xdd' or not mac_ok(frame[6:12]):
            return self._drop('bad_eth')
        ip = frame[14:54]
        length = (ip[4] << 8) | ip[5]
        source, destination = ip[8:24], ip[24:40]
        if ip[0] >> 4 != 6 or length < 4 or length > 1240 or 54+length > len(frame) or ip[7] == 0:
            return self._drop('bad_ip')
        if destination not in (self.address, self.group, ALL_NODES):
            return self._drop('wrong_dst')
        wantmac = multicast_mac(destination) if destination[0] == 255 else self.mac
        if frame[:6] != wantmac:
            return self._drop('wrong_dst')
        if ip[6] != 58:
            return self._drop('unsupported')
        body = frame[54:54+length]  # Ethernet padding is not IPv6 payload.
        if checksum(source, destination, body) != 0:
            return self._drop('checksum')
        typ, code = body[0], body[1]
        if code != 0:
            return self._drop('unsupported')
        if typ in (135, 136):
            return self._nd(frame[6:12], source, destination, ip[7], body)
        if not linklocal(source) or destination != self.address or len(body) < 8:
            return self._drop('bad_ip')
        if typ == 128:
            self.stats['echo_req'] += 1
            self.last_request = (source, body[4:8], body[8:])
            self._send(source, frame[6:12], b'\x81\0\0\0'+body[4:])
        elif typ == 129:
            p = self.pending
            if p is None or p[3] != 'echo' or source != p[0] or body[4:8] != p[1] or body[8:] != p[2]:
                return self._drop('unmatched_reply')
            self.stats['echo_reply'] += 1
            self.last_ping = ('reply', source, p[1], p[2])
            self.pending = None
        else:
            self._drop('unsupported')

    def _nd(self, mac, source, destination, hop, body):
        typ = body[0]
        if hop != 255 or len(body) < 24 or not unicast(body[8:24]):
            return self._drop('bad_nd')
        target = body[8:24]
        ll = None
        at = 24
        while at < len(body):
            if at+2 > len(body) or body[at+1] == 0:
                return self._drop('bad_nd')
            size = body[at+1]*8
            if at+size > len(body):
                return self._drop('bad_nd')
            kind = body[at]
            if kind in (1, 2):
                if kind != (1 if typ == 135 else 2) or size != 8 or ll is not None:
                    return self._drop('bad_nd')
                ll = body[at+2:at+8]
                if not mac_ok(ll) or ll != mac:
                    return self._drop('bad_nd')
            at += size  # unknown, well-sized options are safely skipped.
        if typ == 135:
            if body[4:8] != bytes(4) or target != self.address or destination not in (self.address, self.group):
                return self._drop('bad_nd')
            if source == ZERO:
                if ll is not None or destination != self.group:
                    return self._drop('bad_nd')
                replyto, replymac, flags = ALL_NODES, multicast_mac(ALL_NODES), b'\x20\0\0\0'
            else:
                if not linklocal(source) or (destination == self.group and ll is None):
                    return self._drop('bad_nd')
                if ll is not None:
                    self._learn(source, ll)
                replyto, replymac, flags = source, mac, b'\x60\0\0\0'
            self.stats['ns'] += 1
            self._send(replyto, replymac, b'\x88\0\0\0'+flags+self.address+b'\x02\x01'+self.mac, 255)
        else:
            flags = int.from_bytes(body[4:8], 'big')
            p = self.pending
            if (not linklocal(source) or source != target or destination != self.address or ll is None
                    or not flags & 0x40000000 or flags & 0x9fffffff
                    or p is None or p[3] != 'nd' or target != p[0]):
                return self._drop('bad_nd')
            self.stats['na'] += 1
            self._learn(target, ll)
            self._echo()

    def poll(self, timeout_ms=20):
        if self.closed:
            return False
        if not 0 <= timeout_ms <= 1000:
            raise ValueError('bounded poll timeout required')
        try:
            self.tick()
            try:
                h, envelope = self.api.obj_receive(self.sub, timeout_ms)
            except OSError as e:
                if str(e) not in ('vobj: receive timed out', 'vobj: receive would block'):
                    raise
                return True
            try:
                size = self.api.obj_info(h)[3]
                if size > self.maxframe:
                    self._drop('bad_eth')
                else:
                    self.handle(self.api.obj_read(h, size))
            finally:
                self.api.obj_close(h)
            self.tick()
        except OSError:
            self.stats['transport_error'] += 1
            self.close('transport-error')
        return not self.closed
