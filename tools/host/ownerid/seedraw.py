#!/usr/bin/env python3
"""Build the seed/raw fixture consumed by ge/format/ream.c.

legacy   a legacy Space (2) with >32 live objects, multiple versions, an
         empty object, tombstones, plus an empty Space (3); no owner
         records and no owner-mode header.
corrupt  an owner-aware Space (2) with live objects whose Kowner record is
         missing, malformed, or carries owner value 0.
"""
from __future__ import annotations

import argparse
import pathlib
import struct

KOBJ, KO, KOWNER = 7, 8, 9
OBJTOMB = 0xFFFFFFFF
M64 = (1 << 64) - 1
BASE = 0x0000010000000000
COUNT = 120
SPACE = 2
EMPTY_SPACE = 3


def u32(n: int) -> bytes:
    return struct.pack(">I", n)


def u64(n: int) -> bytes:
    return struct.pack(">Q", n)


def kobjhdr(space: int) -> bytes:
    return bytes([KOBJ]) + u64(space)


def kobj(space: int, oid: int, ver: int, idx: int) -> bytes:
    return bytes([KOBJ]) + u64(space) + u64(oid) + u64((~ver) & M64) + u32(idx)


def kowner(space: int, oid: int, ver: int) -> bytes:
    return bytes([KOWNER]) + u64(space) + u64(oid) + u64((~ver) & M64)


def rec(key: bytes, value: bytes) -> bytes:
    return u32(len(key)) + u32(len(value)) + key + value


def objrecs(space: int, oid: int, ver: int, data: bytes) -> list[bytes]:
    out = []
    if data:
        out.append(rec(kobj(space, oid, ver, 0), u32(len(data)) + data))
    else:
        out.append(rec(kobj(space, oid, ver, 0), u32(0)))
    return out


def tomb(space: int, oid: int, ver: int) -> bytes:
    return rec(kobj(space, oid, ver, 0), u32(OBJTOMB))


def hdr(space: int, n: int, k: int, w: int, user: bytes = b"") -> bytes:
    return rec(kobjhdr(space), u64(n) + u64(k) + u64(w) + user)


def legacy() -> list[bytes]:
    out = [
        rec(bytes([KO]) + b"/", u64(SPACE)),
        rec(bytes([KO]) + b"empty", u64(EMPTY_SPACE)),
        hdr(SPACE, 2, 1, 2),
        hdr(EMPTY_SPACE, 1, 1, 1),
    ]
    for i in range(COUNT):
        oid = BASE + i
        if i % 10 == 9:
            out += objrecs(SPACE, oid, 1, ("dead-%04d" % i).encode())
            out.append(tomb(SPACE, oid, 2))
        elif i % 17 == 0:
            out += objrecs(SPACE, oid, 1, b"")
        elif i % 3 == 0:
            out += objrecs(SPACE, oid, 1, ("v1-%04d" % i).encode())
            out += objrecs(SPACE, oid, 2, ("v2-%04d" % i).encode())
        else:
            out += objrecs(SPACE, oid, 1, ("v1-%04d" % i).encode())
    return out


def corrupt() -> list[bytes]:
    user = bytearray(104)
    user[72:80] = b"OALLOC01"
    struct.pack_into(">Q", user, 80, 0x40)
    user[88:96] = b"OOWNMODE"
    struct.pack_into(">Q", user, 96, 1)
    out = [
        rec(bytes([KO]) + b"/", u64(SPACE)),
        hdr(SPACE, 1, 1, 1, bytes(user)),
    ]
    out += objrecs(SPACE, BASE, 1, b"corrupt-missing")
    out += objrecs(SPACE, BASE + 1, 1, b"corrupt-badmagic")
    out += objrecs(SPACE, BASE + 2, 1, b"corrupt-badlen")
    out += objrecs(SPACE, BASE + 3, 1, b"corrupt-zero")
    out.append(rec(kowner(SPACE, BASE + 1, 1),
                   b"BADMAGIC" + u64(0x1111222233334444)))
    out.append(rec(kowner(SPACE, BASE + 2, 1), b"OOWNER01"))
    out.append(rec(kowner(SPACE, BASE + 3, 1), b"OOWNER01" + u64(0)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=("legacy", "corrupt"))
    ap.add_argument("output", type=pathlib.Path)
    args = ap.parse_args()
    records = legacy() if args.kind == "legacy" else corrupt()
    args.output.write_bytes(b"RAWREC01" + b"".join(records))
    print(f"seedraw: {args.kind}: {len(records)} records -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
