#!/usr/bin/env python3
"""Provision a *fresh* GE/O seed with a checked UXFS v1 root manifest.

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
NODES = 64
NODE_SIZE = 272
MANIFEST_BYTES = 16 + NODES * NODE_SIZE + 8


def fnv1a(data: bytes) -> int:
    value = 14695981039346656037
    for ch in data:
        value = ((value ^ ch) * 1099511628211) & ((1 << 64) - 1)
    return value


def initial_manifest() -> bytes:
    header = struct.pack("<QII", UXFS_MAGIC, 1, 0)
    nodes = bytearray(NODES * NODE_SIZE)
    for slot, path in enumerate(BOOT_DIRS):
        label = path.encode("ascii")
        assert len(label) < 256
        offset = slot * NODE_SIZE
        nodes[offset:offset + len(label)] = label
        struct.pack_into("<QII", nodes, offset + 256, 0, 0, 1)
    payload = header + bytes(nodes)
    result = payload + struct.pack("<Q", fnv1a(payload))
    assert len(result) == MANIFEST_BYTES
    return result


def provision(seed: Path) -> None:
    if not seed.is_dir() or not (seed / "env").is_file():
        raise ValueError("must use an existing, freshly prepared oseed directory")
    for name in ("bad", "badoid"):
        if (seed / name).exists():
            raise ValueError("refusing to replace existing formatter seed " + name)
    (seed / "bad").write_bytes(initial_manifest())
    (seed / "badoid").write_text(f"{MANIFEST_OID:016x}", encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=Path, required=True)
    provision(parser.parse_args().seed)
    print(f"Provisioned UXFS v1 GE/O manifest at 0x{MANIFEST_OID:016x}")
