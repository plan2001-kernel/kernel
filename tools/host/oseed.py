#!/usr/bin/env python3
"""Linux-host O seed generator for the native GE/O formatter.

This replaces the old in-VM ``space.define`` seeder.  It compiles the
chosen module source with the repository's same-version ``mpy-cross``
and writes exactly the seed inputs that ``ge/format/ream.c::oseed``
consumes:

    seed/env
    seed/envhex
    seed/mod/<source sha256>.mpy
    seed/mp
    seed/mpoid

The Environment's definition ID is the SHA-256 of the source bytes.
The ``.mpy`` digest in the Environment is the SHA-256 of the bytes the
formatter stores.  The MicroPython executable is copied byte for byte
at OID 0x4f45584543000003.  The initial demo code OID is preserved as
0xffffffff00000000 and all seeded objects belong directly to the volume-global OID domain.
The formatter creates no named SpaceID catalog.
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MPY = ROOT / "micropython"
DEFAULT_MPY_CROSS = ROOT / "build" / "mpy-cross" / "mpy-cross"
OID_CODE = 0xffffffff00000000
# Persistent GE/O identity and external name (docs/O-FORMAT.md): keep MP.
OID_MP = 0x4f45584543000003
MODULE_NAME = "odemo"
MPY_MAGIC = ord("M")


def _define(path: pathlib.Path, name: str) -> str:
    text = path.read_text()
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?([0-9]+)\)?\s*$", text, re.M)
    if not m:
        raise SystemExit(f"oseed: cannot read {name} from {path}")
    return m.group(1)


def runtime_id() -> str:
    major = _define(MPY / "py" / "mpconfig.h", "MICROPY_VERSION_MAJOR")
    minor = _define(MPY / "py" / "mpconfig.h", "MICROPY_VERSION_MINOR")
    micro = _define(MPY / "py" / "mpconfig.h", "MICROPY_VERSION_MICRO")
    prerelease = _define(MPY / "py" / "mpconfig.h", "MICROPY_VERSION_PRERELEASE")
    mpy = _define(MPY / "py" / "persistentcode.h", "MPY_VERSION")
    sub = _define(MPY / "py" / "persistentcode.h", "MPY_SUB_VERSION")
    preview = "-preview" if prerelease != "0" else ""
    # Persisted in GE/O Environment manifests: keep the MP/MPY spelling.
    return f"mp-{major}.{minor}.{micro}{preview}-mpy{mpy}.{sub}"


def mpy_version() -> int:
    return int(_define(MPY / "py" / "persistentcode.h", "MPY_VERSION"))


def small_int_bits() -> int:
    # The canonical runtime is a 64-bit amd64 build with the default
    # MICROPY_OBJ_REPR_A; its compiler uses MP_SMALL_INT_BITS (63).
    # mpy-cross defaults to 31 for portability, which the loader also
    # accepts, but 63 matches what space.define would emit.
    return struct.calcsize("P") * 8 - 1


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def compile_mpy(source: pathlib.Path, out: pathlib.Path, mpy_cross: pathlib.Path) -> bytes:
    if out.exists():
        out.unlink()
    cmd = [
        str(mpy_cross),
        "-s", source.name,
        f"-msmall-int-bits={small_int_bits()}",
        "-X", "emit=bytecode",
        "-o", str(out),
        str(source),
    ]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout)
        sys.stderr.write(p.stderr)
        raise SystemExit(f"oseed: mpy-cross failed for {source}")
    if not out.is_file():
        raise SystemExit(f"oseed: mpy-cross produced no {out}")
    data = out.read_bytes()
    if len(data) < 4 or data[0] != MPY_MAGIC:
        raise SystemExit(f"oseed: {out} has no .mpy header")
    want = mpy_version()
    if data[1] != want:
        raise SystemExit(f"oseed: {out} is .mpy version {data[1]}, expected {want}")
    arch = (data[2] >> 2) & 0x2F
    if arch != 0 or data[2] & 0x40:
        raise SystemExit(f"oseed: {out} contains native code (feature byte {data[2]:#x})")
    bits = small_int_bits()
    if data[3] > bits:
        raise SystemExit(f"oseed: {out} assumes {data[3]}-bit small ints, runtime has {bits}")
    return data


def build_seed(source: pathlib.Path, seed: pathlib.Path, mpy_exec: pathlib.Path | None,
               mpy_cross: pathlib.Path, ike_exec: pathlib.Path | None = None) -> dict[str, str]:
    src = source.read_bytes()
    definition = sha256_hex(src)

    if seed.exists() and any(seed.iterdir()):
        raise SystemExit(f"oseed: seed directory {seed} is not empty")
    (seed / "mod").mkdir(parents=True, exist_ok=True)
    mpy_path = seed / "mod" / f"{definition}.mpy"
    mpy_bytes = compile_mpy(source, mpy_path, mpy_cross)
    mpy_digest = sha256_hex(mpy_bytes)

    env = (
        f"runtime {runtime_id()}\n"
        f"{MODULE_NAME} {definition} {mpy_digest} {OID_CODE:016x}\n"
    ).encode()
    env_digest = sha256_hex(env)

    mpy_exec_bytes = None
    if mpy_exec is not None:
        mpy_exec_bytes = mpy_exec.read_bytes()
        if len(mpy_exec_bytes) < 4 or mpy_exec_bytes[:4] != b"\x7fELF":
            raise SystemExit(f"oseed: {mpy_exec} is not an ELF executable")
        # Seed filenames are a formatter contract (ge/format/ream.c): keep MP.
        (seed / "mp").write_bytes(mpy_exec_bytes)
        (seed / "mpoid").write_bytes(f"{OID_MP:016x}".encode())
    if ike_exec is not None:
        executable = ike_exec.read_bytes()
        if executable[:4] != b"\x7fELF":
            raise SystemExit("oseed: iked is not an ELF executable")
        # Reuse the formatter's existing optional raw executable seed slots.
        (seed / "rawexec").write_bytes(executable)
        (seed / "rawexecoid").write_text("4f45584543000004")
    (seed / "env").write_bytes(env)
    (seed / "envhex").write_bytes(env_digest.encode())

    # Read the seed back: the formatter must store exactly these bytes.
    if sha256_hex(source.read_bytes()) != definition:
        raise SystemExit("oseed: source changed while seeding")
    if sha256_hex(mpy_path.read_bytes()) != mpy_digest:
        raise SystemExit("oseed: stored .mpy digest mismatch")
    if sha256_hex((seed / "env").read_bytes()) != env_digest:
        raise SystemExit("oseed: stored Environment digest mismatch")
    if mpy_exec_bytes is not None and (seed / "mp").read_bytes() != mpy_exec_bytes:
        raise SystemExit("oseed: stored MicroPython executable changed")
    if (seed / "envhex").read_text() != env_digest:
        raise SystemExit("oseed: envhex mismatch")
    if mpy_exec_bytes is not None:
        if (seed / "mpoid").read_text() != f"{OID_MP:016x}":
            raise SystemExit("oseed: mpoid mismatch")

    return {
        "runtime": runtime_id(),
        "definition": definition,
        "mpy": mpy_digest,
        "mpy_bytes": str(len(mpy_bytes)),
        "env": env_digest,
        "env_bytes": str(len(env)),
        "mpy_exec_sha256": (sha256_hex(mpy_exec_bytes)
                            if mpy_exec_bytes is not None else "none"),
        "mpy_exec_bytes": (str(len(mpy_exec_bytes))
                           if mpy_exec_bytes is not None else "0"),
        "code_oid": f"{OID_CODE:016x}",
        "mpy_exec_oid": (f"{OID_MP:016x}"
                         if mpy_exec_bytes is not None else "none"),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", type=pathlib.Path, help="Python module source")
    ap.add_argument("--seed", type=pathlib.Path, help="new seed directory")
    ap.add_argument("--mp-exec", dest="mpy_exec", type=pathlib.Path,
                    help="MicroPython ELF to store")
    ap.add_argument("--ike-exec", type=pathlib.Path, help="optional userspace iked ELF")
    ap.add_argument("--no-mp", dest="no_mpy", action="store_true",
                    help="omit seed/mp and seed/mpoid (NOMPEXEC images)")
    ap.add_argument("--mpy-cross", type=pathlib.Path, default=DEFAULT_MPY_CROSS,
                    help=f"mpy-cross binary (default {DEFAULT_MPY_CROSS})")
    ap.add_argument("--print-runtime", action="store_true",
                    help="print the current canonical space.runtime literal")
    args = ap.parse_args()

    if args.print_runtime:
        print(runtime_id())
        return 0
    if args.source is None or args.seed is None:
        ap.error("--source and --seed are required")
    if args.no_mpy and args.mpy_exec is not None:
        ap.error("--no-mp and --mp-exec are exclusive")
    if not args.no_mpy and args.mpy_exec is None:
        ap.error("--mp-exec or --no-mp is required")
    check = [args.source, args.mpy_cross]
    if args.mpy_exec is not None:
        check.append(args.mpy_exec)
    for p in check:
        if not p.is_file():
            ap.error(f"no {p}")
    try:
        info = build_seed(args.source, args.seed,
                          None if args.no_mpy else args.mpy_exec, args.mpy_cross, args.ike_exec)
    except OSError as e:
        print(f"oseed: {e}", file=sys.stderr)
        return 1
    print("seed: runtime " + info["runtime"])
    print("seed: odemo definition " + info["definition"])
    print("seed: odemo mpy " + info["mpy"] + f" ({info['mpy_bytes']} bytes)")
    print("seed: Environment " + info["env"] + f" ({info['env_bytes']} bytes)")
    if info["mpy_exec_sha256"] != "none":
        print("seed: MicroPython " + info["mpy_exec_sha256"]
              + f" ({info['mpy_exec_bytes']} bytes)"
              f" oid {info['mpy_exec_oid']}")
    print("seed: code oid " + info["code_oid"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
