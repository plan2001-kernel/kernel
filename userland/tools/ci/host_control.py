#!/usr/bin/env python3
"""Build a *Linux* static BusyBox control; do not confuse it with the target port."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

OPTIONS = ("STATIC", "TRUE", "FALSE", "ECHO")


def logged(cmd, cwd, log):
    with log.open("a", encoding="utf-8") as stream:
        stream.write(f"$ {' '.join(map(str, cmd))}\n")
        stream.flush()
        result = subprocess.run(cmd, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        tail = log.read_text(encoding="utf-8", errors="replace").splitlines()[-65:]
        print("\n".join(tail), file=sys.stderr)
        raise RuntimeError(f"host control command failed: {' '.join(cmd)}")
    return result


def configure(busybox, log):
    logged(["make", "allnoconfig"], busybox, log)
    config_path = busybox / ".config"
    config = config_path.read_text(encoding="utf-8")
    for name in OPTIONS:
        pattern = re.compile(rf"(?m)^(?:# CONFIG_{name} is not set|CONFIG_{name}=.*)$")
        config, count = pattern.subn(f"CONFIG_{name}=y", config, count=1)
        if count != 1:
            raise RuntimeError(f"BusyBox config symbol missing: {name}")
    config_path.write_text(config, encoding="utf-8")
    logged(["make", "oldconfig"], busybox, log)
    actual = config_path.read_text(encoding="utf-8")
    for name in OPTIONS:
        if f"CONFIG_{name}=y" not in actual.splitlines():
            raise RuntimeError(f"BusyBox config rejected CONFIG_{name}=y")
    return config_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--busybox", type=Path, required=True)
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)
    log = args.reports / "host-build.log"
    report = args.reports / "host-control.md"
    try:
        config = configure(args.busybox, log)
        (args.reports / "host-control.config").write_bytes(config.read_bytes())
        logged(["make", "-j2", "busybox"], args.busybox, log)
        elf = args.busybox / "busybox"
        phdr = subprocess.check_output(["readelf", "-lW", str(elf)], text=True)
        dynamic = subprocess.check_output(["readelf", "-dW", str(elf)], text=True)
        file_info = subprocess.check_output(["file", str(elf)], text=True).strip()
        (args.reports / "host-control.elf.txt").write_text(
            file_info + "\n\n" + phdr + "\n" + dynamic, encoding="utf-8"
        )
        if "INTERP" in phdr or "(NEEDED)" in dynamic:
            raise RuntimeError("Linux host control is not static")
        subprocess.run([str(elf), "true"], check=True)
        failed = subprocess.run([str(elf), "false"], check=False)
        if failed.returncode != 1:
            raise RuntimeError(f"busybox false returned {failed.returncode}, expected 1")
        echo = subprocess.check_output(
            [str(elf), "echo", "plan2001-host-control"], text=True
        ).strip()
        if echo != "plan2001-host-control":
            raise RuntimeError(f"busybox echo mismatch: {echo!r}")
        report.write_text(
            "## Linux host-control BusyBox (NOT the Plan2001 port)\n\n"
            "- Static compilation and ELF check: **PASS (Linux host libc)**\n"
            "- Applet smoke tests: **PASS (true, false, echo on Linux)**\n"
            "- Plan2001 libc link: **NOT RUN**\n"
            "- Plan2001 QEMU execution: **NOT RUN**\n"
            "- Configuration: \`host-control.config\` artifact\n"
            "- ELF inspection: \`host-control.elf.txt\` artifact\n",
            encoding="utf-8"
        )
        print("Host-control BusyBox: static ELF and 3 applet smoke tests PASS", flush=True)
    except Exception as exc:
        report.write_text(
            "## Linux host-control BusyBox\n\n"
            f"Host control: **FAIL** ({type(exc).__name__}: {exc})\n\n"
            "Plan2001 target link and execution: **NOT RUN**.\n",
            encoding="utf-8"
        )
        raise


if __name__ == "__main__":
    main()
