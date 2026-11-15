#!/usr/bin/env python3
"""Link full upstream Newlib libc.a/libm.a into native Plan2001 user ELFs.

Hard requirement: no Plan9 libc-user.a appears in link command, artifacts
or symbol providers. Kernel startup and syscall-only stubs are permitted.
"""
import argparse
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "ports/newlib/full"


def run(cmd, log):
    with log.open("a") as stream:
        stream.write("$ " + shlex.join(map(str, cmd)) + "\n")
        stream.flush()
        p = subprocess.run(cmd, stdout=stream, stderr=subprocess.STDOUT)
    if p.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-100:]))
        raise RuntimeError("build failed: " + shlex.join(map(str, cmd)))


def captured(*cmd):
    return subprocess.check_output(cmd, text=True)


def audit(elf, build, required):
    header = captured("readelf", "-hW", str(elf))
    load = captured("readelf", "-lW", str(elf))
    dyn = captured("readelf", "-dW", str(elf))
    defined = captured("nm", "--defined-only", str(elf))
    unresolved = captured("nm", "-u", str(elf)).strip()
    if not re.search(r"Type:\s+EXEC", header):
        raise RuntimeError("not static ET_EXEC: " + str(elf))
    if "Advanced Micro Devices X86-64" not in header:
        raise RuntimeError("not AMD64 target")
    if "INTERP" in load or "DYNAMIC" in load or "(NEEDED)" in dyn or unresolved:
        raise RuntimeError("unexpected dynamic or unresolved symbols: " + unresolved[:800])
    if len(re.findall(r"(?m)^\s*LOAD\s+", load)) != 2:
        raise RuntimeError("expected exactly two Plan2001 PT_LOAD segments")
    for sym in required:
        if not re.search(r"(?m)^[0-9a-f]+\s+[TtBbDdWw]\s+" +
                         re.escape(sym) + r"$", defined):
            raise RuntimeError("missing target symbol: " + sym + "\nnearby nm lines: " + repr([line for line in defined.splitlines() if sym.lstrip("_") in line][-20:]))
    (build / (elf.stem + ".audit.txt")).write_text(
        header + "\n" + load + "\n" + dyn + "\n" + defined)
    return {"elf": elf.name, "bytes": elf.stat().st_size,
            "sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
            "required_symbols": required}


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--kernel", type=Path, required=True)
    a.add_argument("--reports", type=Path, required=True)
    a.add_argument("--cc", default="gcc-14")
    args = a.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)
    build = args.reports / "newlib-target"
    build.mkdir(exist_ok=True)
    log = args.reports / "newlib-target-build.log"
    report = args.reports / "newlib-target-build.md"
    newlib = args.reports / "newlib-full"
    include = newlib / "include"
    libc = newlib / "libc.a"
    libm = newlib / "libm.a"
    runtime = args.kernel / "build/gcc/mpy/mpyobj"
    startup = runtime / "rt_startup.o"
    stubs = runtime / "rt_stubs.o"
    sources = {
        "syscalls": PORT / "plan2001_syscalls.c",
        "posix": PORT / "posix_forward.c",
        "stdio_process": PORT / "stdio_process.c",
        "fd_table": PORT / "fd_table.c",
        "process": PORT / "process.c",
        "inherit": PORT / "inherit.c",
        "file_state": PORT / "file_state.c",
        "dirent": ROOT / "ports/newlib/ash/dirent.c",
        "ash_posix": ROOT / "ports/newlib/ash/ash_posix.c",
        "pipe": ROOT / "ports/unix/uxpipe.c",
        "pvd": ROOT / "ports/unix/pvd.c",
        "uxfs": ROOT / "ports/unix/uxfs.c",
        "uxshell": ROOT / "ports/unix/uxshell.c",
        "uxservice": ROOT / "ports/unix/uxservice.c",
        "uxfd": ROOT / "ports/unix/uxfd.c",
        "startup": PORT / "p9_newlib_start.c",
        "boot": PORT / "newlib_stdio_probe.c",
        "child": PORT / "newlib_exit_child.c",
        "supervised": PORT / "newlib_supervised_child.c",
        "process_probe": PORT / "newlib_process_probe.c",
    }
    inputs = [libc, libm, include / "stdio.h", startup, stubs, *sources.values(),
              PORT / "user-newlib.ld"]
    missing = [str(f) for f in inputs if not f.exists()]
    if missing:
        raise SystemExit("missing inputs: " + ", ".join(missing))
    # Do not accidentally resolve any symbols from host /usr/include/glibc.
    gcc_includes = captured(args.cc, "-print-file-name=include").strip()
    cflags = [
        args.cc, "-m64", "-O2", "-std=gnu11", "-ffreestanding", "-nostdinc",
        "-fno-builtin", "-fno-stack-protector", "-fno-pie", "-fno-pic",
        "-fno-strict-aliasing", "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables", "-fno-omit-frame-pointer",
        "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "ports/newlib/ash/include"),
        "-I", str(PORT), "-I", str(args.kernel / "runtime/abi"),
        "-I", str(args.kernel / "include/amd64"),
        "-isystem", str(include), "-isystem", gcc_includes,
    ]
    try:
        objects = {}
        for key, source in sources.items():
            path = build / (key + ".o")
            flags = (["-DPLAN2001_UXPIPE_NEWLIB=1"] if key == "pipe" else [])
            run(cflags + flags + ["-c", str(source), "-o", str(path)], log)
            objects[key] = path
        # Native SLEEP and ALARM are millisecond Plan9 syscalls, not POSIX
        # sleep()/alarm(). Rename those two entries in a disposable copy
        # before linking Newlib, then provide a genuine POSIX sleep wrapper.
        native_stubs = build / "rt_stubs-newlib.o"
        run(["objcopy", "--redefine-sym", "sleep=p9_native_sleep_millis",
             "--redefine-sym", "alarm=p9_native_alarm_millis",
             str(stubs), str(native_stubs)], log)
        audits = []
        for kind, program in (("boot", "boot"), ("exit", "child"),
                              ("supervised", "supervised"), ("process", "process_probe")):
            elf = build / ("newlib-" + kind + ".elf")
            link = [
                args.cc, "-m64", "-nostdlib", "-nostartfiles",
                "-static", "-no-pie", "-Wl,-T," + str(PORT / "user-newlib.ld"),
                "-Wl,-e,_start", "-Wl,--no-dynamic-linker",
                "-Wl,-z,noexecstack", "-Wl,--gc-sections",
                "-Wl,--build-id=none", "-o", str(elf),
                "-Wl,--wrap=longjmp",
                str(startup), str(objects["startup"]), str(objects[program]),
                str(objects["syscalls"]), str(objects["process"]), str(objects["inherit"]), str(objects["file_state"]), str(objects["fd_table"]), str(objects["posix"]), str(objects["stdio_process"]), str(objects["dirent"]),
                str(objects["ash_posix"]), str(objects["pipe"]),
                *(str(objects[key]) for key in ("pvd", "uxfs", "uxshell", "uxservice", "uxfd")),
                str(native_stubs),
                "-Wl,--start-group", str(libc), str(libm),
                "-lgcc", "-Wl,--end-group",
            ]
            if any("libc-user.a" in arg for arg in link):
                raise AssertionError("Plan9 legacy libc must NOT be linked")
            run(link, log)
            required = ["_start", "p9_start", "main", "_exit", "exit"]
            if kind == "boot":
                required += ["_read", "_write", "_sbrk", "uxpipe_bind", "p9_full_bind_uxpipe", "p9_full_bind_files", "read", "write",
                             "sbrk", "malloc", "free", "_impure_ptr",
                             "printf", "snprintf", "strtol", "sin"]
            if kind == "supervised":
                required += ["p9_full_bind_inherited_uxpipe", "read", "write",
                             "close", "fstat", "uxpipe_read", "uxpipe_write"]
            if kind == "process":
                required += ["fork", "execve", "waitpid", "sigaction", "sigprocmask",
                             "sigsuspend", "kill", "siglongjmp", "__wrap_longjmp",
                             "p9_fd_export", "p9_fd_import"]
            audits.append(audit(elf, build, required))
        (build / "result.json").write_text(
            json.dumps({
                "kernel_commit": captured("git", "-C", str(args.kernel),
                                          "rev-parse", "HEAD").strip(),
                "kernel_worktree_dirty": bool(captured("git", "-C", str(args.kernel),
                                                       "status", "--porcelain").strip()),
                "upstream_newlib_commit": "8ba4275b83ec27529f67e0d477611fa6d8d6e6bd",
                "newlib_libc_sha256": hashlib.sha256(libc.read_bytes()).hexdigest(),
                "plan2001_libc_user_linked": False,
                "real_newlib_libc_linked": True,
                "real_newlib_libm_linked": True,
                "native_sleep_alarm_symbols_renamed_to_avoid_posix_collision": True,
                "targets": audits,
                "qemu": "NOT RUN in this build stage",
            }, indent=2) + "\n")
        report.write_text(
            "### Genuine Newlib libc target link\n\n"
            "- PASS: four static Plan2001 native AMD64 ELFs linked with "
            "full upstream Newlib libc.a/libm.a.\n"
            "- PASS: NO Plan2001 libc-user.a; Newlib malloc/stdio/reentrancy "
            "resolved, no unresolved symbols or dynamic loader.\n"
            "- NOT RUN in this step: real Plan2001 QEMU execution.\n")
        print("Full upstream Newlib target link: PASS")
    except Exception as e:
        report.write_text("### Genuine Newlib libc target link\n\n"
                          "**FAIL:** " + str(e) + "\n")
        raise


if __name__ == "__main__":
    main()
