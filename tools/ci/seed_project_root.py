#!/usr/bin/env python3
"""Provision one empty project object in a fresh formatter seed directory."""
import argparse
from pathlib import Path
import struct
import sys

def provision(seed,kernel):
    sys.path.insert(0,str(Path(kernel)/'tools/host'))
    from object_format import pack,cbor
    seed=Path(seed)
    if not (seed/'env').is_file() or (seed/'bad').exists() or (seed/'badoid').exists():
        raise ValueError('requires fresh oseed directory without an extra object')
    raw=b'P2PROJ01'+struct.pack('>QQQ',1,0,0)
    h=14695981039346656037
    for b in raw:h=((h^b)*1099511628211)&((1<<64)-1)
    raw+=struct.pack('>Q',h)
    meta={'profile':'plan2001.unix.v1','name':'project','name_bytes':b'project','created':None,'media_type':'inode/directory','unix':{'mode':0o40700,'uid':0,'gid':0}}
    (seed/'bad').write_bytes(pack(raw,cbor(meta)))
    (seed/'badoid').write_text('4f45584543000007')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('kernel',type=Path);p.add_argument('seed',type=Path);a=p.parse_args();provision(a.seed,a.kernel)
