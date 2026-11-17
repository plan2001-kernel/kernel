#!/usr/bin/env python3
"""Seed only the native GUI into a fresh oseed fixture (no Unix executables)."""
import argparse
import importlib.util
from pathlib import Path
import sys

def provision(seed, kernel, elf):
    seed, kernel, elf = map(Path, (seed, kernel, elf))
    if not (seed/'env').is_file() or (seed/'raw').exists():
        raise ValueError('fresh oseed directory without raw extensions required')
    sys.path.insert(0, str(kernel/'tools/host'))
    from object_format import frame_file
    spec = importlib.util.spec_from_file_location('native_gui_seedraw', kernel/'tools/host/ownerid/seedraw.py')
    encoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(encoder)
    if not elf.read_bytes().startswith(b'\x7fELF'):
        raise ValueError('native ELF required')
    data = frame_file(elf, 'application/x-elf')
    records = bytearray(b'RAWREC01')
    at = index = 0
    while at < len(data):
        header = encoder.u32(len(data)) if not index else b''
        chunk = data[at:at+480-len(header)]
        records += encoder.rec(encoder.kobj(0x4f4558454300000c, 1, index), header+chunk)
        at += len(chunk)
        index += 1
    (seed/'raw').write_bytes(records)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    for name in ('seed', 'kernel', 'elf'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    provision(a.seed, a.kernel, a.elf)
