#!/usr/bin/env python3
"""Compare a Linux-host BusyBox *object symbol demand* with built Plan2001 libc.

This is an intentionally conservative ABI inventory, not a POSIX conformance
claim. The upstream BusyBox objects are built with Linux headers, so missing
symbols are candidates for porting work, not a target linker error count.
"""
from __future__ import annotations

import argparse
import ast
import json
import re
import subprocess
from pathlib import Path

HEADERS = (
    "assert.h", "ctype.h", "dirent.h", "errno.h", "fcntl.h",
    "limits.h", "locale.h", "pwd.h", "setjmp.h", "signal.h",
    "stdarg.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
    "string.h", "sys/stat.h", "sys/types.h", "sys/wait.h",
    "time.h", "unistd.h",
)

# These are investigation buckets, NOT claims about implemented semantics.
LIBC = {
    "abort", "atexit", "calloc", "exit", "free", "getenv", "malloc",
    "memcmp", "memcpy", "memmove", "memset", "printf", "putchar",
    "puts", "realloc", "snprintf", "sprintf", "strcat", "strchr",
    "strcmp", "strcpy", "strlen", "strncmp", "strncpy", "strrchr",
    "strstr", "strtol", "strtoul", "strtoull", "vsnprintf",
    "fprintf", "fputs", "fwrite", "fread", "fflush", "fclose",
    "fopen", "setvbuf",
}
NAMESPACE_IO = {
    "access", "chdir", "close", "dup", "dup2", "fchdir", "fcntl",
    "fstat", "getcwd", "isatty", "link", "lseek", "lstat", "mkdir",
    "open", "openat", "opendir", "read", "readdir", "readlink",
    "rename", "rmdir", "stat", "unlink", "write",
}
PROCESS = {
    "_exit", "execve", "fork", "getpid", "getppid", "kill",
    "pipe", "raise", "signal", "sigaction", "wait", "waitpid",
}
HOST_ABI_PREFIXES = ("__x86_", "__libc_", "__isoc99_", "__errno_location",
                     "__stack_chk_", "__fortify_", "__ctype_", "__assert_",
                     "_GLOBAL_OFFSET_TABLE_", "__tls_", "__pthread_")


def output(argv: list[str]) -> str:
    return subprocess.check_output(argv, text=True, stderr=subprocess.PIPE)


def symbols_from_nm(text: str, *, undefined: bool) -> set[str]:
    """Parse GNU nm -P -g output (object/archive headings are discarded)."""
    out = set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 2 or not re.fullmatch(r"[a-zA-Z?]", fields[1]):
            continue
        kind = fields[1]
        if (kind.upper() == "U") == undefined and kind not in ("w", "v"):
            out.add(fields[0])
    return out


def nm(paths: list[Path], flag: str, destination: Path) -> set[str]:
    raw = output(["nm", "-P", "-g", flag, *map(str, paths)])
    destination.write_text(raw, encoding="utf-8")
    return symbols_from_nm(raw, undefined=(flag == "--undefined-only"))


def classify(symbol: str) -> str:
    if symbol in LIBC:
        return "libc-runtime"
    if symbol in NAMESPACE_IO:
        return "userspace-namespace-or-io"
    if symbol in PROCESS:
        return "process-or-ipc-adapter"
    if symbol.startswith(HOST_ABI_PREFIXES) or "@GLIBC" in symbol:
        return "linux-host-toolchain-candidate"
    return "unclassified"


def built_source_manifest(kernel: Path) -> list[str]:
    """Read the literal LIBC_SOURCES list from the kernel's canonical builder."""
    path = kernel / "tools/host/build-mpy-gcc.py"
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "LIBC_SOURCES"
            for target in node.targets
        ):
            value = ast.literal_eval(node.value)
            if not isinstance(value, list) or not all(
                isinstance(item, str) for item in value
            ):
                raise RuntimeError("Kernel LIBC_SOURCES is not a list of paths")
            return value
    raise RuntimeError("Cannot find kernel build LIBC_SOURCES manifest")


def analyze(busybox_objects: set[str], target_symbols: set[str],
            host_defined: set[str]) -> dict:
    externals = busybox_objects - host_defined
    return {
        "host_busybox_external_references": sorted(externals),
        "in_plan2001_direct_runtime": sorted(externals & target_symbols),
        "not_in_plan2001_direct_runtime": sorted(externals - target_symbols),
        "gap_categories": {
            name: classify(name) for name in sorted(externals - target_symbols)
        },
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", type=Path, required=True)
    ap.add_argument("--busybox", type=Path, required=True)
    ap.add_argument("--reports", type=Path, required=True)
    args = ap.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)

    archive = args.kernel / "build/gcc/mpy/lib/libc-user.a"
    runtime_dir = args.kernel / "build/gcc/mpy/mpyobj"
    runtime = [runtime_dir / (name + ".o") for name in
               ("rt_startup", "rt_start", "rt_stubs", "rt_misc", "rt_fcr")]
    elf = args.reports / "native-true/busybox-true-plan2001.elf"
    required = [archive, *runtime, elf, args.busybox / ".config"]
    absent = [str(p) for p in required if not p.is_file()]
    if absent:
        raise SystemExit("Missing build prerequisites: " + ", ".join(absent))

    config = (args.busybox / ".config").read_text(encoding="utf-8")
    for enabled in ("CONFIG_STATIC=y", "CONFIG_TRUE=y",
                    "CONFIG_FALSE=y", "CONFIG_ECHO=y"):
        if enabled not in config.splitlines():
            raise SystemExit("Host BusyBox is not the pinned minimal control: "
                             + enabled)

    host_objects = sorted(p for p in args.busybox.rglob("*.o")
                          if "scripts" not in p.relative_to(args.busybox).parts
                          and ".git" not in p.relative_to(args.busybox).parts)
    if not host_objects:
        raise SystemExit("No host BusyBox .o files: run host_control.py first")

    libc_def = nm([archive], "--defined-only",
                  args.reports / "plan2001-libc-defined.nm.txt")
    runtime_def = nm(runtime, "--defined-only",
                     args.reports / "plan2001-startup-defined.nm.txt")
    busybox_def = nm(host_objects, "--defined-only",
                     args.reports / "busybox-host-defined.nm.txt")
    busybox_undef = nm(host_objects, "--undefined-only",
                       args.reports / "busybox-host-undefined.nm.txt")

    if not libc_def or not busybox_undef:
        raise SystemExit("Empty libc exports or host BusyBox references")
    report = analyze(busybox_undef, libc_def | runtime_def, busybox_def)

    src_dir = args.kernel / "runtime/libc"
    all_sources = {
        p.relative_to(src_dir).as_posix() for p in src_dir.rglob("*.c")
    }
    compiled_sources = built_source_manifest(args.kernel)
    missing_source_files = sorted(set(compiled_sources) - all_sources)
    if missing_source_files:
        raise SystemExit("Kernel libc manifest points at missing source: "
                         + ", ".join(missing_source_files))
    unused_sources = sorted(all_sources - set(compiled_sources))
    members = output(["ar", "t", str(archive)]).splitlines()
    if len(members) != len(compiled_sources):
        raise SystemExit(f"Archive members ({len(members)}) differ from "
                         f"kernel libc manifest ({len(compiled_sources)})")

    include_roots = [args.kernel / "include", args.kernel / "include/amd64",
                     args.kernel / "micropython/include"]
    headers = {
        name: [root.relative_to(args.kernel).as_posix()
               for root in include_roots if (root / name).is_file()]
        for name in HEADERS
    }

    elf_header = output(["readelf", "-hW", str(elf)])
    elf_segments = output(["readelf", "-lW", str(elf)])
    elf_dynamic = output(["readelf", "-dW", str(elf)])
    elf_undefined = output(["nm", "-u", str(elf)]).strip()
    target_valid = (
        bool(re.search(r"Type:\s+EXEC", elf_header))
        and "Advanced Micro Devices X86-64" in elf_header
        and "INTERP" not in elf_segments
        and "DYNAMIC" not in elf_segments
        and "(NEEDED)" not in elf_dynamic
        and not elf_undefined
    )
    if not target_valid:
        raise SystemExit("Native BusyBox true diagnostic ELF violates "
                         "static AMD64 target constraints")

    payload = {
        "schema": 1,
        "scope": "Linux-host BusyBox object references versus native "
                 "Plan2001 libc/startup exports; NOT a target BusyBox multicall link",
        "kernel_commit": output(
            ["git", "-C", str(args.kernel), "rev-parse", "HEAD"]).strip(),
        "busybox_commit": output(
            ["git", "-C", str(args.busybox), "rev-parse", "HEAD"]).strip(),
        "busybox_host_object_count": len(host_objects),
        "libc_archive_object_count": len(members),
        "libc_export_count": len(libc_def),
        "runtime_export_count": len(runtime_def),
        "libc_manifest": compiled_sources,
        "libc_sources_not_in_archive_manifest": unused_sources,
        "required_posix_headers": headers,
        "native_true_elf": {
            "static_amd64_et_exec": target_valid,
            "uses_kernel_libc_archive": "native_probe.py link command",
            "guest_execution": "NOT ASSESSED by symbol audit",
        },
        **report,
    }
    (args.reports / "libc-audit.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    gaps = payload["not_in_plan2001_direct_runtime"]
    available = payload["in_plan2001_direct_runtime"]
    unavailable_headers = [name for name, roots in headers.items() if not roots]
    lines = [
        "# Plan2001 libc / BusyBox ABI audit",
        "",
        f"- Kernel pin: {payload['kernel_commit']}",
        f"- BusyBox pin: {payload['busybox_commit']}",
        "- Plan2001 libc built as real target archive: **PASS**",
        "- Unmodified BusyBox true.c native static link: **PASS** "
        "(from native_probe.py; not multicall)",
        "- Native true diagnostic ELF static AMD64 inspection: **PASS**",
        "- Full BusyBox target build/link: **NOT RUN**",
        "- Guest BusyBox execution and POSIX semantics: **NOT ASSESSED**",
        "",
        "## Symbol-level comparison (diagnostic, not compatibility proof)",
        "",
        f"- BusyBox Linux-host object files inspected: {len(host_objects)}",
        f"- Plan2001 libc archive members: {len(members)}",
        f"- Plan2001 libc global exports: {len(libc_def)}",
        f"- BusyBox unresolved externals (after BusyBox-internal resolution): "
        f"{len(available) + len(gaps)}",
        f"- Names exported by Plan2001 libc/startup: {len(available)}",
        f"- Names absent from those exports: {len(gaps)}",
        "",
        "These BusyBox objects were built with **Linux headers and ABI**. "
        "Their references are a *candidate porting gap*, not a native "
        "link failure list. Name equality does not establish compatible "
        "types, calling conventions, errno behavior, object access, "
        "or security semantics. libgcc and other target support code "
        "are not included in the direct-runtime symbol set.",
        "",
        "## POSIX header names not found in selected kernel include roots",
        "",
        ", ".join(unavailable_headers) if unavailable_headers else
        "All audited header names are present (semantics still unchecked).",
        "",
        "## Kernel libc source files not selected by the native build manifest",
        "",
        *["- " + name for name in unused_sources],
        "",
        "## Direct-runtime symbol gaps (up to first 100 sorted names)",
        "",
        *["- " + sym + " | " + classify(sym) for sym in gaps[:100]],
        "",
        "Full header inventory, complete categorized gap list and "
        "input SHA values are in libc-audit.json. Raw nm snapshots "
        "are attached as separate text reports.",
        "",
    ]
    (args.reports / "libc-audit.md").write_text(
        "\n".join(lines), encoding="utf-8")
    print(f"Libc ABI symbol audit: PASS; host refs={len(available)+len(gaps)}, "
          f"direct-runtime candidates missing={len(gaps)}, "
          f"not-in-archive sources={len(unused_sources)}", flush=True)


if __name__ == "__main__":
    main()
