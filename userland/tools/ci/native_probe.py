#!/usr/bin/env python3
"""Link BusyBox's *actual* true applet against the kernel's native C runtime.

This is a minimal single-applet link diagnostic, NOT a complete BusyBox port.
It reuses the exact Plan2001 freestanding startup, libc archive, syscall stubs
and linker script produced by the kernel's canonical MicroPython build.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path


def run_logged(command, log):
    with log.open("a", encoding="utf-8") as handle:
        handle.write("$ " + " ".join(map(str, command)) + "\n")
        handle.flush()
        completed = subprocess.run(
            command, stdout=handle, stderr=subprocess.STDOUT, check=False
        )
    if completed.returncode:
        tail = log.read_text(errors="replace").splitlines()[-80:]
        print("\n".join(tail), file=sys.stderr)
        raise RuntimeError(f"command failed ({completed.returncode}): {command[0]}")


def output(command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", type=Path, required=True)
    ap.add_argument("--busybox", type=Path, required=True)
    ap.add_argument("--reports", type=Path, required=True)
    ap.add_argument("--cc", default="gcc-14")
    args = ap.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)

    log = args.reports / "native-link.log"
    report = args.reports / "native-link.md"
    b = args.kernel / "build" / "gcc" / "mpy"
    archive = b / "lib" / "libc-user.a"
    runtime = b / "mpyobj"
    required = [
        archive, runtime / "rt_startup.o", runtime / "rt_start.o",
        runtime / "rt_stubs.o", args.kernel / "micropython/gcc/user.ld",
        args.busybox / "coreutils/true.c",
    ]
    missing = [str(x) for x in required if not x.is_file()]
    if missing:
        raise RuntimeError("Canonical kernel libc/user runtime is missing: "
                           + ", ".join(missing))

    # Keep build outputs separate from all source repositories.
    build = args.reports / "native-true"
    build.mkdir(exist_ok=True)
    shim = build / "libbb.h"
    shim.write_text(
        "/* Minimal BusyBox true.c diagnostic header, NOT the complete libbb. */\n"
        "#ifndef PLAN2001_MINI_LIBBB_H\n#define PLAN2001_MINI_LIBBB_H\n"
        "#define MAIN_EXTERNALLY_VISIBLE __attribute__((visibility(\"default\")))\n"
        "#define UNUSED_PARAM __attribute__((unused))\n"
        "#define EXIT_SUCCESS 0\n"
        "#endif\n",
        encoding="utf-8",
    )
    driver = build / "main.c"
    driver.write_text("""\
/* Test driver, not an upstream BusyBox source modification. */
#include "obj.h"
extern int true_main(int, char **);
extern long obj_open(unsigned long long, void *, long, unsigned long long, ObjId *);
extern long obj_write(int, void *, unsigned long long, unsigned long long);
extern long obj_close(int);
extern long sleep(long);

static char marker[] = "PLAN2001-USERLAND-BUSYBOX-TRUE-EXECUTED";

int main(int argc, char **argv)
{
    ObjOpenConsoleReq req = { 2, 0 };
    int h;
    if (true_main(argc, argv) != 0)
        return 11;
    h = (int)obj_open(ObjOpenConsole, &req, sizeof(req), 0, 0);
    if (h < 0) {
        req.stream = 1;
        h = (int)obj_open(ObjOpenConsole, &req, sizeof(req), 0, 0);
    }
    if (h < 0)
        return 12;
    if (obj_write(h, marker, sizeof(marker)-1, 0) != sizeof(marker)-1)
        return 13;
    obj_close(h);
    /* The kernel treats boot-owner process termination as fatal. */
    /* Keep this diagnostic boot-owner alive after true_main succeeded. */
    for (;;) (void)sleep(1000);
    return 0;
}
""", encoding="utf-8")
    common = [
        args.cc, "-std=gnu11", "-m64", "-O2", "-ffreestanding",
        "-fno-builtin", "-fno-stack-protector", "-fno-pie", "-fno-pic",
        "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections",
        "-Werror",
    ]
    true_obj = build / "busybox-true.o"
    main_obj = build / "main.o"
    try:
        run_logged(common + ["-I", str(build), "-c",
                             str(args.busybox / "coreutils/true.c"), "-o", str(true_obj)], log)
        run_logged(common + ["-I", str(args.kernel / "runtime/abi"),
                             "-c", str(driver), "-o", str(main_obj)], log)

        elf = build / "busybox-true-plan2001.elf"
        run_logged(
            [args.cc, "-nostdlib", "-nostartfiles", "-static", "-no-pie",
             "-Wl,-T," + str(args.kernel / "micropython/gcc/user.ld"),
             "-Wl,-e,_start", "-Wl,--no-dynamic-linker",
             "-Wl,-z,noexecstack", "-Wl,--gc-sections",
             "-Wl,--build-id=none", "-o", str(elf),
             str(runtime / "rt_startup.o"), str(runtime / "rt_start.o"),
             str(true_obj), str(main_obj), str(runtime / "rt_stubs.o"),
             str(archive), "-lgcc"],
            log,
        )
        header = output(["readelf", "-hW", str(elf)])
        programs = output(["readelf", "-lW", str(elf)])
        dynamic = output(["readelf", "-dW", str(elf)])
        symbols = output(["nm", "-u", str(elf)]).strip()
        defined = output(["nm", str(elf)])
        members = output(["ar", "t", str(archive)])
        (args.reports / "native-link-elf.txt").write_text(
            header + "\n" + programs + "\n" + dynamic
            + "\nUNDEFINED:\n" + symbols + "\n", encoding="utf-8"
        )

        if not re.search(r"Type:\s+EXEC", header):
            raise RuntimeError("ELF is not ET_EXEC")
        if "Advanced Micro Devices X86-64" not in header:
            raise RuntimeError("ELF machine is not AMD64")
        if "INTERP" in programs or "DYNAMIC" in programs or "(NEEDED)" in dynamic:
            raise RuntimeError("Unexpected dynamic ELF dependencies")
        if symbols:
            raise RuntimeError(f"Unresolved symbols: {symbols[:400]}")
        for required_symbol in ("true_main", "p9_start", "exits"):
            if not re.search(rf"(?m)^\S+\s+[Tt]\s+{required_symbol}$", defined):
                raise RuntimeError(f"Missing linked symbol: {required_symbol}")
        if not any("exits" in m for m in members.splitlines()):
            raise RuntimeError("Missing native libc exits archive member")

        report.write_text(
            "# Plan2001 libc native link acceptance\n\n"
            "- **PASS:** compiled the unmodified upstream BusyBox `coreutils/true.c` "
            "against a purpose-built minimal `libbb.h` diagnostic shim.\n"
            "- **PASS:** linked the applet using the kernel's actual "
            "`libc-user.a`, `rt_startup.o`, `rt_start.o`, "
            "`rt_stubs.o`, `micropython/gcc/user.ld`, and `libgcc`.\n"
            "- **PASS:** AMD64 ET_EXEC; no PT_INTERP/PT_DYNAMIC, "
            "shared libraries or undefined symbols.\n"
            "- **LIMITATION:** this is a **single BusyBox applet**, "
            "not the BusyBox multicall executable or a POSIX port.\n"
            "- **LIMITATION:** native QEMU guest applet execution "
            "is not demonstrated by the linker test.\n",
            encoding="utf-8",
        )
        print("Plan2001 libc + actual BusyBox true.c applet: static link PASS", flush=True)
    except Exception as exc:
        report.write_text(
            "# Plan2001 libc native link acceptance\n\n"
            f"**FAIL:** {type(exc).__name__}: {exc}\n",
            encoding="utf-8",
        )
        raise


if __name__ == "__main__":
    main()
