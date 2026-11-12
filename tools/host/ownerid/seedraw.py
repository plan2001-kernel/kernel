#!/usr/bin/env python3
"""Flat GE/O v2 raw-record fixtures for owner recovery tests.

legacy: ownerless flat O volume with 120 OIDs, empty objects and tombstones.
        The formatter supplies the single published-version-1 header.
corrupt: flat live objects with missing/malformed/zero Kowner records.
         The runner enables owner mode through the formatter seed/alloc.
"""
from __future__ import annotations

import argparse
import pathlib
import struct

KOBJ, KOWNER = 7, 8
OBJTOMB = 0xFFFFFFFF
M64 = (1 << 64) - 1
BASE = 0x0000010000000000
COUNT = 120


def u32(n: int) -> bytes:
    return struct.pack(">I", n)


def u64(n: int) -> bytes:
    return struct.pack(">Q", n)


def kobj(oid: int, ver: int, idx: int) -> bytes:
    return bytes([KOBJ]) + u64(oid) + u64((~ver) & M64) + u32(idx)


def kowner(oid: int, ver: int) -> bytes:
    return bytes([KOWNER]) + u64(oid) + u64((~ver) & M64)


def rec(key: bytes, value: bytes) -> bytes:
    return u32(len(key)) + u32(len(value)) + key + value


def objrecs(oid: int, ver: int, data: bytes) -> list[bytes]:
    return [rec(kobj(oid, ver, 0), u32(len(data)) + data)]


def tomb(oid: int, ver: int) -> bytes:
    return rec(kobj(oid, ver, 0), u32(OBJTOMB))


def legacy() -> list[bytes]:
    out = []
    for i in range(COUNT):
        oid = BASE + i
        if i % 10 == 9:
            out.append(tomb(oid, 1))
        elif i % 17 == 0:
            out += objrecs(oid, 1, b"")
        else:
            out += objrecs(oid, 1, ("v1-%04d" % i).encode())
    return out


def corrupt() -> list[bytes]:
    out = []
    for i, label in enumerate(("missing", "badmagic", "badlen", "zero")):
        out += objrecs(BASE + i, 1, ("corrupt-" + label).encode())
    out.append(rec(kowner(BASE + 1, 1),
                   b"BADMAGIC" + u64(0x1111222233334444)))
    out.append(rec(kowner(BASE + 2, 1), b"OOWNER01"))
    out.append(rec(kowner(BASE + 3, 1), b"OOWNER01" + u64(0)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=("legacy", "corrupt"))
    ap.add_argument("output", type=pathlib.Path)
    args = ap.parse_args()
    records = legacy() if args.kind == "legacy" else corrupt()
    args.output.write_bytes(b"RAWREC01" + b"".join(records))
    print(f"seedraw: {args.kind}: {len(records)} flat records -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
