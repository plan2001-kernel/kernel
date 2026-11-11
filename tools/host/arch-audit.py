#!/usr/bin/env python3
"""Negative architecture audit for the Phase 6 replacement.

The audit fails when removed generic IPC or kernel L3 code reappears in
source, the build manifest, generated syscall tables or linked ELF symbols.
A narrow allowlist covers durable historical prose and the negative tests
that deliberately name the removed mechanisms.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
KERNEL = ROOT / "build/gcc/9pc64.elf"
MPY = ROOT / "build/gcc/micropython.elf"

TEXT_SUFFIXES = {".c", ".h", ".s", ".S", ".py", ".sh", ".mk", ".txt", ".tsv"}
SCAN_ROOTS = ("kernel", "runtime", "micropython", "include", "boot", "ge",
              "tools")
ALLOW_PREFIXES = (
    "docs/",
    "AGENTS.md",
    "README.md",
    "tools/host/micropython-ipc/",
    "tools/host/console-sessions/",
    "tools/host/ethernet-frames/",
    "tools/host/arch-audit.py",
    "tools/host/run-phase6-architecture-checks.sh",
)
ALLOW_SUFFIXES = (".md",)

FORBIDDEN = {
    "ObjectFIFO": "generic message FIFO",
    "VkindFifo": "FIFO object kind",
    "VkindPipe": "Pipe object kind",
    "ObjKindFifo": "FIFO create kind",
    "ObjKindPipe": "Pipe create kind",
    "ObjOpenPipe": "Pipe open provider",
    "ObjOpStat": "FIFO statistics operation",
    "ObjOpSend": "FIFO send operation",
    "ObjOpRecv": "FIFO receive operation",
    "ObjOpSubscribe": "FIFO subscription operation",
    "ObjOpPipePeer": "Pipe peer operation",
    "ObjOpFifoShutdown": "FIFO shutdown operation",
    "VobjFifostat": "FIFO statistics ABI",
    "vobjfifo": "FIFO provider symbol",
    "vobjpipe": "Pipe provider symbol",
    "pipecore": "Pipe provider source",
    "obj_fifo": "FIFO libc adapter",
    "obj_pipe_": "Pipe libc adapter",
    "obj_publish": "FIFO publish adapter",
    "obj_subscribe": "FIFO subscribe adapter",
    "obj_receive": "FIFO receive adapter",
    "PIPENEW": "retired Pipe syscall",
    "PIPEREAD": "retired Pipe syscall",
    "PIPEWRITE": "retired Pipe syscall",
    "PIPECLOSE": "retired Pipe syscall",
    "PIPEINHERIT": "retired Pipe syscall",
    "ipv6native": "kernel IPv6 boot/provider",
    "ipfsinit": "kernel L3 stack init",
    "ipiput6": "kernel IPv6 input",
    "espctl": "kernel ESP service",
    "esp6_": "kernel ESP datapath",
    "espcontrol": "kernel ESP control",
    "nativev6": "kernel IPv6 provider",
    "ip6only": "kernel IPv6 stack",
    "ipv6only": "kernel IPv6 stack",
    "ipifcinit": "kernel L3 interface init",
    "arpinit": "kernel ARP init",
    "icmp6init": "kernel ICMPv6 init",
    "ethernative": "kernel L3 Ethernet medium",
    "loopbackmedium": "kernel L3 medium",
    "nullmedium": "kernel L3 medium",
    "netlog": "kernel L3 log",
}

# The build manifest is not a suffix scan target; include it explicitly.
EXTRA_FILES = (ROOT / "tools/host/build-kernel.py",)


def allowed(path: pathlib.Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith(".agent-work/"):
        return True
    if any(rel.startswith(p) for p in ALLOW_PREFIXES):
        return True
    return False


def source_files():
    for root in SCAN_ROOTS:
        base = ROOT / root
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or "__pycache__" in path.parts:
                continue
            if path.suffix not in TEXT_SUFFIXES and path.name not in (
                    "Makefile", "pc64", "qemu"):
                continue
            if allowed(path):
                continue
            yield path
    for path in EXTRA_FILES:
        if path.is_file() and not allowed(path):
            yield path


def scan_text(text: str):
    hits = []
    for token, what in FORBIDDEN.items():
        for m in re.finditer(re.escape(token), text):
            line = text.count("\n", 0, m.start()) + 1
            hits.append((token, what, line))
    return hits


def scan_sources():
    hits = []
    for path in source_files():
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        for token, what, line in scan_text(text):
            hits.append(f"{path.relative_to(ROOT)}:{line}: {token} ({what})")
    return hits


def scan_build_inputs():
    hits = []
    gen = ROOT / "build/gcc/gen"
    if gen.is_dir():
        for path in gen.iterdir():
            if not path.is_file():
                continue
            for token, what, line in scan_text(path.read_text(errors="replace")):
                hits.append(f"{path.relative_to(ROOT)}:{line}: {token} ({what})")
    return hits


def scan_symbols():
    hits = []
    for elf in (KERNEL, MPY):
        if not elf.is_file():
            hits.append(f"missing build artifact: {elf.relative_to(ROOT)}")
            continue
        out = subprocess.run(["nm", "-a", str(elf)], capture_output=True,
                             text=True, check=False)
        if out.returncode != 0:
            hits.append(f"nm failed for {elf.relative_to(ROOT)}")
            continue
        for line in out.stdout.splitlines():
            for token, what in FORBIDDEN.items():
                if token.lower() in line.lower():
                    hits.append(f"{elf.relative_to(ROOT)} symbol: {line.strip()} ({what})")
    return hits


def selftest():
    """Prove the checks detect deliberate reintroduction, then discard it."""
    samples = ("int ObjKindFifo;", "void ipv6native_boot_start(void);")
    for sample in samples:
        if not scan_text(sample):
            raise SystemExit("arch-audit selftest failed: " + sample)
    print("arch-audit selftest: forbidden ObjectFIFO and IPv6 fixtures detected")
    return 0


def main() -> int:
    if "--selftest" in sys.argv:
        return selftest()
    hits = scan_sources() + scan_build_inputs() + scan_symbols()
    if hits:
        for hit in hits:
            print("FAIL", hit, file=sys.stderr)
        print("arch-audit: %d forbidden reference(s)" % len(hits), file=sys.stderr)
        return 1
    print("arch-audit: PASS (no forbidden FIFO/Pipe or kernel L3 code)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
