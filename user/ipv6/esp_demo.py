"""T15 disposable transport-mode ESP example; execute after node.py.
No persistent keys, lifecycle, replay window or security across restart.
Demo SAs are supplied by a TEST-ONLY fixture, never built into this module.
"""
import aesgcm


class EspNode(Node):
    def __init__(self, device, address, peer, tx_sa, rx_sa, api=None):
        # Each SA is (SPI, AES128 key, four-byte salt); no key-object service.
        for spi, key, salt in (tx_sa, rx_sa):
            if not 0 < spi <= 0xffffffff or len(key) != 16 or len(salt) != 4:
                raise ValueError('demo SA fields')
        if tx_sa[0] == rx_sa[0] or tx_sa[1:] == rx_sa[1:] or not linklocal(peer):
            raise ValueError('distinct directional demo SAs required')
        super().__init__(device, address, api)
        self.peer, self.tx_sa, self.rx_sa = peer, tx_sa, rx_sa
        self.sequence = 0
        for name in ('esp_tx', 'esp_rx', 'esp_bad', 'plain_drop'):
            self.stats[name] = 0

    def ping(self, peer, data=b'T15', identifier=0x1515, sequence=1):
        if peer != self.peer or len(data) > 1196:
            raise ValueError('demo peer / payload bound')
        return super().ping(peer, data, identifier, sequence)

    def _send(self, destination, mac, payload, hop=64):
        if payload[0] in (135, 136):
            return super()._send(destination, mac, payload, hop)
        if destination != self.peer or payload[0] not in (128, 129) or len(payload) > 1204:
            raise ValueError('only protected demo echo')
        if self.sequence == 0xffffffff:
            raise OSError('demo SA counter exhausted')
        self.sequence += 1
        spi, key, salt = self.tx_sa
        aad = spi.to_bytes(4, 'big')+self.sequence.to_bytes(4, 'big')
        iv = self.sequence.to_bytes(8, 'big')
        c = checksum(self.address, destination, payload)
        payload = payload[:2]+c.to_bytes(2, 'big')+payload[4:]
        pad = (-(len(payload)+2)) & 3
        trailer = bytes(range(1, pad+1))+bytes((pad, 58))
        esp = aad+iv+aesgcm.encrypt(key, salt+iv, aad, payload+trailer)
        ip = b'\x60\0\0\0'+len(esp).to_bytes(2, 'big')+bytes((50, hop))
        frame = mac+self.mac+b'\x86\xdd'+ip+self.address+destination+esp
        if len(frame) > self.maxframe:
            raise ValueError('demo ESP frame bound')
        if self.api.obj_device_send(self.device, frame) != len(frame):
            raise OSError('short Ethernet send')
        self.stats['tx'] += 1
        self.stats['esp_tx'] += 1

    def handle(self, frame):
        if len(frame) < 55 or frame[12:14] != b'\x86\xdd':
            return super().handle(frame)
        nh = frame[20]
        if nh == 58 and frame[54] not in (135, 136):
            # Protected echo must not take the inherited plaintext path.
            self.stats['plain_drop'] += 1
            return
        if nh != 50:
            return super().handle(frame)
        length = int.from_bytes(frame[18:20], 'big')
        if (len(frame) > self.maxframe or length < 36 or 54+length > len(frame)
                or frame[14] >> 4 != 6 or frame[21] == 0
                or frame[:6] != self.mac or not mac_ok(frame[6:12])
                or frame[22:38] != self.peer or frame[38:54] != self.address):
            return self._drop('esp_bad')
        esp = frame[54:54+length]
        spi, key, salt = self.rx_sa
        sequence = int.from_bytes(esp[4:8], 'big')
        if int.from_bytes(esp[:4], 'big') != spi or sequence == 0:
            return self._drop('esp_bad')
        try:
            plain = aesgcm.decrypt(key, salt+esp[8:16], esp[:8], esp[16:])
        except ValueError:
            return self._drop('esp_bad')
        pad, inner = plain[-2], plain[-1]
        if inner != 58 or pad > len(plain)-10 or plain[len(plain)-2-pad:-2] != bytes(range(1, pad+1)):
            return self._drop('esp_bad')
        body = plain[:len(plain)-pad-2]
        if len(body) > 1204 or body[0] not in (128, 129):
            return self._drop('esp_bad')
        self.stats['esp_rx'] += 1
        # Decapsulated bytes stay inside this user process. Reuse T14 checksum
        # and ICMP handling; TX dynamically dispatches back to protected _send.
        ip = frame[14:18]+len(body).to_bytes(2, 'big')+bytes((58, frame[21]))+frame[22:54]
        return super().handle(frame[:14]+ip+body)
