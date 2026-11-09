#!/usr/bin/env python3
"""T0 syscall measurement: build/run the tracing kernel and classify calls.

Normal use:

    python3 tools/host/syscall-t0-measure.py --run

--run builds a measurement-only kernel and MicroPython ELF with
PLAN2001_SYSCALL_TRACE=1, boots the two sanctioned QEMU tools with disposable
images, copies the measured binaries, rebuilds the normal canonical artifacts
and performs one normal smoke boot.  Without --run the script only re-parses
the logs already under build/syscall-t0/.

The trace kernel stores dispatch records in a bounded buffer and flushes them
at SYSCTL snapshot checkpoints.  The normal build has no trace code.
"""

from __future__ import annotations

import argparse
import bisect
import collections
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTDIR = ROOT / "build/syscall-t0"
BINDIR = OUTDIR / "binaries"
LOGDIR = OUTDIR / "logs"
BOOT_LOG = OUTDIR / "boot/serial-boot.log"
PERSIST1 = OUTDIR / "persist/serial-boot1.log"
PERSIST2 = OUTDIR / "persist/serial-boot2.log"
NORMAL_SMOKE = OUTDIR / "normal-smoke/serial-boot.log"
MPY = ROOT / "build/gcc/micropython.elf"
KERNEL = ROOT / "build/gcc/9pc64.elf"
OBOOTOID = ROOT / "build/gcc/gen/obootoid.elf"
MPY_OID = "0x4f45584543000003"

# The checkpoint order is fixed by the measurement build:
#   1  trace boot-loader dump, emitted on the first MicroPython trap
#   2  MicroPython main, immediately after mp_init()
#   3  MicroPython main, after init.run(), commit and report
#   4  REPL input plan9.system() at the end of the scripted commands
PHASES = {
    1: "boot-loader",
    2: "micropython-startup",
    3: "micropython-init-and-commit",
    4: "micropython-repl",
}

DUMP_RE = re.compile(
    r"P2K-SYSCALL-DUMP checkpoint=(\d+) label=(\S+) records=(\d+) dropped=(\d+)")
REC_RE = re.compile(
    r"P2K-SYSCALL rec seq=(\d+) pid=(\d+) kind=(\w+) nr=(\d+) "
    r"state=(\w+) pc=(0x[0-9a-f]+) retpc=(0x[0-9a-f]+)")


def sh(cmd: list[str], *, env: dict[str, str] | None = None,
       log: Path | None = None, timeout: int | None = None) -> int:
    e = os.environ.copy()
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=ROOT, env=e, capture_output=True,
                       text=True, errors="replace", timeout=timeout)
    text = p.stdout + p.stderr
    if log is not None:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(text)
    if p.returncode:
        print(f"FAIL: {' '.join(cmd)} (see {log})")
    return p.returncode


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_measured_binary(src: Path, name: str) -> dict:
    BINDIR.mkdir(parents=True, exist_ok=True)
    dst = BINDIR / name
    shutil.copy2(src, dst)
    return {
        "path": str(dst.relative_to(ROOT)),
        "sha256": sha256(dst),
        "size": dst.stat().st_size,
    }


# ---------------------------------------------------------------------------
# syscall ABI table
# ---------------------------------------------------------------------------

def syscall_names() -> list[tuple[int, str]]:
    out = []
    for line in (ROOT / "runtime/abi/sys.h").read_text().splitlines():
        m = re.match(r"#define\s+([A-Z0-9_][A-Z0-9_]*)\s+([0-9]+)\b", line)
        if m:
            out.append((int(m.group(2)), m.group(1)))
    return out


def dispatch_table() -> tuple[dict[str, str], set[str]]:
    text = (ROOT / "build/gcc/gen/systab-gcc.h").read_text()
    handlers = {}
    for m in re.finditer(r"\[([A-Z0-9_]+)\]\s*(nil|sys\w+),", text):
        handlers[m.group(1)] = m.group(2)
    reserved = set()
    for line in (ROOT / "build/gcc/gen/systab-gcc.reserved").read_text().splitlines():
        if line.strip():
            reserved.add(line.strip())
    return handlers, reserved


def stub_symbol(name: str) -> str:
    s = name.lower()
    if s in ("exits", "nsec"):
        s = "_" + s
    if re.match(r"^_x[0-9]+$", s):
        s = "sysdeath"
    return s


# ---------------------------------------------------------------------------
# static ELF text analysis
# ---------------------------------------------------------------------------

class ElfText:
    def __init__(self, path: Path):
        self.path = path
        self.blocks: list[dict] = []
        self.edges: dict[str, list[str]] = collections.defaultdict(list)
        self.refs: dict[str, list[tuple[str, int, str]]] = collections.defaultdict(list)
        self.starts: list[int] = []
        self._parse()

    def _parse(self) -> None:
        dis = subprocess.run(["objdump", "-d", str(self.path)],
                             capture_output=True, text=True, check=True).stdout
        cur = None
        pending = []
        for line in dis.splitlines():
            m = re.match(r"^([0-9a-f]{6,}) <([^>]+)>:", line)
            if m:
                cur = {"name": m.group(2), "start": int(m.group(1), 16)}
                self.blocks.append(cur)
                continue
            m = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\s+(\w+)\s+(.*)$", line)
            if not m or cur is None:
                continue
            pending.append((cur["name"], int(m.group(1), 16), m.group(2), m.group(3)))
        self.blocks.sort(key=lambda b: b["start"])
        self.starts = [b["start"] for b in self.blocks]
        self.names = {b["name"] for b in self.blocks}
        for caller, addr, op, args in pending:
            tm = re.search(r"<([^>+]+)(?:\+0x[0-9a-f]+)?>", args)
            if not tm or tm.group(1) not in self.names:
                continue
            target = tm.group(1)
            if op in ("call", "jmp"):
                if op == "jmp" and target == caller:
                    continue
                if target not in self.edges[caller]:
                    self.edges[caller].append(target)
            else:
                self.refs[target].append((caller, addr, op))

    def function_at(self, addr: int) -> str:
        i = bisect.bisect_right(self.starts, addr) - 1
        return self.blocks[i]["name"] if i >= 0 else "<unknown>"

    def callers(self, symbol: str) -> list[str]:
        return sorted(n for n, ts in self.edges.items() if symbol in ts)

    def caller_chains(self, symbol: str, depth: int = 2,
                      limit: int = 64) -> list[list[str]]:
        reverse = collections.defaultdict(list)
        for caller, targets in self.edges.items():
            for target in targets:
                reverse[target].append(caller)
        paths = [[symbol]]
        for _ in range(depth):
            nxt = []
            for path in paths:
                for caller in sorted(reverse.get(path[-1], [])):
                    if caller not in path:
                        nxt.append(path + [caller])
            paths = nxt
            if len(paths) > limit:
                paths = paths[:limit]
                break
        return paths

    def reachable_from(self, entry: str) -> set[str]:
        seen = {entry} if entry in self.names else set()
        work = list(seen)
        while work:
            n = work.pop()
            for t in self.edges.get(n, []):
                if t not in seen:
                    seen.add(t)
                    work.append(t)
        return seen

    def address_taken(self) -> dict[str, list[str]]:
        """Find function pointers in allocated PROGBITS sections.

        This scans .text/.rodata as well because the user link script folds
        .rodata into .text.  The result is supporting evidence only; the
        classification below uses direct call/jmp sites.
        """
        sections = subprocess.run(["readelf", "-SW", str(self.path)],
                                  capture_output=True, text=True, check=True).stdout
        starts = {b["start"]: b["name"] for b in self.blocks}
        raw = self.path.read_bytes()
        out: dict[str, list[str]] = collections.defaultdict(list)
        for line in sections.splitlines():
            m = re.match(
                r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+"
                r"([0-9a-f]+)\s+([0-9a-f]+)\s+(\S+)\s+(\S*)\s+"
                r"(\d+)\s+(\d+)\s+(\d+)", line)
            if not m:
                continue
            name, typ, _addr, off, size, _es, flags = m.groups()[:7]
            if typ != "PROGBITS" or "A" not in flags:
                continue
            off, size = int(off, 16), int(size, 16)
            for i in range(0, size - 7, 8):
                v = struct.unpack_from("<Q", raw, off + i)[0]
                if v in starts:
                    out[starts[v]].append(f"{name}+0x{i:x}")
        return dict(out)


# ---------------------------------------------------------------------------
# runtime log parsing
# ---------------------------------------------------------------------------

def parse_trace(path: Path) -> dict:
    if not path.is_file():
        return {"log": str(path.relative_to(ROOT)), "present": False,
                "checkpoints": [], "records": []}
    checkpoints, records = [], []
    cur = None
    for line in path.read_text(errors="replace").splitlines():
        m = DUMP_RE.search(line)
        if m:
            cur = int(m.group(1))
            checkpoints.append({
                "checkpoint": cur,
                "kernel_label": m.group(2),
                "phase": PHASES.get(cur, f"checkpoint-{cur}"),
                "records": int(m.group(3)),
                "dropped": int(m.group(4)),
            })
            continue
        m = REC_RE.search(line)
        if m and cur is not None:
            records.append({
                "checkpoint": cur,
                "seq": int(m.group(1)),
                "pid": int(m.group(2)),
                "kind": m.group(3),
                "nr": int(m.group(4)),
                "state": m.group(5),
                "pc": int(m.group(6), 16),
                "retpc": int(m.group(7), 16),
            })
    return {"log": str(path.relative_to(ROOT)), "present": True,
            "checkpoints": checkpoints, "records": records}


def summarize_trace(trace: dict, mpy: ElfText, boot: ElfText) -> dict:
    names = dict(syscall_names())
    phases = collections.defaultdict(lambda: {
        "total": 0,
        "by_syscall": collections.Counter(),
        "by_caller": collections.Counter(),
        "by_pid": collections.Counter(),
        "reserved_or_unknown": collections.Counter(),
    })
    for r in trace["records"]:
        ph = PHASES.get(r["checkpoint"], f"checkpoint-{r['checkpoint']}")
        p = phases[ph]
        p["total"] += 1
        p["by_pid"][str(r["pid"])] += 1
        p["by_syscall"][(r["nr"], names.get(r["nr"], "?"))] += 1
        elf = boot if r["kind"] == "other" else mpy
        p["by_caller"][(r["nr"], elf.function_at(r["retpc"]))] += 1
        if r["state"] != "ACTIVE":
            p["reserved_or_unknown"][(r["nr"], names.get(r["nr"], "?"),
                                      r["state"])] += 1
    out = {}
    for ph, p in phases.items():
        out[ph] = {
            "total": p["total"],
            "by_pid": dict(p["by_pid"]),
            "by_syscall": [
                {"nr": nr, "name": name, "count": n}
                for (nr, name), n in sorted(p["by_syscall"].items())],
            "by_caller": [
                {"nr": nr, "caller": caller, "count": n}
                for (nr, caller), n in sorted(p["by_caller"].items())],
            "reserved_or_unknown": [
                {"nr": nr, "name": name, "state": state, "count": n}
                for (nr, name, state), n in sorted(p["reserved_or_unknown"].items())],
        }
    pids = sorted({r["pid"] for r in trace["records"]})
    return {
        **trace,
        "phases": out,
        "pids": pids,
        "observed_syscalls": sorted({r["nr"] for r in trace["records"]}),
        "invalid_traps": [
            r for r in trace["records"] if r["state"] != "ACTIVE"],
    }


def aggregate_observed(runs: dict[str, dict]) -> dict[int, dict]:
    agg: dict[int, dict] = {}
    names = dict(syscall_names())
    for run, trace in runs.items():
        for ph, data in trace.get("phases", {}).items():
            for item in data["by_syscall"]:
                a = agg.setdefault(item["nr"], {
                    "nr": item["nr"], "name": item["name"], "total": 0,
                    "runs": collections.Counter(), "phases": collections.Counter(),
                })
                a["total"] += item["count"]
                a["runs"][run] += item["count"]
                a["phases"][ph] += item["count"]
    for a in agg.values():
        a["runs"] = dict(sorted(a["runs"].items()))
        a["phases"] = dict(sorted(a["phases"].items()))
    return dict(sorted(agg.items()))


# ---------------------------------------------------------------------------
# report
# ---------------------------------------------------------------------------

def build_report(runs: dict[str, dict], mpy: ElfText, boot: ElfText,
                 mpy_info: dict, boot_info: dict) -> dict:
    names = dict(syscall_names())
    handlers, reserved_handlers = dispatch_table()
    mpy_reach = mpy.reachable_from("_start")
    boot_reach = boot.reachable_from("_main")
    mpy_taken = mpy.address_taken()
    boot_taken = boot.address_taken()
    observed = aggregate_observed(runs)

    entries = []
    for nr, name in sorted(names.items()):
        sym = stub_symbol(name)
        active = handlers.get(name) not in (None, "nil")
        dispatched_reserved = not active
        present_mpy = sym in mpy.names
        present_boot = sym in boot.names
        callers_mpy = mpy.callers(sym) if present_mpy else []
        callers_boot = boot.callers(sym) if present_boot else []
        callers = ([{"binary": "micropython", "function": c,
                     "directly_reachable_from_entry": c in mpy_reach}
                    for c in callers_mpy] +
                   [{"binary": "obootoid", "function": c,
                     "directly_reachable_from_entry": c in boot_reach}
                    for c in callers_boot])
        obs = observed.get(nr)
        if obs:
            category = "A"
            detail = "observed trap in this measurement"
        elif not active:
            category = "C"
            detail = ("reserved historical number with nil dispatch; "
                      "no trap observed")
            if callers:
                detail += ("; linked stub has caller(s), but dispatch rejects "
                           "the number")
        elif callers:
            category = "B"
            detail = ("active kernel syscall with a direct linked caller in the "
                      "measured userspace, not executed by these tests")
        else:
            category = "D"
            detail = ("active kernel syscall with no direct caller in the "
                      "measured userspace; not exercised and not shown unused "
                      "by the kernel")
        taken = []
        if sym in mpy_taken:
            taken += [{"binary": "micropython", "location": x}
                      for x in mpy_taken[sym]]
        if sym in boot_taken:
            taken += [{"binary": "obootoid", "location": x}
                      for x in boot_taken[sym]]
        entries.append({
            "nr": nr,
            "name": name,
            "symbol": sym,
            "dispatch_status": "ACTIVE" if active else "RESERVED",
            "category": category,
            "category_detail": detail,
            "runtime": obs or {"nr": nr, "name": name, "total": 0,
                               "runs": {}, "phases": {}},
            "linked": {
                "micropython": {
                    "present": present_mpy,
                    "callers": callers_mpy,
                    "address_taken_locations": mpy_taken.get(sym, []),
                },
                "obootoid": {
                    "present": present_boot,
                    "callers": callers_boot,
                    "address_taken_locations": boot_taken.get(sym, []),
                },
                "all_callers": callers,
                "caller_chains": (
                    [{"binary": "micropython", "chain": c}
                     for c in mpy.caller_chains(sym)] +
                    [{"binary": "obootoid", "chain": c}
                     for c in boot.caller_chains(sym)]),
            },
            "test_only": sym == "sysraw",
        })

    active = [e for e in entries if e["dispatch_status"] == "ACTIVE"]
    reserved = [e for e in entries if e["dispatch_status"] == "RESERVED"]
    defined = {e["nr"] for e in entries}
    undefined = [n for n in range(0, 141) if n not in defined]
    five = {}
    for nr_name in ("OBJCREATE", "OBJOPEN", "OBJCLOSE", "OBJBIND", "OBJINVOKE"):
        e = next(x for x in entries if x["name"] == nr_name)
        five[nr_name] = {
            "nr": e["nr"],
            "category": e["category"],
            "runtime_counts": e["runtime"]["runs"],
            "runtime_phases": e["runtime"]["phases"],
            "linked_callers": e["linked"]["all_callers"],
            "linked_caller_chains": e["linked"]["caller_chains"],
        }

    return {
        "task": "T0",
        "generated_utc": datetime.datetime.now(datetime.timezone.utc)
        .strftime("%Y-%m-%dT%H:%M:%SZ"),
        "source_snapshot_reference":
            "1236e5b94c3e9b5e3323c4bfaac59e35ad69d20bc9c6eaa490deff87b032e172",
        "environment": {
            "host": os.uname().nodename,
            "kernel": os.uname().release,
            "qemu": subprocess.run(["qemu-system-x86_64", "--version"],
                                   capture_output=True, text=True)
            .stdout.splitlines()[0] if shutil.which("qemu-system-x86_64") else "not found",
            "accel": "kvm" if Path("/dev/kvm").exists() else "tcg",
            "trace_macro": "PLAN2001_SYSCALL_TRACE=1 (measurement build only)",
        },
        "binaries": {
            "measured_micropython": mpy_info,
            "measured_obootoid": boot_info,
            "measured_kernel": ({
                "path": str((BINDIR / "9pc64.elf").relative_to(ROOT)),
                "sha256": sha256(BINDIR / "9pc64.elf"),
                "size": (BINDIR / "9pc64.elf").stat().st_size,
            } if (BINDIR / "9pc64.elf").is_file() else
                {"path": str(KERNEL.relative_to(ROOT)),
                 "sha256": sha256(KERNEL),
                 "size": KERNEL.stat().st_size}),
            "normal_artifacts_restored": True,
        },
        "syscall_table": {
            "source": "runtime/abi/sys.h; build/gcc/gen/systab-gcc.h",
            "defined_names": len(entries),
            "active_count": len(active),
            "reserved_count": len(reserved),
            "undefined_numbers_0_140": undefined,
            "entries": entries,
        },
        "runs": runs,
        "observed_summary": {
            "syscalls": list(observed.values()),
            "observed_numbers": sorted(observed),
            "any_reserved_or_unknown_traps": any(
                t["invalid_traps"] for t in runs.values()),
            "boot_pids": {name: t.get("pids", []) for name, t in runs.items()},
            "child_processes_observed": sorted({
                pid for t in runs.values() for pid in t.get("pids", [])
                if t.get("pids") and pid != t["pids"][0]
            }),
        },
        "five_obj_calls": five,
        "static_method": {
            "call_graph": ("direct call and tail-jmp sites parsed from the "
                           "measured ELF text; callers are function symbols"),
            "address_taken": ("8-byte function addresses found in allocated "
                              "PROGBITS sections; supporting evidence only"),
            "runtime_caller": ("recorded user return PC (retpc) mapped to the "
                               "containing function; tail-jumped libc adapters "
                               "can make this one frame above the wrapper"),
        },
    }


def print_summary(report: dict) -> None:
    print("syscall    nr  category  observed")
    for e in report["syscall_table"]["entries"]:
        if e["category"] in ("A", "B", "C") or e["name"].startswith("OBJ"):
            print(f"{e['name']:<12} {e['nr']:>3}  {e['category']:<8}  "
                  f"{e['runtime']['total']}")
    print()
    print("five OBJ calls:")
    for name, v in report["five_obj_calls"].items():
        print(f"  {name}: {v['category']} runtime={v['runtime_counts']}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", action="store_true",
                    help="build the measurement artifacts and execute the QEMU tests")
    ap.add_argument("--out", default="build/syscall-t0/measurement.json",
                    help="JSON report path (build/, .agent-work/, or outside the checkout)")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()

    run_results = {}
    if args.run:
        LOGDIR.mkdir(parents=True, exist_ok=True)
        OUTDIR.mkdir(parents=True, exist_ok=True)
        trace_env = {"PLAN2001_SYSCALL_TRACE": "1"}

        print("building measurement kernel and MicroPython")
        if sh(["tools/build-gcc.sh", "--kernel", "-j", "8"], env=trace_env,
              log=LOGDIR / "build-kernel-measure.log"):
            return 1
        if sh(["tools/build-mpy-gcc.sh", "-j", "8"], env=trace_env,
              log=LOGDIR / "build-mpy-measure.log"):
            return 1
        if sh(["tools/build-efi-gcc.sh", "-j", "8"],
              log=LOGDIR / "build-efi.log"):
            return 1
        if sh(["tools/build.sh"], log=LOGDIR / "build-host.log"):
            return 1

        BINDIR.mkdir(parents=True, exist_ok=True)
        shutil.copy2(KERNEL, BINDIR / "9pc64.elf")
        (BINDIR / "9pc64.elf.sha256").write_text(sha256(KERNEL) + "\n")
        copy_measured_binary(MPY, "micropython.elf")
        copy_measured_binary(OBOOTOID, "obootoid.elf")
        (BINDIR / "SOURCE-SNAPSHOT").write_text(
            "reference 1236e5b94c3e9b5e3323c4bfaac59e35ad69d20bc9c6eaa490deff87b032e172\n")

        inp = OUTDIR / "inputs/repl-t0.py"
        inp.parent.mkdir(parents=True, exist_ok=True)
        inp.write_text(
            "print(6*7)\n"
            "import plan9\n"
            'h = plan9.obj_open_space("init")\n'
            'print("T0-OBJ-HANDLE", h)\n'
            "plan9.obj_close(h)\n"
            'print("T0-OBJ-OK")\n'
            "plan9.system()\n")

        run_results["boot"] = sh([
            "tools/qemu-o-boot.sh", "--no-build",
            "--output-dir", str(OUTDIR / "boot"), "--timeout", "180",
            "--obootexec", MPY_OID, "--repl-input", str(inp),
            "--expect", "T0-OBJ-OK",
        ], log=LOGDIR / "qemu-o-boot.log")
        run_results["persist"] = sh([
            "tools/qemu-o-persist.sh", "--no-build",
            "--output-dir", str(OUTDIR / "persist"), "--timeout", "240",
        ], env={"QOB_INI_BOOTEXEC": MPY_OID}, log=LOGDIR / "qemu-o-persist.log")

        print("rebuilding normal canonical artifacts")
        if sh(["tools/build-gcc.sh", "--kernel", "-j", "8"],
              log=LOGDIR / "build-kernel-normal.log"):
            return 1
        if sh(["tools/build-mpy-gcc.sh", "-j", "8"],
              log=LOGDIR / "build-mpy-normal.log"):
            return 1
        run_results["normal-smoke"] = sh([
            "tools/qemu-o-boot.sh", "--no-build",
            "--output-dir", str(OUTDIR / "normal-smoke"), "--timeout", "180",
            "--obootexec", MPY_OID, "--repl-input", str(inp),
            "--expect", "T0-OBJ-OK",
        ], log=LOGDIR / "qemu-o-normal-smoke.log")
        run_results["normal-persist"] = sh([
            "tools/qemu-o-persist.sh", "--no-build",
            "--output-dir", str(OUTDIR / "normal-persist"), "--timeout", "240",
        ], env={"QOB_INI_BOOTEXEC": MPY_OID},
            log=LOGDIR / "qemu-o-normal-persist.log")

    mpy_path = BINDIR / "micropython.elf" if (BINDIR / "micropython.elf").is_file() else MPY
    boot_path = BINDIR / "obootoid.elf" if (BINDIR / "obootoid.elf").is_file() else OBOOTOID
    mpy = ElfText(mpy_path)
    boot = ElfText(boot_path)
    mpy_info = copy_measured_binary(mpy_path, "micropython.elf") \
        if mpy_path != BINDIR / "micropython.elf" else {
            "path": str(mpy_path.relative_to(ROOT)), "sha256": sha256(mpy_path),
            "size": mpy_path.stat().st_size}
    boot_info = copy_measured_binary(boot_path, "obootoid.elf") \
        if boot_path != BINDIR / "obootoid.elf" else {
            "path": str(boot_path.relative_to(ROOT)), "sha256": sha256(boot_path),
            "size": boot_path.stat().st_size}

    runs = {
        "boot": summarize_trace(parse_trace(BOOT_LOG), mpy, boot),
        "persist-boot1": summarize_trace(parse_trace(PERSIST1), mpy, boot),
        "persist-boot2": summarize_trace(parse_trace(PERSIST2), mpy, boot),
    }
    if NORMAL_SMOKE.is_file():
        runs["normal-smoke"] = summarize_trace(parse_trace(NORMAL_SMOKE), mpy, boot)
    report = build_report(runs, mpy, boot, mpy_info, boot_info)
    report["run_exit_status"] = run_results
    if args.run:
        report["validation"] = {
            "instrumented_boot": "PASS" if run_results.get("boot", 1) == 0 else "FAIL",
            "instrumented_persist": "PASS" if run_results.get("persist", 1) == 0 else "FAIL",
            "normal_smoke_boot": ("PASS" if run_results.get("normal-smoke", 1) == 0
                                  else "FAIL"),
            "normal_persist": ("PASS" if run_results.get("normal-persist", 1) == 0
                               else "FAIL"),
            "normal_rebuild": "PASS",
        }
    else:
        report["validation"] = {
            "instrumented_boot": "NOT RUN (analyze-only)",
            "instrumented_persist": "NOT RUN (analyze-only)",
            "normal_smoke_boot": "NOT RUN (analyze-only)",
            "normal_persist": "NOT RUN (analyze-only)",
            "normal_rebuild": "NOT RUN (analyze-only)",
        }
    out = (ROOT / args.out).resolve()
    # Per-run measurement reports are local artifacts, never tracked docs.
    allowed_local = ((ROOT / "build").resolve(), (ROOT / ".agent-work").resolve())
    if out.is_relative_to(ROOT) and not any(
            out.is_relative_to(directory) for directory in allowed_local):
        ap.error("--out must be under build/, .agent-work/, or outside the checkout")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(f"wrote {out}")
    if args.summary:
        print_summary(report)
    return 0 if all(v == 0 for v in run_results.values()) or not args.run else 1


if __name__ == "__main__":
    sys.exit(main())
