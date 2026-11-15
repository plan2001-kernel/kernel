#!/usr/bin/env python3
"""Build freestanding Plan2001 AMD64 BusyBox applet and native stream subset.

Builds ORIGINAL pinned upstream applet sources unchanged with a small,
explicitly scoped Plan2001 libbb/console adapter. This is not a complete
BusyBox make-based port nor a POSIX compatibility certification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

APPLETS = {"true": "coreutils/true.c", "false": "coreutils/false.c",
           "echo": "coreutils/echo.c"}
ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "ports" / "busybox"
UNIX = ROOT / "ports" / "unix"


def logged(command: list[str], log: Path) -> None:
    with log.open("a", encoding="utf-8") as stream:
        stream.write("$ " + shlex.join(map(str, command)) + "\n")
        stream.flush()
        p = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    if p.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-100:]),
              file=sys.stderr)
        raise RuntimeError(f"target command failed (exit {p.returncode}): "
                           f"{shlex.join(map(str, command))}")


def read(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT)


def check_elf(elf: Path, reports: Path, title: str) -> dict:
    header = read(["readelf", "-hW", str(elf)])
    phdr = read(["readelf", "-lW", str(elf)])
    dynamic = read(["readelf", "-dW", str(elf)])
    nm = read(["nm", str(elf)])
    undef = read(["nm", "-u", str(elf)]).strip()
    if not re.search(r"Type:\s+EXEC", header):
        raise RuntimeError(f"{title}: ELF must be ET_EXEC")
    if "Advanced Micro Devices X86-64" not in header:
        raise RuntimeError(f"{title}: expected AMD64")
    if "INTERP" in phdr or "DYNAMIC" in phdr or "(NEEDED)" in dynamic:
        raise RuntimeError(f"{title}: unexpected dynamic loader/dependency")
    if undef:
        raise RuntimeError(f"{title}: undefined symbols: {undef[:500]}")
    loads = re.findall(r"(?m)^\s*LOAD\s+", phdr)
    if len(loads) != 2:
        raise RuntimeError(f"{title}: expected two PT_LOAD, found {len(loads)}")
    symbols = (("_start", "p9_start", "uxsupervisor_run",
                "uxspawn_start", "p2001_session_try_read", "uxpipe_read")
               if title == "unix-supervisor-f5-plan2001" else
               ("_start", "p9_start", "plan2001_busybox_dispatch",
                "true_main", "false_main", "echo_main", "p2001_ls_main", "uxpipe_read",
                "uxpipe_write", "full_write",
                "obj_write"))
    for sym in symbols:
        if not re.search(rf"(?m)^[0-9a-f]+\s+[Tt]\s+{re.escape(sym)}$", nm):
            raise RuntimeError(f"{title}: missing symbol {sym}")
    if title == "unix-init-f5-plan2001":
        for sym in ("full_read", "p2001_session_read", "p2001_session_write",
                    "region_wait", "region_wake",
                    "pvd_bootstrap", "pvd_chdir", "pvd_getcwd",
                    "uxfs_list", "uxshell_eval", "uxexternal_ls",
                    "p2001_unix_init", "p2001_f5_shell"):
            if not re.search(rf"(?m)^[0-9a-f]+\s+[Tt]\s+{re.escape(sym)}$", nm):
                raise RuntimeError(f"{title}: missing console symbol {sym}")
    (reports / f"{title}.readelf.txt").write_text(
        header + "\n" + phdr + "\n" + dynamic
        + "\nUNDEFINED SYMBOLS:\n" + (undef or "(none)") + "\n",
        encoding="utf-8")
    data = elf.read_bytes()
    return {"file": str(elf), "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "arch": "AMD64", "type": "ET_EXEC",
            "pt_load": len(loads), "undefined_symbols": 0,
            "dynamic_interpreter": False, "linked_applets": list(APPLETS)}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", type=Path, required=True)
    ap.add_argument("--busybox", type=Path, required=True)
    ap.add_argument("--reports", type=Path, required=True)
    ap.add_argument("--cc", default="gcc-14")
    args = ap.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)
    build = args.reports / "busybox-target"
    build.mkdir(parents=True, exist_ok=True)
    log = args.reports / "busybox-target-build.log"
    report = args.reports / "busybox-target-build.md"
    runtime = args.kernel / "build/gcc/mpy/mpyobj"
    archive = args.kernel / "build/gcc/mpy/lib/libc-user.a"
    linker = args.kernel / "micropython/gcc/user.ld"
    originals = [args.busybox / path for path in APPLETS.values()]
    inputs = [archive, linker, PORT / "libbb.h", PORT / "compat.c", PORT / "session.c",
              PORT / "main.c", PORT / "ls_child.c", PORT / "cat_child.c", PORT / "uxpipe_child.c", PORT / "pvd_cli.c", PORT / "pvd_cli.h",
              PORT / "f5_shell.c", PORT / "f5_shell.h",
              UNIX / "supervisor.c", UNIX / "supervisor.h",
              UNIX / "supervisor_main.c",
              UNIX / "pvd.c", UNIX / "pvd.h", UNIX / "init.c",
              UNIX / "init.h", UNIX / "uxfs.c", UNIX / "uxfs.h",
              UNIX / "uxgeo.c", UNIX / "uxgeo.h",
              UNIX / "uxservice.c", UNIX / "uxservice.h",
              UNIX / "uxshell.c", UNIX / "uxshell.h",
              UNIX / "uxspawn.c", UNIX / "uxspawn.h",
              UNIX / "uxexternal.c", UNIX / "uxexternal.h",
              UNIX / "uxpipe.c", UNIX / "uxpipe.h",
              UNIX / "uxrpc.c", UNIX / "uxrpc.h",
              UNIX / "uxrpc_external.c", UNIX / "uxrpc_external.h",
              UNIX / "uxrpc_child.c", UNIX / "uxrpc_child.h",
              UNIX / "uxfd.c", UNIX / "uxfd.h",
              runtime / "rt_startup.o",
              runtime / "rt_start.o", runtime / "rt_stubs.o",
              runtime / "rt_misc.o", runtime / "libm_frexp.o",
              runtime / "libm_ldexp.o", runtime / "libm_scalbn.o", *originals]
    absent = [str(p) for p in inputs if not p.is_file()]
    if absent:
        raise SystemExit("Missing pinned input(s): " + ", ".join(absent))

    common = [args.cc, "-std=gnu11", "-m64", "-O2", "-ffreestanding",
              "-nostdinc", "-fno-builtin", "-fno-stack-protector",
              "-fno-pie", "-fno-pic", "-fno-strict-aliasing",
              "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
              "-fno-omit-frame-pointer", "-ffunction-sections",
              "-fdata-sections", "-Wall", "-Wextra", "-Werror",
              "-I", str(PORT), "-I", str(args.kernel / "runtime/abi")]

    objects = []
    info = {"kernel_commit": read(
        ["git", "-C", str(args.kernel), "rev-parse", "HEAD"]).strip(),
        "busybox_commit": read(
            ["git", "-C", str(args.busybox), "rev-parse", "HEAD"]).strip(),
        "applet_sources": list(APPLETS.values()),
        "native_compiler": read([args.cc, "--version"]).splitlines()[0],
        "libc": str(archive),
        "method": "gcc freestanding upstream 3 applets plus Plan2001 ls/pipe",
        "full_upstream_busybox_make": "NOT RUN",
        "linux_host_libc_used_for_target": False,
        "qemu_result": "NOT ASSESSED by build script"}
    try:
        for name, source in {**APPLETS, "ls_child": str(PORT / "ls_child.c"),
                             "cat_child": str(PORT / "cat_child.c"),
                             "uxpipe_child": str(PORT / "uxpipe_child.c"),
                             "uxpipe": str(UNIX / "uxpipe.c"),
                             "compat": str(PORT / "compat.c"),
                             "session": str(PORT / "session.c")}.items():
            path = (args.busybox / source if name in APPLETS else Path(source))
            obj = build / f"{name}.o"
            logged(common + ["-c", str(path), "-o", str(obj)], log)
            objects.append(obj)

        pvd_objects = []
        for name, source in (("pvd", UNIX / "pvd.c"),
                             ("pvd_cli", PORT / "pvd_cli.c"),
                             ("unix_init", UNIX / "init.c"),
                             ("f5_shell", PORT / "f5_shell.c"),
                             ("uxfs", UNIX / "uxfs.c"),
                             ("uxgeo", UNIX / "uxgeo.c"),
                             ("uxshell", UNIX / "uxshell.c"),
                             ("uxservice", UNIX / "uxservice.c"),
                             ("uxspawn", UNIX / "uxspawn.c"),
                             ("uxexternal", UNIX / "uxexternal.c"),
                             ("uxrpc", UNIX / "uxrpc.c"),
                             ("uxrpc_external", UNIX / "uxrpc_external.c"),
                             ("uxrpc_child", UNIX / "uxrpc_child.c"),
                             ("uxfd", UNIX / "uxfd.c"),
                             ("supervisor", UNIX / "supervisor.c")):
            obj = build / f"{name}.o"
            logged(common + ["-c", str(source), "-o", str(obj)], log)
            pvd_objects.append(obj)

        for label, mode in (("busybox-plan2001", "standard"),
                            ("busybox-smoke-plan2001", "smoke"),
                            ("unix-init-f5-plan2001", "f5"),
                            ("unix-supervisor-f5-plan2001", "supervisor")):
            launcher = build / ("main-" + mode + ".o")
            flags = (["-DPLAN2001_BUSYBOX_GUEST_SMOKE=1"] if mode == "smoke"
                     else ["-DPLAN2001_BUSYBOX_F5=1"] if mode == "f5" else [])
            logged(common + flags + ["-c", str(UNIX / "supervisor_main.c"
                                                    if mode == "supervisor"
                                                    else PORT / "main.c"),
                                     "-o", str(launcher)], log)
            elf = build / (label + ".elf")
            link = [
                args.cc, "-nostdlib", "-nostartfiles", "-static", "-no-pie",
                "-Wl,-T," + str(linker), "-Wl,-e,_start",
                "-Wl,--no-dynamic-linker", "-Wl,-z,noexecstack",
                "-Wl,--gc-sections", "-Wl,--build-id=none",
                "-o", str(elf), str(runtime / "rt_startup.o"),
                str(runtime / "rt_start.o"), str(runtime / "rt_misc.o"),
                str(launcher),
                *(map(str, [build / "session.o", build / "uxpipe.o"])
                  if mode == "supervisor" else map(str, objects)),
                *(map(str, pvd_objects) if mode == "f5" else
                  map(str, [build / "uxspawn.o", build / "supervisor.o"])
                  if mode == "supervisor" else ()),
                str(runtime / "rt_stubs.o"),
                str(runtime / "libm_frexp.o"),
                str(runtime / "libm_ldexp.o"),
                str(runtime / "libm_scalbn.o"),
                str(archive), "-lgcc",
            ]
            logged(link, log)
            info[label] = check_elf(elf, args.reports, label)

        (args.reports / "busybox-target-build.json").write_text(
            json.dumps(info, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        report.write_text(
            "# BusyBox native AMD64 build\n\n"
            "- **PASS:** upstream BusyBox 1.36.1 `true.c`, `false.c`, "
            "and `echo.c` compiled unchanged with freestanding GCC.\n"
            "- **PASS:** linked a limited Plan2001 native multicall ELF "
            "a guest-smoke ELF, the F5 unix-init/shell ELF and the trusted "
            "F5 Unix supervisor ELF using native libc, "
            "native startup/syscalls and libgcc.\n"
            "- **PASS:** static AMD64 ELF checks, no dynamic interpreter, "
            "no undefined symbols and exactly two PT_LOAD segments.\n"
            "- **ADD-ONS:** Plan2001-specific native ls and uxpipe sender/receiver "
            "using explicit capabilities, NOT original BusyBox ls/ash.\\n"
            "- **NOT RUN:** complete upstream BusyBox Makefile/libbb port, "
            "full POSIX semantics or all BusyBox applets.\n"
            "- **NOT RUN here:** QEMU guest execution (separate step).\n"
            "\nFour target ELF images are stored in the **runner scratch "
            "build tree** and are not shipped as Linux binaries. The "
            "source files and BSD/GPL provenance remain pinned.\n",
            encoding="utf-8")
        print("Native freestanding BusyBox true/false/echo subset: "
              "static ET_EXEC link PASS", flush=True)
    except Exception as exc:
        report.write_text(
            "# BusyBox native AMD64 build\n\n"
            f"**FAIL:** {type(exc).__name__}: {exc}\n"
            "QEMU guest execution: **NOT RUN**.\n",
            encoding="utf-8")
        raise


if __name__ == "__main__":
    main()
