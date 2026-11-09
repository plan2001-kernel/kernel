#!/usr/bin/env python3
"""N9.6: Linux-hosted GCC build of the real Plan2001-O MicroPython userspace.

Produces build/gcc/micropython.elf, a freestanding ELF64 x86-64 ET_EXEC
for OEXECUTE.  It links the repository's actual MicroPython 1.27.0
sources, the Plan 9 libc compiled for user mode with GCC, the N9.6
SysV/Plan2001 syscall startup layer, and the required maths.  It never
uses 6c/6l, glibc, a dynamic interpreter or host syscalls in the target.

All intermediates live under build/gcc/mpy/.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys

from syscall_abi import syscall_entries

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
MPY = ROOT / "micropython"
BUILD = ROOT / "build/gcc/mpy"
LIB = BUILD / "lib"
OBJ = BUILD / "obj"
LOG = BUILD / "log"
GEN = BUILD / "gen"
GENHDR = BUILD / "genhdr"
STAMP = BUILD / "flags.sha256"
OUT = ROOT / "build/gcc/micropython.elf"
OBJEXECMAX = 8 * 1024 * 1024

CC = os.environ.get("CC", "gcc")
LD = os.environ.get("LD", "ld")

CFLAGS = [
    "-std=gnu11", "-m64", "-O2",
    "-ffreestanding", "-fno-builtin",
    "-fno-stack-protector", "-fno-pie", "-fno-pic",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-fcommon", "-fno-strict-aliasing", "-ffunction-sections",
    "-fdata-sections", "-fno-omit-frame-pointer",
    "-DPLAN2001_GCC", "-DPLAN2001_GCC_USER", "-D__PLAN9__", "-DNDEBUG",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-include", str(ROOT / "kernel/compat/plan2001-gcc.h"),
    "-I", str(MPY / "include"),
    "-I", str(BUILD),
    "-I", str(MPY),
    "-I", str(GENHDR),
    "-I", str(GEN),
    "-I", str(ROOT / "include/amd64"),
    "-I", str(ROOT / "include"),
    "-I", str(ROOT / "runtime/mp/port"),
    "-I", str(MPY / "lib/libm_dbl"),
    "-I", str(MPY / "lib/musl"),
    "-I", str(ROOT / "runtime/ikecrypto/bearssl/inc"),
    "-I", str(ROOT / "runtime/ikecrypto/bearssl/src"),
]
# Measurement-only: keep the normal user ELF free of trace checkpoints.
if os.environ.get("PLAN2001_SYSCALL_TRACE") == "1":
    CFLAGS.append("-DPLAN2001_SYSCALL_TRACE")


def run(cmd, log=None, cwd=ROOT, quiet=False):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                       errors="replace")
    if log is not None:
        pathlib.Path(log).write_text(shlex.join(cmd) + "\n\n" + p.stdout + p.stderr)
    if p.returncode != 0 and not quiet:
        print(f"FAIL: {shlex.join(cmd)}")
        print((p.stdout + p.stderr).strip()[:4000])
    return p


def mkvar(text: str, name: str) -> list[str]:
    m = re.search(rf"^{re.escape(name)}=\\\n((?:.*\\\n)*.*)$", text, re.M)
    if not m:
        return []
    body = m.group(1).replace("\\\n", " ")
    return [x for x in body.split() if not x.startswith("#")]


def gen_headers(reuse: bool, report: list[str]) -> None:
    """Generate qstr/module/root-pointer/version headers reproducibly."""
    if reuse and (GENHDR / "qstrdefs.generated.h").is_file():
        report.append("genhdr: reused build/gcc/mpy/genhdr")
        return
    GENHDR.mkdir(parents=True, exist_ok=True)
    src = BUILD / "gen-src"
    if src.exists():
        shutil.rmtree(src)
    shutil.copytree(MPY, src, symlinks=True)
    ok = True
    p = run(["make", "-C", str(src), "genhdr"], log=BUILD / "genhdr.log", quiet=True)
    if p.returncode != 0:
        ok = False
        report.append("genhdr: make genhdr failed; using checked-in headers")
    else:
        for f in ("qstrdefs.generated.h", "moduledefs.h", "root_pointers.h",
                  "mpversion.h"):
            shutil.copy2(src / "genhdr" / f, GENHDR / f)
        # A fixed build date keeps the linked image reproducible.
        v = (GENHDR / "mpversion.h").read_text()
        v = re.sub(r'#define MICROPY_BUILD_DATE ".*"',
                   '#define MICROPY_BUILD_DATE "2026-10-18"', v)
        (GENHDR / "mpversion.h").write_text(v)
        report.append("genhdr: make genhdr OK (build date normalized)")
    shutil.rmtree(src, ignore_errors=True)
    if not ok:
        for f in ("qstrdefs.generated.h", "moduledefs.h", "root_pointers.h",
                  "mpversion.h"):
            shutil.copy2(MPY / "genhdr" / f, GENHDR / f)


def gen_stubs() -> pathlib.Path:
    """One generic wrapper per syscall in sys.h.

    The kernel reads the number from RBP and the 64-bit argument slots
    from [user RSP+8].  SysV register arguments are spilled into those
    slots; RBP is preserved around the call.
    """
    GEN.mkdir(parents=True, exist_ok=True)
    lines = [".text"]
    seen = set()
    for name, num, reserved in syscall_entries():
        if reserved:
            continue
        sym = name.lower()
        if sym in ("exits", "nsec"):
            sym = "_" + sym
        if re.match(r"^_x[0-9]+$", sym):
            sym = "sysdeath"
        if sym in seen:
            continue
        seen.add(sym)
        lines += [
            f".globl {sym}", f".type {sym},@function", f"{sym}:",
            "\tsubq $56, %rsp",
            "\tmovq %rbp, 0(%rsp)",
            "\tmovq %rdi, 8(%rsp)",
            "\tmovq %rsi, 16(%rsp)",
            "\tmovq %rdx, 24(%rsp)",
            "\tmovq %rcx, 32(%rsp)",
            "\tmovq %r8, 40(%rsp)",
            f"\tmovq ${num}, %rbp",
            "\tsyscall",
            "\tmovq 0(%rsp), %rbp",
            "\taddq $56, %rsp",
            "\tret",
            f".size {sym}, .-{sym}",
        ]
    # Diagnostic trampoline used only by the ABI retirement tests.  It adds
    # no kernel entry point: the caller supplies the syscall number, exactly
    # as the generated per-call stubs do.  SysV args map to the same five
    # stack slots; rdi is the number, rsi..r9 are the up to five arguments.
    lines += [
        ".globl sysraw", ".type sysraw,@function", "sysraw:",
        "\tsubq $56, %rsp",
        "\tmovq %rbp, 0(%rsp)",
        "\tmovq %rsi, 8(%rsp)",
        "\tmovq %rdx, 16(%rsp)",
        "\tmovq %rcx, 24(%rsp)",
        "\tmovq %r8, 32(%rsp)",
        "\tmovq %r9, 40(%rsp)",
        "\tmovq %rdi, %rbp",
        "\tsyscall",
        "\tmovq 0(%rsp), %rbp",
        "\taddq $56, %rsp",
        "\tret",
        ".size sysraw, .-sysraw",
    ]
    lines.append('.section .note.GNU-stack,"",@progbits')
    path = GEN / "stubs.S"
    path.write_text("\n".join(lines) + "\n")
    return path


PYOFILES = [
    "argcheck", "bc", "binary", "builtinevex", "builtinhelp", "builtinimport",
    "compile", "cstack", "emitbc", "emitcommon", "emitglue", "formatfloat",
    "frozenmod", "gc", "lexer", "malloc", "map", "modarray", "modbuiltins",
    "modcmath", "modcollections", "moderrno", "modgc", "modio", "modmath",
    "modmicropython", "modstruct", "modsys", "modthread", "mpprint", "mpstate",
    "mpz", "nativeglue", "objarray", "objattrtuple", "objbool", "objboundmeth",
    "obj", "objcell", "objclosure", "objcode", "objcomplex", "objdeque",
    "objdict", "objenumerate", "objexcept", "objfilter", "objfloat", "objfun",
    "objgenerator", "objgetitemiter", "objint", "objint_longlong",
    "objint_mpz", "objlist", "objmap", "objmodule", "objnamedtuple", "objnone",
    "objobject", "objpolyiter", "objproperty", "objrange", "objreversed",
    "objringio", "objset", "objsingleton", "objslice", "objstr",
    "objstringio", "objstrunicode", "objtuple", "objtype", "objzip",
    "opmethods", "pairheap", "parse", "parsenumbase", "parsenum",
    "persistentcode", "profile", "pystack", "qstr", "reader", "repl",
    "ringbuf", "runtime", "runtime_utils", "scheduler", "scope", "sequence",
    "showbc", "smallint", "stackctrl", "stream", "unicode", "vm", "vstr",
    "warning", "nlr", "nlrsetjmp", "modjson",
]

LIBMOFILES = [
    "acos", "acosh", "asin", "asinh", "atan2", "atan", "atanh", "ceil",
    "__cos", "cos", "cosh", "erf", "expm1", "__expo2", "floor", "fmod",
    "__fpclassify", "frexp", "ldexp", "lgamma", "log10", "log1p", "modf",
    "nearbyint", "__rem_pio2", "__rem_pio2_large", "rint", "round", "scalbn",
    "__signbit", "__sin", "sin", "sinh", "__tan", "tan", "tanh", "tgamma",
    "trunc",
]

MUSLOFILES = [
    "musl_exp", "musl_exp_data", "musl_log", "musl_log_data", "musl_pow",
    "musl_pow_data", "musl___math_oflow", "musl___math_uflow",
    "musl___math_xflow", "musl___math_invalid", "musl___math_divzero",
]


def sources() -> list[tuple[str, pathlib.Path]]:
    py = PYOFILES
    libm = LIBMOFILES
    musl = [n[5:] if n.startswith("musl_") else n for n in MUSLOFILES]
    out: list[tuple[str, pathlib.Path]] = []
    for n in py:
        if n == "modjson":
            continue
        out.append((f"py_{n}", MPY / "py" / (n + ".c")))
    for n in ("main", "modplan9", "modcbor", "modaesgcm", "p9space", "p9image", "p9sha",
              "p9builtin", "p9math", "p9console", "p9hal"):
        out.append((n, MPY / (n + ".c")))
    out.append(("extmod_modjson", MPY / "extmod/modjson.c"))
    out.append(("cbor", ROOT / "runtime/cbor/cbor.c"))
    out.append(("oenv", ROOT / "runtime/cbor/oenv.c"))
    out.append(("readline", MPY / "shared/readline/readline.c"))
    for n in ("pyexec", "stdout_helpers", "gchelper_generic"):
        out.append((n, MPY / "shared/runtime" / (n + ".c")))
    for n in libm:
        out.append((f"libm_{n}", MPY / "lib/libm_dbl" / (n + ".c")))
    for n in musl:
        out.append((f"musl_{n}", MPY / "lib/musl" / (n + ".c")))
    # Same six allocation-free AES-GCM units already used by kernel libesp,
    # compiled independently into the MPY userspace ELF. No kernel crypto API.
    for rel in ("espcrypto.c", "bearssl/src/symcipher/aes_ct.c",
                "bearssl/src/symcipher/aes_ct_enc.c",
                "bearssl/src/symcipher/aes_ct_ctr.c",
                "bearssl/src/hash/ghash_ctmul.c", "bearssl/src/aead/gcm.c"):
        out.append(("gcm_" + pathlib.Path(rel).stem, ROOT / "runtime/ikecrypto" / rel))
    return out


LIBC_SOURCES = [
    "9sys/abort.c",
    "9sys/convD2M.c",
    "9sys/convM2D.c",
    "9sys/getenv.c",
    "9sys/getpid.c",
    "9sys/nsec.c",
    "9sys/privalloc.c",
    "9sys/rerrstr.c",
    "9sys/sbrk.c",
    "9sys/wait.c",
    "9sys/werrstr.c",
    "fmt/dofmt.c",
    "fmt/errfmt.c",
    "fmt/fltfmt.c",
    "fmt/fmt.c",
    "fmt/fmtlock.c",
    "fmt/snprint.c",
    "fmt/sprint.c",
    "fmt/vseprint.c",
    "fmt/vsnprint.c",
    "port/_assert.c",
    "port/atexit.c",
    "port/atnotify.c",
    "port/atol.c",
    "port/calloc.c",
    "port/ctype.c",
    "port/exits.c",
    "port/fabs.c",
    "port/getcallerpc.c",
    "port/lock.c",
    "port/malloc.c",
    "port/memccpy.c",
    "port/memcmp.c",
    "port/memmove.c",
    "port/memset.c",
    "port/muldiv.c",
    "port/nan.c",
    "port/oobject.c",
    "port/osysctl.c",
    "port/pool.c",
    "port/pow10.c",
    "port/qsort.c",
    "port/readn.c",
    "port/rune.c",
    "port/sqrt.c",
    "port/strcat.c",
    "port/strchr.c",
    "port/strcmp.c",
    "port/strcpy.c",
    "port/strcspn.c",
    "port/strdup.c",
    "port/strlen.c",
    "port/strncmp.c",
    "port/strrchr.c",
    "port/strspn.c",
    "port/strstr.c",
    "port/strtod.c",
    "port/strtol.c",
    "port/strtoll.c",
    "port/strtoull.c",
    "port/tokenize.c",
    "port/toupper.c",
    "port/utfecpy.c",
    "port/utflen.c",
    "port/utfrune.c",
]


def libc_sources() -> list[tuple[str, pathlib.Path]]:
    out: list[tuple[str, pathlib.Path]] = []
    for rel in LIBC_SOURCES:
        src = ROOT / "runtime/libc" / rel
        out.append(("libc_" + rel.replace("/", "_")[:-2], src))
    return out


def libsec_sources() -> list[tuple[str, pathlib.Path]]:
    return [
        ("libsec_sha2_64", ROOT / "runtime/crypto/port/sha2_64.c"),
        ("libsec_sha2block64", ROOT / "runtime/crypto/port/sha2block64.c"),
    ]


def gcc_runtime_sources() -> list[tuple[str, pathlib.Path, bool]]:
    # (name, source, is_asm)
    return [
        ("rt_startup", MPY / "gcc/startup.S", True),
        ("rt_start", MPY / "gcc/start.c", False),
        ("rt_setjmp", MPY / "gcc/setjmp.S", True),
        ("rt_misc", MPY / "gcc/misc.S", True),
        ("rt_fcr", MPY / "gcc/fcr.c", False),
    ]


def up_to_date(obj: pathlib.Path, src: pathlib.Path, stamp: str) -> bool:
    if not obj.is_file() or not STAMP.is_file():
        return False
    if STAMP.read_text().strip() != stamp:
        return False
    return obj.stat().st_mtime >= src.stat().st_mtime


def compile_one(item, outdir: pathlib.Path, stamp: str, asm: bool = False):
    name, src = item
    obj = outdir / (name + ".o")
    log = LOG / (name + ".log")
    if up_to_date(obj, src, stamp):
        return name, "CACHED", ""
    cmd = ([CC, "-c", "-o", str(obj), "-m64", "-fno-pie", str(src)]
           if asm else [CC, "-c", "-o", str(obj)] + CFLAGS + [str(src)])
    p = run(cmd, log=log, quiet=True)
    if p.returncode:
        return name, "FAIL", (p.stdout + p.stderr).strip().splitlines()[:3]
    return name, "PASS", ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-j", "--jobs", type=int,
                    default=int(os.environ.get("JOBS", "0")) or (os.cpu_count() or 1))
    ap.add_argument("--reuse-headers", action="store_true",
                    help="reuse build/gcc/mpy/genhdr instead of running make genhdr")
    ap.add_argument("--no-incremental", action="store_true")
    args = ap.parse_args()

    for d in (BUILD, LIB, OBJ, LOG, GEN, GENHDR):
        d.mkdir(parents=True, exist_ok=True)
    report: list[str] = []
    gen_headers(args.reuse_headers, report)
    stub_src = gen_stubs()

    # QSTR numeric IDs change when interfaces add names. Every consumer must
    # rebuild together; source timestamps alone can silently misbind modules.
    signature=hashlib.sha256("\n".join(CFLAGS).encode())
    headers=set(GENHDR.glob("*.h"))
    for base in (MPY,ROOT/"runtime/abi",ROOT/"runtime/cbor",ROOT/"runtime/ikecrypto"):
        headers.update(base.rglob("*.h"))
    for header in sorted(headers):
        signature.update(str(header.relative_to(ROOT)).encode())
        signature.update(header.read_bytes())
    stamp=signature.hexdigest()
    if args.no_incremental:
        STAMP.unlink(missing_ok=True)

    MPY_OBJ = BUILD / "mpyobj"
    MPY_OBJ.mkdir(parents=True, exist_ok=True)
    libc_items = libc_sources()
    libsec_items = libsec_sources()
    mpy_items = sources()
    gcc_items = gcc_runtime_sources()
    all_items: list[tuple[pathlib.Path, tuple[str, pathlib.Path], bool]] = []
    for item in libc_items:
        all_items.append((LIB, item, False))
    for item in libsec_items:
        all_items.append((LIB, item, False))
    for item in mpy_items:
        all_items.append((MPY_OBJ, item, False))
    for item in gcc_items:
        all_items.append((MPY_OBJ, (item[0], item[1]), item[2]))
    all_items.append((MPY_OBJ, ("rt_stubs", stub_src), True))

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(compile_one, item, outdir, stamp, asm)
                for outdir, item, asm in all_items]
        for f in concurrent.futures.as_completed(futs):
            results.append(f.result())
    bad = [r for r in results if r[1] == "FAIL"]
    for r in bad:
        print(f"COMPILE FAIL {r[0]}: {r[2]}")
    if bad:
        print(f"compile: {len(results)-len(bad)}/{len(results)} PASS")
        return 1
    STAMP.write_text(stamp + "\n")
    cached = sum(1 for r in results if r[1] == "CACHED")
    report.append(f"compile: {len(results)} units ({len(results)-cached} built, {cached} cached), 0 FAIL")

    abi = BUILD / "abi-check.o"
    p = run([CC, "-c", "-o", str(abi)] + CFLAGS + [str(MPY / "gcc/abi-check.c")],
            log=LOG / "abi-check.log")
    if p.returncode:
        return 1
    report.append("ABI static checks: PASS (widths, offsetof, GCC jmp_buf)")

    # Plan 9 libc archive: exactly this run's manifest members.
    libobjs = [str(LIB / (name + ".o")) for name, _ in libc_items]
    libsecobjs = [str(LIB / (name + ".o")) for name, _ in libsec_items]
    archive = LIB / "libc-user.a"
    archive.unlink(missing_ok=True)
    p = run(["ar", "rcs", str(archive)] + libobjs, log=LOG / "ar-libc.log")
    if p.returncode:
        return 1
    report.append(f"libc archive: {len(libobjs)} manifest objects")

    mpyobjs = sorted(str(outdir / (item[0] + ".o"))
                     for outdir, item, _ in all_items if outdir == MPY_OBJ)
    linkcmd = [
        CC, "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-Wl,-T," + str(MPY / "gcc/user.ld"),
        "-Wl,-e,_start", "-Wl,--no-dynamic-linker", "-Wl,-z,noexecstack",
        "-Wl,--gc-sections", "-Wl,--build-id=none",
        "-o", str(OUT),
    ] + mpyobjs + libsecobjs + [str(archive), "-lgcc"]
    p = run(linkcmd, log=BUILD / "link.log")
    if p.returncode:
        return 1

    # Strict target checks.
    checks = []
    if not OUT.is_file():
        checks.append("no output")
    else:
        h = OUT.read_bytes()
        hd = run(["readelf", "-h", "-l", "-d", str(OUT)])
        txt = hd.stdout
        checks.append(f"sha256: {hashlib.sha256(h).hexdigest()}")
        checks.append(f"size: {len(h)}")
        if "Type:" not in txt or "EXEC" not in txt:
            checks.append("NOT ET_EXEC")
        if "INTERP" in txt or "DYNAMIC" in txt:
            checks.append("dynamic/interp present")
        loads = [ln for ln in txt.splitlines() if ln.strip().startswith("LOAD")]
        checks.append(f"PT_LOAD count: {len(loads)}")
        if len(loads) != 2:
            checks.append("expected exactly 2 PT_LOAD")
        und = run(["nm", "-u", str(OUT)]).stdout.strip()
        checks.append("undefined symbols: " + (und if und else "none"))
        if und:
            checks.append("UNRESOLVED")
        if len(h) > OBJEXECMAX:
            checks.append("exceeds Objexecmax")
        entry_m = re.search(r"Entry point address:\s+0x([0-9a-fA-F]+)", txt)
        entry = int(entry_m.group(1), 16) if entry_m else 0
        checks.append(f"entry: {entry:#x}")
        if not (0x200000 <= entry < 0x7ffffffff000):
            checks.append("entry outside user range")
        if entry == 0:
            checks.append("no entry")
        (BUILD / "readelf.txt").write_text(txt)
        (BUILD / "nm-undef.txt").write_text(und + "\n")
        dis = run(["objdump", "-d", str(OUT)]).stdout
        (BUILD / "disasm.txt").write_text(dis)
        (BUILD / "nm.txt").write_text(run(["nm", str(OUT)]).stdout)
        if "and    $0xfffffffffffffff0,%rsp" not in dis and "andq   $0xfffffffffffffff0,%rsp" not in dis:
            checks.append("startup stack not 16-byte aligned")
        for sym, num in (("_start", None), ("setjmp", None), ("longjmp", None)):
            if not re.search(rf"<{re.escape(sym)}>:", dis):
                checks.append(f"missing {sym}")
        sysnums = {"sysctl": "0x90", "ioctl": "0x91", "objopen": "0x89",
                   "oexecute": "0x3c", "spawn": "0x92",
                   "procwait": "0x8e",
                   "errstr": "0x29", "_nsec": "0x35", "sleep": "0x11"}
        for sym, num in sysnums.items():
            if not re.search(rf"<{re.escape(sym)}>:", dis):
                checks.append(f"missing syscall stub {sym}")
            elif f"mov    $0x{int(num,16):x},%rbp" not in dis and                  f"movq   $0x{int(num,16):x},%rbp" not in dis:
                checks.append(f"syscall stub {sym} has no RBP={num}")
        # Migrated process-lifecycle adapters must be real C symbols, not
        # retired syscall stubs; their OBJOP path is checked in the disasm.
        realsyms = ("wait", "proc_wait", "obj_ospawn", "rfork")
        for sym in realsyms:
            if not re.search(rf"<{re.escape(sym)}>:", dis):
                checks.append(f"missing OBJ adapter {sym}")
        direct = {"_exits": "0x8", "oterminate": "0x5d",
                  "notify": "0x1c", "noted": "0x1d", "brk_": "0x18",
                  "sleep": "0x11", "rforkex": "0x8f"}
        for sym, num in direct.items():
            if not re.search(rf"<{re.escape(sym)}>:", dis):
                checks.append(f"missing direct syscall stub {sym}")
            elif (f"mov    $0x{int(num,16):x},%rbp" not in dis
                    and f"movq   $0x{int(num,16):x},%rbp" not in dis):
                checks.append(f"direct syscall stub {sym} has no RBP={num}")
        for sym in ("obj_rfork", "obj_oterminate", "obj_exit",
                    "obj_process_self_sleep"):
            if re.search(rf"<{re.escape(sym)}>:", dis):
                checks.append(f"retired OBJ adapter still present {sym}")
        # Every retired number must be absent from generated wrappers and
        # from syscall-bearing functions in the linked image.  Inspect each
        # function separately; unrelated RBP constants are not syscall use.
        blocks = re.split(r"(?m)^[0-9a-f]+ <([^>]+)>:\n", dis)
        retired = {number for _, number, reserved in syscall_entries() if reserved}
        for i in range(1, len(blocks), 2):
            sym, body = blocks[i:i+2]
            if "syscall" not in body or sym == "sysraw":
                continue
            for value in re.findall(r"movq?\s+\$(0x[0-9a-f]+),%rbp", body):
                if int(value, 16) in retired:
                    checks.append(f"retired syscall stub still present {sym}")
        if not any("missing syscall stub" in c or "no RBP" in c
                   or "missing OBJ adapter" in c
                   or "retired OBJ adapter still present" in c
                   or "retired syscall stub still present" in c for c in checks):
            checks.append("syscall stubs: direct RBP numbers, OBJ wait/ospawn adapters and _start alignment verified")
        badchecks = [c for c in checks if c in ("NOT ET_EXEC", "dynamic/interp present",
                                                "expected exactly 2 PT_LOAD",
                                                "UNRESOLVED", "exceeds Objexecmax",
                                                "entry outside user range", "no entry",
                                                "no output", "startup stack not 16-byte aligned")
                     or c.startswith("missing ") or "no RBP" in c
                     or "retired OBJ adapter still present" in c
                     or "retired syscall stub still present" in c]
        if badchecks:
            for c in badchecks:
                print("CHECK FAIL:", c)
            return 1
    report.extend(checks)
    (BUILD / "report.txt").write_text(
        "Plan2001-O N9.6 GCC MicroPython user ELF\n" + "=" * 44 + "\n"
        + "\n".join(report) + "\n")
    print("\n".join(report))
    print(f"output: {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
