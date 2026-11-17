#!/usr/bin/env python3
"""Add a native toolchain and bounded install bundle to a fresh GE/O seed."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys

TCC_OID=0x4f45584543000009
BUNDLE_OID=0x4f4558454300000a


def bundle(sysroot):
    files=sorted(p for p in sysroot.rglob('*') if p.is_file())
    if not files or len(files)>4096: raise ValueError('invalid file count')
    out=bytearray(b'P2TCB001'+struct.pack('>I',len(files)))
    for path in files:
        name=path.relative_to(sysroot).as_posix().encode()
        if len(name)>=511: raise ValueError('path too long')
        data=path.read_bytes()
        out+=struct.pack('>HQ',len(name),len(data))+name+data
    if len(out)>=64*1024*1024: raise ValueError('bundle too large')
    return bytes(out)


def provision(seed,kernel,build):
    seed,kernel,build=map(Path,(seed,kernel,build))
    if not (seed/'env').is_file(): raise ValueError('fresh oseed directory required')
    # P2 supplies both roots; its raw input contains only the second root.
    raw=(seed/'raw').read_bytes()
    if raw[:8]!=b'RAWREC01': raise ValueError('invalid P2 raw seed')
    sys.path.insert(0,str(kernel/'tools/host'))
    from object_format import frame_file,pack
    spec=importlib.util.spec_from_file_location('toolchain_seedraw',kernel/'tools/host/ownerid/seedraw.py')
    encoder=importlib.util.module_from_spec(spec);spec.loader.exec_module(encoder)
    def records(oid,data):
        chunks=[];at=0;index=0
        while at<len(data) or not index:
            header=encoder.u32(len(data)) if not index else b''
            chunk=data[at:at+480-len(header)]
            chunks.append(encoder.rec(encoder.kobj(oid,1,index),header+chunk))
            at+=len(chunk);index+=1
        return b''.join(chunks)
    raw+=records(0x4f4558454300000b,frame_file(build/'bootstrap.elf','application/x-elf'))
    for oid,name in ((0x4f45584543000005,'supervisor.elf'),(0x4f45584543000006,'busybox.elf')):
        if (build/name).is_file(): raw+=records(oid,frame_file(build/name,'application/x-elf'))
    if (build/'gui/nuklear-demo.elf').is_file():
        raw+=records(0x4f4558454300000c,frame_file(build/'gui/nuklear-demo.elf','application/x-elf'))
    raw+=records(TCC_OID,frame_file(build/'tcc.elf','application/x-elf'))
    raw+=records(BUNDLE_OID,pack(bundle(build/'sysroot')))
    # A repeated call would duplicate OIDs. Mark it before any future caller
    # can reuse this directory; the top-level seeder already refuses reuse.
    marker=seed/'toolchain.json'
    if marker.exists(): raise ValueError('toolchain already seeded')
    (seed/'raw').write_bytes(raw)
    marker.write_text(json.dumps({'tcc_oid':f'{TCC_OID:016x}','bundle_oid':f'{BUNDLE_OID:016x}'})+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('seed','kernel','build'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();provision(a.seed,a.kernel,a.build)
