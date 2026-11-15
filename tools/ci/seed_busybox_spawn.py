#!/usr/bin/env python3
"""Seed native MicroPython + normal BusyBox ELF into a fresh GE/O image.

Use the kernel's own mpy-cross, oseed and format-o tools. No existing
device, O volume or user checkout is reformatted. This uses the formatter's
public rawexec/rawexecoid seed input with a dedicated BusyBox OID.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

BUSYBOX_OID = 0x4f45584543000005
MPY_OID = 0x4f45584543000003
UNIX_CHILD_OID = 0x4f45584543000006


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--unix-manifest", action="store_true",
                        help="provision a checked UXFS manifest on the new scratch volume")
    parser.add_argument("--extra-elf", type=Path,
                        help="additional checked native F5 child at OID 0x...06")
    parser.add_argument("--init", required=True, type=Path)
    parser.add_argument("--reports", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    args = parser.parse_args()

    kernel = args.kernel.resolve()
    reports = args.reports.resolve()
    image = args.image.absolute()
    if image.exists() or image.is_symlink():
        raise SystemExit("refusing existing GE/O image: " + str(image))
    if image.parent != reports and reports not in image.parents:
        raise SystemExit("image must be beneath the disposable reports directory")
    if any(str(image).startswith(prefix) for prefix in ("/dev/", "/sys/", "/proc/")):
        raise SystemExit("refusing device or pseudo-filesystem image")
    reports.mkdir(parents=True, exist_ok=True)
    image.parent.mkdir(parents=True, exist_ok=True)

    mp = kernel / "build/gcc/micropython.elf"
    cross = kernel / "build/mpy-cross/mpy-cross"
    formatter = kernel / "build/format-o/format-o"
    oseed = kernel / "tools/host/oseed.py"
    inspect = kernel / "tools/oinspect.py"
    for file in (mp, cross, formatter, oseed, inspect, args.elf, args.init,
                 *((args.extra_elf,) if args.extra_elf else ())):
        if not file.is_file():
            raise SystemExit("missing input: " + str(file))
    if not all(file.read_bytes().startswith(b"\x7fELF") for file in (mp, args.elf)):
        raise SystemExit("expected native ELF images")

    seed = reports / "seed-busybox-child"
    if seed.exists():
        raise SystemExit("refusing existing seed directory: " + str(seed))
    seed.mkdir()
    log = reports / "busybox-spawn-seed.log"
    with log.open("w", encoding="utf-8") as stream:
        for command in (
            ["python3", str(oseed), "--source", str(args.init),
             "--seed", str(seed), "--mp-exec", str(mp),
             "--mpy-cross", str(cross)],
            [str(formatter), "--seed", str(seed),
             "--size", "129", str(image)],
        ):
            if command[0] == str(formatter):
                # This is a formatter-neutral *raw* executable extension of
                # the normal seeder, never a second boot executable.
                (seed / "rawexec").write_bytes(args.elf.read_bytes())
                (seed / "rawexecoid").write_text(f"{BUSYBOX_OID:016x}",
                                                  encoding="ascii")
                if args.extra_elf:
                    (seed / "exe").write_bytes(args.extra_elf.read_bytes())
                    (seed / "exeoid").write_text(f"{UNIX_CHILD_OID:016x}",
                                                 encoding="ascii")
            if command[0] == str(formatter) and args.unix_manifest:
                from seed_unix_manifest import provision
                provision(seed)
            stream.write("$ " + " ".join(command) + "\n")
            stream.flush()
            run = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
            if run.returncode:
                raise SystemExit(f"seed/format command failed ({run.returncode}): "
                                 + command[0])

    result = subprocess.check_output(
        ["python3", str(inspect), str(image)], text=True)
    (reports / "busybox-spawn-oinspect.json").write_text(result, encoding="utf-8")
    info = json.loads(result)
    verified = [(MPY_OID, mp.read_bytes()), (BUSYBOX_OID, args.elf.read_bytes())]
    if args.extra_elf:
        verified.append((UNIX_CHILD_OID, args.extra_elf.read_bytes()))
    if args.unix_manifest:
        from seed_unix_manifest import MANIFEST_OID, initial_manifest
        verified.append((MANIFEST_OID, initial_manifest()))
    for oid, data in verified:
        found = info["objects"].get(f"{oid:016x}")
        if not found or found["sha256"] != sha(data) or found["bytes"] != len(data):
            raise SystemExit(f"seeded OID {oid:016x} disagrees with input ELF")
    if image.stat().st_size != 129 * 1024 * 1024:
        raise SystemExit("unexpected GE/O image size")
    manifest = {
        "boot_executable_oid": f"{MPY_OID:016x}",
        "boot_executable_sha256": sha(mp.read_bytes()),
        "child_executable_oid": f"{BUSYBOX_OID:016x}",
        "child_executable_sha256": sha(args.elf.read_bytes()),
        "initial_python_code": str(args.init),
        "additional_f5_child_oid": (f"{UNIX_CHILD_OID:016x}"
                                  if args.extra_elf else None),
        "additional_f5_child_sha256": (sha(args.extra_elf.read_bytes())
                                      if args.extra_elf else None),
        "image_size": image.stat().st_size,
        "result": "PASS: fresh GE/O volume contains both native ELFs",
    }
    (reports / "busybox-spawn-seed.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(manifest["result"], flush=True)


if __name__ == "__main__":
    main()
