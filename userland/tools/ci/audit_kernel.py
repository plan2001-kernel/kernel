#!/usr/bin/env python3
"""Host-only native ABI and runtime source inventory, not a POSIX port test."""
import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)

    abi = args.kernel / "runtime" / "abi"
    libc = args.kernel / "runtime" / "libc"
    required = [
        abi / "sys.h", abi / "obj.h", abi / "objectspace.h",
        libc / "port", libc / "9sys",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise SystemExit("Missing pinned-kernel inputs: " + ", ".join(missing))

    # This tests that one native ABI header can be included by a host C compiler.
    # It deliberately does NOT link a Plan2001 executable or exercise its syscalls.
    probe = """#include "objectspace.h"
_Static_assert(ObjSpaceLabelMax == 48, "unexpected ObjectSpace label layout");
_Static_assert(ObjSpaceBindingMax == 64, "unexpected binding limit");
_Static_assert(sizeof(ObjSpaceBindReq) > 0, "missing ObjectSpace binding ABI");
int main(void) { return 0; }
"""
    with tempfile.TemporaryDirectory(prefix="p2001-abi-") as tmp:
        source = Path(tmp) / "abi_probe.c"
        source.write_text(probe, encoding="utf-8")
        subprocess.run(
            ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fsyntax-only", "-I", str(abi), str(source)],
            check=True,
        )

    kernel_sha = subprocess.check_output(
        ["git", "-C", str(args.kernel), "rev-parse", "HEAD"], text=True
    ).strip()
    sources = sorted(p.relative_to(args.kernel).as_posix()
                     for p in libc.rglob("*.c"))
    lines = [
        "# Plan2001 libc and native ABI host inventory",
        "",
        f"- Kernel commit: \`{kernel_sha}\`",
        "- Native \`runtime/abi/objectspace.h\` compile probe: **PASS (host syntax only)**",
        f"- Existing C runtime source files found: **{len(sources)}**",
        "- C runtime and POSIX link compatibility: **NOT RUN**",
        "- Native ABI runtime behavior: **NOT RUN**",
        "- No kernel sources were modified by this audit.",
        "",
        "## Existing runtime C sources (inventory only)",
        "",
    ]
    lines.extend(f"- \`{name}\`" for name in sources)
    (args.reports / "kernel-libc-inventory.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )
    for name in ("sys.h", "obj.h", "objectspace.h"):
        data = (abi / name).read_bytes()
        (args.reports / (name + ".sha256")).write_text(
            f"{hashlib.sha256(data).hexdigest()}  {name}\n", encoding="utf-8"
        )
    print(f"Kernel ABI host syntax: PASS; inventoried {len(sources)} C files", flush=True)


if __name__ == "__main__":
    main()
