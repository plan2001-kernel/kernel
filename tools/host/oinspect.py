#!/usr/bin/env python3
"""Inspect an at-rest O v1 image without running GE or Python payload code.

Input is opened rb; no replay, mount, repair or guest boot. Checks superblocks,
arena copies, log payload hashes and the snapshot tree. Buffered O insert/delete
messages are applied only to a host-memory dictionary. Other message operations
are deliberately unsupported. Optionally extract published objects to a NEW
directory. This is a bounded forensic reader, not a general GE checker.
"""
import argparse
import hashlib
import json
import pathlib
import runpy
import struct

ge = runpy.run_path(str(pathlib.Path(__file__).with_name('gefslog')))
BS = ge['Blksz']
NIL = (1 << 64) - 1


def require(ok, message):
    if not ok:
        raise ValueError(message)


def uint(b, p, n=8):
    require(0 <= p <= len(b) - n, 'truncated integer')
    return int.from_bytes(b[p:p+n], 'big')


class GraphReader:
    def __init__(self, b):
        self.b, self.p, self.refs = b, 0, []

    def take(self, n):
        require(0 <= n <= len(self.b) - self.p, 'truncated graph field')
        b = self.b[self.p:self.p+n]
        self.p += n
        return b

    def var(self):
        n = 0
        for shift in range(0, 70, 7):
            x = self.take(1)[0]
            n |= (x & 127) << shift
            if not x & 128:
                require(n <= NIL, 'varint overflow')
                return n
        raise ValueError('unterminated varint')

    def string(self):
        return self.take(self.var()).decode('utf-8', errors='backslashreplace')

    def many(self, fn, multiplier=1):
        n = self.var() * multiplier
        require(n <= len(self.b), 'unbounded graph count')
        return [fn() for _ in range(n)]

    def value(self):
        tag = self.take(1)[0]
        if tag < 4:
            return [None, False, True, {'ellipsis': True}][tag]
        if tag == 4:
            n = self.var()
            return ~(n >> 1) if n & 1 else n >> 1
        if tag in (5, 7, 8, 9, 10):
            return {'tag': tag, 'text': self.string()}
        if tag == 6:
            return {'float_bytes': self.take(8).hex()}
        if tag == 11:
            return {'builtin': [self.string(), self.string()]}
        require(tag == 12, 'unknown graph value tag')
        oid = self.var()
        self.refs.append(oid)
        return {'ref': oid}

    def object(self):
        kind = self.take(1)[0]
        require(kind < 17, 'unknown graph object kind')
        if kind in (0, 1, 3, 4):
            fields = self.many(self.value)
        elif kind == 2:
            fields = self.many(self.value, 2)
        elif kind == 5:
            fields = self.string()
        elif kind in (6, 11, 12):
            fields = self.value()
        elif kind in (7, 15):
            names = [self.string() for _ in range(3 if kind == 15 else 1)]
            fields = [names, self.many(self.value), self.value()]
        elif kind == 8:
            fields = [self.value(), self.many(self.value, 2)]
        elif kind in (9, 14):
            flag = self.take(1)[0]
            names = [self.string() for _ in range(2 if kind == 14 else 1)]
            fields = [flag, names, self.many(self.var), self.many(self.value), self.value()]
        elif kind == 16:
            flag = self.take(1)[0]
            fields = [flag, {'native_code_oid': self.var()},
                      self.many(self.value), self.value()]
        elif kind == 10:
            fields = [self.value(), self.many(self.value)]
        else:  # bound method
            fields = [self.value(), self.value()]
        require(self.p == len(self.b), 'trailing graph object bytes')
        return {'kind': kind, 'fields': fields}


def graph(b):
    require(len(b) >= 21 and b[0] == ord('H'), 'invalid graph header')
    nu = uint(b, 17, 4)
    require(nu in (72, 88) and len(b) >= 21 + nu, 'invalid graph user header')
    out = {'version': uint(b, 1), 'oldest': uint(b, 9),
           'environment': b[21:85].decode('ascii'), 'next_oid': uint(b, 85),
           'objects': {}}
    refs, p, roots = [], 21 + nu, None
    while p < len(b):
        require(len(b) - p >= 25 and b[p] == ord('o'), 'invalid graph record')
        oid, ver, n = (uint(b, p+x) for x in (1, 9, 17))
        require(n <= len(b) - p - 25, 'truncated graph record')
        r = GraphReader(b[p+25:p+25+n])
        if oid == 1:  # OID_ROOTS (p9space.h)
            require(roots is None, 'duplicate graph roots')
            roots = [(r.string(), r.var()) for _ in range(r.var())]
            require(r.p == len(r.b), 'trailing root bytes')
            refs.extend(o for _, o in roots)
        elif oid != NIL - 1:  # OID_UPGRADE_META is opaque here
            require(str(oid) not in out['objects'], 'duplicate graph OID')
            out['objects'][str(oid)] = dict(r.object(), version=ver)
            refs.extend(r.refs)
        p += 25 + n
    require(roots is not None, 'missing graph roots')
    require(all(str(o) in out['objects'] for o in refs), 'dangling graph reference')
    require(all(out['objects'][str(o)]['kind'] == 2 for _, o in roots), 'non-dict root')
    out['roots'] = roots
    return out


def inspect(path, extract=None, sid=1):
    im = ge['Image'](path)
    try:
        supers = [ge['parse_super'](im, off, label) for off, label in
                  ((0, 'primary'), (im.size // BS * BS - BS, 'backup'))]
        sb = next((s for s in supers if s['hash']['ok']), None)
        require(sb is not None and sb['flag'] == (1 << 63 | 1), 'no valid O v1 superblock')
        mode = sb['hash']['mode']
        checked, arenas, freesets = {}, [], []
        for addr, want in sb['arenabp']:
            copies = [ge['blkhash'](im.read(addr + d), mode) == want for d in (0, BS)]
            require(any(copies), 'neither arena copy matches superblock hash')
            b = im.read(addr + (0 if copies[0] else BS))
            require(uint(b, 0, 2) == 5, 'invalid arena block')
            log = ge['walk_log'](im, (uint(b, 2), uint(b, 10)))
            for entry in log:
                lb = im.read(entry['addr'])
                require(ge['bufhash'](lb[36:36+uint(lb, 2, 2)], mode) == uint(lb, 4),
                        'allocation log payload hash mismatch')
            # Unlike gefslog's descriptive counters, reject replay errors.
            free = ge['Freeset']()
            for block in log:
                finished = False
                for entry in block['entries']:
                    op = entry['op']
                    if op == 3:
                        if entry['gen'] >= sb['qgen']:
                            finished = True
                            break
                        continue
                    off, length = entry['off'] & ~255, entry['len']
                    require(off % BS == length % BS == 0 and length > 0
                            and addr <= off < off+length <= addr+uint(b, 18),
                            'allocation range outside arena')
                    if op in (1, 4):
                        require(free.grab(off, length), 'allocation of non-free range')
                    else:
                        require(not any(off < o+n and o < off+length for o, n in free.ranges),
                                'overlapping free ranges')
                        free.free(off, length)
                if finished:
                    break
            freesets.append(free)
            arenas.append({'addr': addr, 'copies_match': copies,
                           'log': ge['analyse_log'](log, sb['qgen'])})

        def tree(bp, depth=0):
            addr, want = bp[:2]
            require(depth < 32 and addr not in checked and len(checked) < 100000,
                    'snapshot cycle, shared block or excessive tree')
            require(addr % BS == 0 and 0 <= addr <= im.size - BS, 'invalid block address')
            require(not any(f.covers(addr, BS) for f in freesets),
                    'snapshot references a free block')
            b = im.read(addr)
            require(ge['blkhash'](b, mode) == want, 'snapshot block hash mismatch at %#x' % addr)
            typ, n = uint(b, 0, 2), uint(b, 2, 2)
            require(typ in (1, 2), 'invalid snapshot node type')
            checked[addr] = typ
            base = 10 if typ == 1 else 6

            def record(area, i, op=False):
                p = area + uint(b, area + 2*i, 2)
                opcode = b[p] if op else None
                p += int(op)
                nk = uint(b, p, 2)
                k = b[p+2:p+2+nk]
                p += 2 + nk
                nv = uint(b, p, 2)
                require(p+2+nv <= BS, 'truncated tree record')
                return k, b[p+2:p+2+nv], opcode

            records = [record(base, i) for i in range(n)]
            require(all(a[0] < z[0] for a, z in zip(records, records[1:])), 'unsorted tree keys')
            if typ == 2:
                return {k: v for k, v, _ in records}
            values = {}
            for _, v, _ in records:
                require(len(v) == 26, 'invalid pivot pointer')
                child = tree(struct.unpack_from('>QQQ', v), depth+1)
                require(not values.keys() & child.keys(), 'duplicate leaf keys')
                values.update(child)
            for i in range(uint(b, 6, 2)):
                k, v, op = record(base + 8187, i, True)
                require(op in (1, 2, 3, 4), 'unsupported buffered operation %d' % op)
                if op == 1:
                    values[k] = v
                else:
                    values.pop(k, None)
            return values

        values = tree(sb['snapbp'])
        require(values.get(b'\x08init') == (1).to_bytes(8, 'big'), 'invalid init catalog')
        hdr = values[b'\x07' + sid.to_bytes(8, 'big')]
        published, oldest, writer = struct.unpack_from('>QQQ', hdr)
        objects = {}
        for k, v in values.items():
            if len(k) == 29 and k[:9] == b'\x07' + sid.to_bytes(8, 'big'):
                oid, negver, idx = struct.unpack_from('>QQI', k, 9)
                ver = NIL ^ negver
                if ver <= published:
                    objects.setdefault(oid, {}).setdefault(ver, {})[idx] = v
        result = {'superblocks': supers, 'arenas': arenas, 'tree_blocks': len(checked),
                  'tree_records': len(values), 'space': {'id': sid, 'N': published, 'K': oldest, 'W': writer},
                  'catalog': {k[1:].decode('utf-8'): uint(v, 0)
                              for k, v in values.items() if k[:1] == b'\x08' and len(v) == 8},
                  'objects': {}}
        if extract:
            pathlib.Path(extract).mkdir()  # refuse an existing output directory
        for oid, versions in sorted(objects.items()):
            ver = max(versions)
            chunks = versions[ver]
            require(sorted(chunks) == list(range(len(chunks))), 'non-contiguous chunks')
            data = b''.join(chunks[i] for i in range(len(chunks)))
            n = uint(data, 0, 4)
            if n == (1 << 32) - 1:
                continue
            require(len(data) == n + 4, 'object length mismatch')
            data = data[4:]
            result['objects']['%016x' % oid] = {'version': ver, 'bytes': n,
                                               'sha256': hashlib.sha256(data).hexdigest()}
            if extract:
                (pathlib.Path(extract) / ('%016x.bin' % oid)).write_bytes(data)
            if oid == NIL - 2:
                result['graph'] = graph(data)
        return result
    finally:
        im.close()


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image')
    ap.add_argument('--extract', help='new directory for published object bytes')
    ap.add_argument('--space-id', type=int, default=1, help='numeric SpaceID (default 1)')
    args = ap.parse_args()
    if not 0 <= args.space_id < 2**64:
        ap.error('SpaceID must fit in 64 bits')
    print(json.dumps(inspect(args.image, args.extract, args.space_id), indent=2))
