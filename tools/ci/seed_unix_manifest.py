#!/usr/bin/env python3
"""Provision a *fresh* GE/O seed with a checked framed UXFS v3 root manifest.

The formatter's existing optional seed/bad + seed/badoid object slot holds an
ordinary, correctly formatted UXFS manifest. No kernel or GE/O format changes.
This module only accepts the temporary seed directory provided by the caller.
"""
from __future__ import annotations
import argparse
import struct
from pathlib import Path

MANIFEST_OID = 0x4F45584543000007
UXFS_MAGIC = 0x3153465855323030
BOOT_DIRS = ("/", "/bin", "/dev", "/etc", "/home", "/tmp",
             "/usr", "/usr/bin", "/var")
NODES = 2048
NODE_SIZE = 272
META_SIZE = 376
MANIFEST_BYTES = 16 + NODES * NODE_SIZE + NODES * META_SIZE + 8


def fnv1a(data: bytes) -> int:
    value = 14695981039346656037
    for ch in data:
        value = ((value ^ ch) * 1099511628211) & ((1 << 64) - 1)
    return value


def initial_manifest(kernel: Path) -> bytes:
    header = struct.pack("<QII", UXFS_MAGIC, 3, 0)
    nodes = bytearray(NODES * NODE_SIZE)
    for slot, path in enumerate(BOOT_DIRS):
        label = path.encode("ascii")
        assert len(label) < 256
        offset = slot * NODE_SIZE
        nodes[offset:offset + len(label)] = label
        struct.pack_into("<QII", nodes, offset + 256, 0, 0, 1)
    metadata=bytearray(NODES*META_SIZE)
    for slot,path in enumerate(BOOT_DIRS):
        name=path.rsplit('/',1)[-1] or '/'
        entry=struct.pack('<256s64sqqqqQIIII',name.encode(),b'inode/directory',0,0,0,0,0,0o40700,0,0,0)
        metadata[slot*META_SIZE:(slot+1)*META_SIZE]=entry
    payload = header + bytes(nodes)+bytes(metadata)
    result = payload + struct.pack("<Q", fnv1a(payload))
    assert len(result) == MANIFEST_BYTES
    import sys
    sys.path.insert(0,str(kernel/'tools/host'))
    from object_format import pack,cbor
    return pack(result,cbor({'profile':'plan2001.unix.v1','name':'uxfs-manifest','created':None,'media_type':'application/vnd.plan2001.uxfs-manifest'}))


def provision(seed: Path,kernel: Path) -> None:
    if not seed.is_dir() or not (seed / "env").is_file():
        raise ValueError("must use an existing, freshly prepared oseed directory")
    for name in ("bad", "badoid"):
        if (seed / name).exists():
            raise ValueError("refusing to replace existing formatter seed " + name)
    (seed / "bad").write_bytes(initial_manifest(kernel))
    (seed / "badoid").write_text(f"{MANIFEST_OID:016x}", encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=Path, required=True)
    parser.add_argument("--kernel",type=Path,required=True)
    args=parser.parse_args()
    provision(args.seed,args.kernel)
    print(f"Provisioned framed UXFS v3 GE/O manifest at 0x{MANIFEST_OID:016x}")
