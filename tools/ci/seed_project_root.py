#!/usr/bin/env python3
"""Provision one empty project object in a fresh formatter seed directory."""
import argparse
from pathlib import Path
import struct
import sys

PROJECT_OID = 0x4f45584543000007
F6_PROJECT_OID = 0x4f45584543000008

def initial_root(kernel):
    sys.path.insert(0,str(Path(kernel)/'tools/host'))
    from object_format import pack,cbor
    raw=b'P2PROJ01'+struct.pack('>QQQ',1,0,0)
    h=14695981039346656037
    for b in raw:h=((h^b)*1099511628211)&((1<<64)-1)
    raw+=struct.pack('>Q',h)
    meta={'profile':'plan2001.unix.v1','name':'project','name_bytes':b'project','created':None,'media_type':'inode/directory','unix':{'mode':0o40700,'uid':0,'gid':0}}
    return pack(raw,cbor(meta))

def provision(seed,kernel,second_root=False):
    seed=Path(seed)
    names=['bad','badoid'] + (['raw'] if second_root else [])
    if not (seed/'env').is_file() or any((seed/name).exists() for name in names):
        raise ValueError('requires fresh oseed directory without an extra object')
    data=initial_root(kernel)
    if second_root:
        # Use the kernel's existing raw-record seed encoder. These are ordinary
        # version-1 object records, exactly as produced by seedfile(), with no
        # owner record or ambient authority added by this fixture.
        import importlib.util
        spec=importlib.util.spec_from_file_location('project_seedraw',
            Path(kernel)/'tools/host/ownerid/seedraw.py')
        encoder=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(encoder)
        if len(data)+4>480:
            raise ValueError('project root exceeds one formatter seed chunk')
        records=b'RAWREC01'+b''.join(encoder.objrecs(F6_PROJECT_OID,1,data))
        (seed/'raw').write_bytes(records)
    (seed/'bad').write_bytes(data)
    (seed/'badoid').write_text(f'{PROJECT_OID:016x}')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('kernel',type=Path);p.add_argument('seed',type=Path);a=p.parse_args();provision(a.seed,a.kernel)
