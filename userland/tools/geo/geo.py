#!/usr/bin/env python3
"""Linux GE/O inspection/export and explicit offline snapshot rebuild.
Rebuild is NOT an in-place transactional put: it resets versions/volume layout.
Owner-aware volumes are rejected rather than silently discarding provenance.
"""
import argparse
import subprocess
import json
import tempfile
import hashlib
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--kernel', type=Path, default=Path(__file__).resolve().parents[3] / 'kernel')
s = p.add_subparsers(dest='op', required=True)
a = s.add_parser('ls')
a.add_argument('volume')
a.add_argument('--json', action='store_true')
a = s.add_parser('get')
a.add_argument('volume')
a.add_argument('oid', type=lambda x: int(x, 0))
a.add_argument('output', type=Path)
for op in ('raw','metadata'):
    a=s.add_parser(op)
    a.add_argument('volume');a.add_argument('oid',type=lambda x:int(x,0))
    a.add_argument('output',type=Path)
a = s.add_parser('rebuild')
a.add_argument('volume')
a.add_argument('output', type=Path)
a.add_argument('--put', action='append', default=[], metavar='OID=FILE')
a = p.parse_args()
k = a.kernel.resolve()
with tempfile.TemporaryDirectory(prefix='geo-') as t:
    t = Path(t)
    extract = t / 'objects'
    cmd = ['python3', str(k / 'tools/oinspect.py'), a.volume]
    if a.op != 'ls':
        cmd += ['--extract', str(extract)]
    d = json.loads(subprocess.check_output(cmd, text=True))
    if a.op == 'ls':
        if a.json:
            print(json.dumps(d, indent=2))
        else:
            print('OID                 BYTES       SHA256')
            for oid, item in d['objects'].items():
                print(f"0x{oid} {item['bytes']:10d} {item['sha256']}")
    elif a.op in ('get','raw','metadata'):
        if a.output.exists():
            p.error('output must not exist')
        src = extract / f'{a.oid:016x}.bin'
        if not src.exists():
            p.error('OID not found')
        data=src.read_bytes()
        if a.op!='get':
            import sys
            sys.path.insert(0,str(k/'tools/host'))
            from object_format import unpack
            raw,meta=unpack(data);data=raw if a.op=='raw' else meta
        with a.output.open('xb') as out:
            out.write(data)
    else:
        if a.output.exists():
            p.error('output must not exist; rebuild writes regular new images only')
        if d['owners'] or d['owner_mode']:
            p.error('owner-aware rebuild not supported; provenance must be preserved by a native transaction writer')
        for pair in a.put:
            try:
                oid, name = pair.split('=', 1)
                oid = int(oid, 0)
            except ValueError:
                p.error('--put requires OID=FILE with a numeric OID')
            if not 0 < oid < 2 ** 64:
                p.error('OID outside unsigned 64-bit range')
            (extract / f'{oid:016x}.bin').write_bytes(Path(name).read_bytes())
        seed = t / 'seed'
        (seed / 'mod').mkdir(parents=True)
        env = (extract / 'fffffffffffffffc.bin').read_bytes()
        (seed / 'env').write_bytes(env)
        (seed / 'envhex').write_text(hashlib.sha256(env).hexdigest())
        excluded = {0xfffffffffffffffc}  # Environment seeded by native formatter.
        for line in env.decode('ascii').splitlines():
            if line.startswith('runtime '):
                continue
            name, definition, digest, oid = line.split()
            if len(definition) != 64 or any(c not in '0123456789abcdef' for c in definition):
                p.error('Environment definition must be a SHA-256 hex digest')
            oid = int(oid, 16)
            excluded.add(oid)
            payload = (extract / f'{oid:016x}.bin').read_bytes()
            if hashlib.sha256(payload).hexdigest() != digest:
                p.error('Environment module digest mismatch')
            (seed / 'mod' / f'{definition}.mpy').write_bytes(payload)
        raw = bytearray(b'RAWREC01')
        for src in sorted(extract.glob('*.bin')):
            oid = int(src.stem, 16)
            if oid in excluded:
                continue
            data = struct.pack('>I', src.stat().st_size) + src.read_bytes()
            for i, offset in enumerate(range(0, len(data), 480)):
                # Native Kobj key, inverted version 1; value chunks are 480 bytes.
                key = struct.pack('>BQQI', 7, oid, 2 ** 64 - 2, i)
                value = data[offset:offset + 480]
                raw += struct.pack('>II', len(key), len(value)) + key + value
        (seed / 'raw').write_bytes(raw)
        size = Path(a.volume).stat().st_size
        if size == 0:
            # Block-device stat has size zero; query its actual byte capacity.
            size = int(subprocess.check_output(['blockdev', '--getsize64', a.volume]))
        with a.output.open('xb') as out:
            out.truncate(size)
        subprocess.run([str(k / 'build/format-o/format-o'), '--seed', str(seed), str(a.output)], check=True)
        verify = json.loads(subprocess.check_output(['python3', str(k / 'tools/oinspect.py'), str(a.output)], text=True))
        for src in extract.glob('*.bin'):
            if verify['objects'][src.stem]['sha256'] != hashlib.sha256(src.read_bytes()).hexdigest():
                raise SystemExit('rebuilt object verification failed')
        print('Verified rebuilt live objects; versions reset. Input volume was not modified.')
