#!/usr/bin/env python3
"""Build the experimental Plan2001-O AArch64 boot kernel.

This is a freestanding ELF64 AArch64 image, not a new operating system: its
one job is to enter at EL1 from the UEFI loader, set up a minimal boot map
and console, validate the same Plan2001 BootInfo v1 blob the AMD64 kernel
validates through kernel/core/oplanboot.c, and print an ARM64 marker.  It
is deliberately independent of 9front mk/Ken C and of the AMD64 manifest.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
OUT = ROOT / "build" / "arm64"
OBJ = OUT / "obj"
LOG = OUT / "log"
ELF = OUT / "9karm64.elf"
LDSCRIPT = ROOT / "tools/host/kernel-arm64.ld"

SOURCES = {
    "entry": "kernel/arm64/gcc/entry.S",
    "vectors": "kernel/arm64/gcc/vectors.S",
    "main": "kernel/arm64/gcc/main.c",
    "bootmem": "kernel/arm64/gcc/bootmem.c",
    "oplanboot": "kernel/core/oplanboot.c",
}

CFLAGS = [
    "--target=aarch64-none-elf",
    "-std=gnu11",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fno-pic",
    "-fno-pie",
    "-fno-asynchronous-unwind-tables",
    "-fno-unwind-tables",
    "-fcommon",
    "-mgeneral-regs-only",
    "-mno-outline-atomics",
    "-mstrict-align",
    "-mcmodel=small",
    "-DPLAN2001_GCC",
    "-DPLAN2001_ARM64",
    "-Werror=implicit-function-declaration",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
    "-Werror=return-type",
]

INCLUDES = [
    ROOT / "kernel/arm64/gcc",
    ROOT / "kernel/arm64",
    ROOT / "kernel/core",
    ROOT / "include/arm64",
    ROOT / "include",
    ROOT,
]


def tool(name: str) -> str:
    p = shutil.which(name)
    if p is None:
        raise SystemExit(f"build-arm64: missing {name}")
    return p


def run(cmd: list[str], log: Path) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as f:
        f.write("$ " + " ".join(cmd) + "\n")
        f.flush()
        p = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    if p.returncode != 0:
        raise RuntimeError(f"command failed, see {log}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    args = ap.parse_args()
    jobs = max(1, args.jobs)

    clang = tool("clang")
    lld = tool("ld.lld")
    readelf = tool("llvm-readelf")
    objdump = tool("llvm-objdump")

    OBJ.mkdir(parents=True, exist_ok=True)
    LOG.mkdir(parents=True, exist_ok=True)
    for p in OBJ.glob("*.o"):
        p.unlink()
    if ELF.exists():
        ELF.unlink()

    inc = [f"-I{p}" for p in INCLUDES]

    def compile_one(item: tuple[str, str]) -> None:
        name, rel = item
        src = ROOT / rel
        if src.suffix == ".S":
            cmd = [clang, "--target=aarch64-none-elf", "-c", "-o",
                   str(OBJ / f"{name}.o"), str(src)]
        else:
            cmd = [clang] + CFLAGS + inc + ["-c", "-o",
                   str(OBJ / f"{name}.o"), str(src)]
        run(cmd, LOG / f"{name}.log")

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            list(ex.map(compile_one, SOURCES.items()))
        objs = [str(OBJ / f"{name}.o") for name in SOURCES]
        run([lld, "-static", "-nostdlib", "-T", str(LDSCRIPT), "-o",
             str(ELF)] + objs, LOG / "link.log")
    except RuntimeError as exc:
        print(f"build-arm64: FAIL: {exc}", file=sys.stderr)
        return 1

    hdr = subprocess.run([readelf, "-h", str(ELF)], capture_output=True,
                         text=True)
    prog = subprocess.run([readelf, "-l", str(ELF)], capture_output=True,
                          text=True)
    dis = subprocess.run([objdump, "-d", "--no-show-raw-insn", str(ELF)],
                         capture_output=True, text=True)
    text = hdr.stdout + prog.stdout + dis.stdout
    if "aarch64" not in hdr.stdout.lower():
        print("build-arm64: FAIL: not an AArch64 ELF", file=sys.stderr)
        return 1
    if "entry point address:" not in hdr.stdout.lower() or \
            "0xffffffffc0100000" not in hdr.stdout.lower():
        print("build-arm64: FAIL: unexpected entry point", file=sys.stderr)
        print(hdr.stdout, file=sys.stderr)
        return 1
    if "Type:" not in hdr.stdout or "EXEC" not in hdr.stdout:
        print("build-arm64: FAIL: not ET_EXEC", file=sys.stderr)
        return 1
    # every PT_LOAD must be a KZERO image: p_vaddr - p_paddr == KZERO
    loads = 0
    for ln in prog.stdout.splitlines():
        f = ln.split()
        if len(f) >= 6 and f[0] == "LOAD":
            loads += 1
            if int(f[2], 16) - int(f[3], 16) != 0xffffffff80000000:
                print("build-arm64: FAIL: load segment is not a KZERO image",
                      file=sys.stderr)
                return 1
    if loads == 0:
        print("build-arm64: FAIL: no PT_LOAD segment", file=sys.stderr)
        return 1
    if "_efi64" not in dis.stdout:
        print("build-arm64: FAIL: _efi64 not in disassembly", file=sys.stderr)
        return 1

    sha = subprocess.run(["sha256sum", str(ELF)], capture_output=True,
                         text=True).stdout.split()[0]
    (OUT / "build-report.txt").write_text(
        "Plan2001-O experimental ARM64 boot kernel\n"
        f"clang: {subprocess.run([clang, '--version'], capture_output=True, text=True).stdout.splitlines()[0]}\n"
        f"lld: {subprocess.run([lld, '--version'], capture_output=True, text=True).stdout.splitlines()[0]}\n"
        f"sources: {' '.join(SOURCES.values())}\n"
        f"output: {ELF.relative_to(ROOT)}\n"
        f"sha256: {sha}\n\n{hdr.stdout}{prog.stdout}")
    print(f"build-arm64: PASS {ELF.relative_to(ROOT)} sha256 {sha}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
