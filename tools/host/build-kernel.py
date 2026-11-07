#!/usr/bin/env python3
"""Linux-hosted GCC build for the canonical Plan2001-O amd64 kernel.

The default and --kernel modes compile the explicit source manifest,
build the GCC libraries and link build/gcc/9pc64.elf.  There is no
dependency on 9front mkfile syntax: KERNEL_SOURCES below is the one
authoritative compile-unit list.  The optional --full and --probe modes
are compile/readiness checks only.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
BUILD = ROOT / "build" / "gcc"
OBJDIR = BUILD / "obj"
LOGDIR = BUILD / "log"
GENDIR = BUILD / "gen"
PROBEDIR = BUILD / "probe"
PROBELOG = BUILD / "probe-log"

CC_DEFAULT = os.environ.get("CC", "gcc")

CFLAGS = [
    "-std=gnu11",
    "-m64",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fno-pie",
    "-fno-pic",
    "-fno-asynchronous-unwind-tables",
    "-fno-unwind-tables",
    "-fcommon",
    "-mno-red-zone",
    "-mcmodel=kernel",
    "-fno-omit-frame-pointer",
    "-fno-strict-aliasing",
    "-DE7_IPV6_ONLY",
    "-Werror=implicit-function-declaration",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
    "-Werror=return-type",
]

# Measurement-only: tools/host/syscall-t0-measure.py sets this to build a
# dispatch-tracing kernel without changing the normal canonical build.
if os.environ.get("PLAN2001_SYSCALL_TRACE") == "1":
    CFLAGS.append("-DPLAN2001_SYSCALL_TRACE")
if os.environ.get("PLAN2001_ALARM_NOTETEST") == "1":
    CFLAGS.append("-DPLAN2001_ALARM_NOTETEST")

INCLUDES = [
    ROOT / "kernel/amd64",
    ROOT / "kernel/core",
    ROOT / "kernel/net",
    ROOT / "kernel/drivers/usb",
    ROOT / "kernel/drivers/uart",
    ROOT / "ge/storage",
    ROOT / "runtime/abi",
    ROOT / "include/amd64",
    ROOT / "include",
    ROOT,
    GENDIR,
]

# The explicit GCC compile manifest: object name -> source file.  This is
# the single authoritative list for the canonical kernel; generated
# objects (pc64.c, pc64.rootc.c) are added separately in full_units().
KERNEL_SOURCES = {
    "alarm": "kernel/core/alarm.c",
    "alloc": "kernel/core/alloc.c",
    "allocb": "kernel/core/allocb.c",
    "apic": "kernel/amd64/apic.c",
    "archacpi": "kernel/amd64/archacpi.c",
    "archcore": "kernel/amd64/archcore.c",
    "archgeneric": "kernel/amd64/archgeneric.c",
    "arp": "kernel/net/arp.c",
    "auth": "kernel/core/auth.c",
    "bootarch": "kernel/amd64/bootarch.c",
    "bootargs": "kernel/core/bootargs.c",
    "bootfb": "kernel/core/bootfb.c",
    "bootinfo": "kernel/core/bootinfo.c",
    "chan": "kernel/core/chan.c",
    "cons": "kernel/core/cons.c",
    "ec": "kernel/amd64/ec.c",
    "edf": "kernel/core/edf.c",
    "envcore": "kernel/core/env/envcore.c",
    "ether8169": "kernel/amd64/ether8169.c",
    "ether82563": "kernel/amd64/ether82563.c",
    "ethercore": "kernel/core/ethercore.c",
    "etheri225": "kernel/amd64/etheri225.c",
    "ethermii": "kernel/core/ethermii.c",
    "ethernative": "kernel/net/ethernative.c",
    "ethersink": "kernel/core/ethersink.c",
    "ethervirtio10": "kernel/core/ethervirtio10.c",
    "fault": "kernel/core/fault.c",
    "fbconsole": "kernel/core/fbconsole.c",
    "fpu": "kernel/amd64/fpu.c",
    "g_avl": "ge/storage/avl.c",
    "g_blk": "ge/storage/blk.c",
    "g_cache": "ge/storage/cache.c",
    "g_check": "ge/storage/check.c",
    "g_dump": "ge/storage/dump.c",
    "g_error": "ge/storage/error.c",
    "g_fs": "ge/storage/fs.c",
    "g_hash": "ge/storage/hash.c",
    "g_kern": "ge/storage/kern.c",
    "g_load": "ge/storage/load.c",
    "g_o": "ge/storage/o.c",
    "g_obj": "ge/storage/obj.c",
    "g_pack": "ge/storage/pack.c",
    "g_snap": "ge/storage/snap.c",
    "g_tree": "ge/storage/tree.c",
    "hpet": "kernel/amd64/hpet.c",
    "i8253": "kernel/amd64/i8253.c",
    "i8259": "kernel/amd64/i8259.c",
    "icmp6": "kernel/net/icmp6.c",
    "iomap": "kernel/core/iomap.c",
    "ip6only": "kernel/net/ip6only.c",
    "ipaux": "kernel/net/ipaux.c",
    "ipcore": "kernel/net/ipcore.c",
    "ipifc": "kernel/net/ipifc.c",
    "iproute6": "kernel/net/iproute6.c",
    "ipv6": "kernel/net/ipv6.c",
    "espctl": "kernel/net/espctl.c",
    "espcontrol": "kernel/net/espcontrol.c",
    "esp6": "kernel/net/esp6.c",
    "ipv6only": "kernel/net/ipv6only.c",
    "irq": "kernel/amd64/irq.c",
    "kbdcore": "kernel/amd64/kbdcore.c",
    "led": "kernel/core/led.c",
    "lentry": "kernel/amd64/gcc/lentry.S",
    "lgcc": "kernel/amd64/gcc/lgcc.S",
    "loopbackmedium": "kernel/net/loopbackmedium.c",
    "lprim": "kernel/amd64/gcc/lprim.S",
    "main": "kernel/amd64/main.c",
    "memmap": "kernel/core/memmap.c",
    "memory": "kernel/amd64/memory.c",
    "mmu": "kernel/amd64/mmu.c",
    "mp": "kernel/amd64/mp.c",
    "mtrr": "kernel/amd64/mtrr.c",
    "nativeinh6": "kernel/net/nativeinh6.c",
    "nativeudp6": "kernel/net/nativeudp6.c",
    "nativev6": "kernel/net/nativev6.c",
    "netlog": "kernel/net/netlog.c",
    "nullmedium": "kernel/net/nullmedium.c",
    "ouser": "kernel/amd64/ouser.c",
    "page": "kernel/core/page.c",
    "parse": "kernel/core/parse.c",
    "pci": "kernel/core/pci.c",
    "pcipc": "kernel/amd64/pcipc.c",
    "pgrp": "kernel/core/pgrp.c",
    "pipecore": "kernel/core/pipe/pipecore.c",
    "portclock": "kernel/core/portclock.c",
    "print": "kernel/core/print.c",
    "proc": "kernel/core/proc.c",
    "procobs": "kernel/core/procobs.c",
    "qio": "kernel/core/qio.c",
    "qlock": "kernel/core/qlock.c",
    "random": "kernel/core/random.c",
    "rdb": "kernel/core/rdb.c",
    "rtccore": "kernel/amd64/rtccore.c",
    "sdcore": "kernel/core/sdcore.c",
    "sdiahci": "kernel/amd64/sdiahci.c",
    "sdnvme": "kernel/core/sdnvme.c",
    "sdram": "kernel/core/sdram.c",
    "sdscsi": "kernel/core/sdscsi.c",
    "sdvirtio10": "kernel/core/sdvirtio10.c",
    "segment": "kernel/core/segment.c",
    "squidboy": "kernel/amd64/squidboy.c",
    "syscallfmt": "kernel/core/syscallfmt.c",
    "sysfile": "kernel/core/sysfile.c",
    "sysgefs": "ge/storage/sysgefs.c",
    "ocontext": "ge/storage/ocontext.c",
    "relations-kernel": "ge/storage/relations-kernel.c",
    "sysinfo": "kernel/core/sysinfo.c",
    "syskbd": "kernel/core/syskbd.c",
    "sysn3": "kernel/core/sysn3.c",
    "sysobj": "kernel/core/sysobj.c",
    "sysnet6": "kernel/core/sysnet6.c",
    "systcp6": "kernel/core/systcp6.c",
    "syspipe": "kernel/core/syspipe.c",
    "sysproc": "kernel/core/sysproc.c",
    "sysuart": "kernel/core/sysuart.c",
    "sysusb": "kernel/core/sysusb.c",
    "sysvobj": "kernel/core/sysvobj.c",
    "taslock": "kernel/core/taslock.c",
    "tod": "kernel/core/tod.c",
    "trap": "kernel/amd64/trap.c",
    "uartcore": "kernel/drivers/uart/uartcore.c",
    "uarti8250": "kernel/amd64/uarti8250.c",
    "uartpci": "kernel/amd64/uartpci.c",
    "udp": "kernel/net/udp.c",
    "tcp": "kernel/net/tcp.c",
    "usbcore": "kernel/drivers/usb/usbcore.c",
    "usbehci": "kernel/drivers/usb/usbehci.c",
    "usbehcipc": "kernel/amd64/usbehcipc.c",
    "usbxhci": "kernel/drivers/usb/usbxhci.c",
    "usbxhcipci": "kernel/drivers/usb/usbxhcipci.c",
    "virtio10pc": "kernel/amd64/virtio10pc.c",
    "xalloc": "kernel/core/xalloc.c",
}

GENERATED_INCLUDES = {
    "rebootcode.i": "generated reboot blob (GCC path)",
    "apbootstrap.i": "generated AP bootstrap (GCC path)",
    "obootoid.i": "generated obootoid user image (GCC path)",
    "kbuildid.h": "generated UTC kernel build identity under build/gcc/gen",
    "errstr.h": "generated under build/gcc/gen",
    "systab.h": "generated systab-gcc.h under build/gcc/gen",
    "systab-gcc.h": "generated under build/gcc/gen",
}

GENERATED_UNITS = {
    "pc64": "pc64.c is generated from kernel/amd64/pc64",
    "pc64.rootc": "pc64.rootc.c is generated",
}


@dataclass
class Unit:
    group: str
    obj: str
    src: Path
    canonical: str = ""
    selected: bool = False
    note: str = ""
    extra_flags: list[str] = field(default_factory=list)

    @property
    def relsrc(self) -> str:
        return self.src.relative_to(ROOT).as_posix()


@dataclass
class Result:
    unit: Unit
    status: str
    category: str = ""
    detail: str = ""
    log: Path | None = None
    seconds: float = 0.0


# ---------------------------------------------------------------------------
# explicit manifest lookup
# ---------------------------------------------------------------------------

def source_for_obj(obj: str) -> Path | None:
    rel = KERNEL_SOURCES.get(obj)
    return ROOT / rel if rel else None


def canonical_objects() -> tuple[dict[str, str], list[str]]:
    """Return the explicit object -> group map and generated-only objects."""
    mapping = {obj: "KERNEL" for obj in KERNEL_SOURCES}
    generated = [obj for obj in GENERATED_UNITS]
    return mapping, generated


# ---------------------------------------------------------------------------
# compiler invocation
# ---------------------------------------------------------------------------

def compiler_version(cc: str) -> str:
    try:
        p = subprocess.run([cc, "--version"], capture_output=True, text=True, timeout=20)
        first = (p.stdout or p.stderr).splitlines()
        return first[0].strip() if first else "unknown"
    except Exception as exc:  # noqa: BLE001 - report, do not crash the build
        return f"unavailable ({exc})"


def make_kbuildid() -> None:
    """Generate the one UTC kernel build identity for this build.

    SOURCE_DATE_EPOCH is honoured when explicitly supplied (interpreted as
    UTC); otherwise the current UTC time is captured once.  The generated
    header is rewritten on every build, so the linked kernel carries the
    new stamp without any runtime clock.
    """
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch is not None:
        try:
            when = datetime.fromtimestamp(int(epoch, 10), tz=timezone.utc)
        except (ValueError, OverflowError, OSError) as exc:
            raise SystemExit(f"invalid SOURCE_DATE_EPOCH {epoch!r}: {exc}")
    else:
        when = datetime.now(timezone.utc)
    stamp = when.strftime("%Y-%m-%d %H:%M UTC")
    GENDIR.mkdir(parents=True, exist_ok=True)
    (GENDIR / "kbuildid.h").write_text(
        "#ifndef PLAN2001_KBUILDID_H\n"
        "#define PLAN2001_KBUILDID_H\n"
        '#define PLAN2001_KERNEL_BUILD_MAGIC "PLAN2001-O-KERNEL-BUILD:"\n'
        f'#define PLAN2001_KERNEL_BUILD_STAMP "{stamp}"\n'
        "#define PLAN2001_KERNEL_BUILD_ID "
        "PLAN2001_KERNEL_BUILD_MAGIC PLAN2001_KERNEL_BUILD_STAMP\n"
        "#endif\n")


def make_errstr() -> None:
    """Generate errstr.h under build/gcc/gen, never in the source tree."""
    src = ROOT / "kernel/core/error.h"
    out: list[str] = []
    for line in src.read_text().splitlines():
        line = line.replace("extern ", "", 1)
        m = re.match(r"^(.*?);.*/\* (.*) \*/", line)
        if m:
            line = f'{m.group(1)} = "{m.group(2)}";'
        out.append(line)
    GENDIR.mkdir(parents=True, exist_ok=True)
    (GENDIR / "errstr.h").write_text("\n".join(out) + "\n")


def make_systab_gcc() -> None:
    """Generate the GCC systab header from the explicit kernel manifest.

    A handler is a function defined by a C source that KERNEL_SOURCES
    actually compiles.  Generated kernel units contain configuration and
    startup data, not syscall handlers, so no generated source is scanned.
    Missing handlers are a build error unless the exact historical name is
    in the explicit N11.3 set; those resolve to the one explicit reject.
    """
    sysh = (ROOT / "runtime/abi/sys.h").read_text()

    names: list[str] = []
    numbers: dict[int, str] = {}
    for line in sysh.splitlines():
        m = re.match(r"#define\s+([A-Z0-9_][A-Z0-9_]*)\s+([0-9]+)\b", line)
        if not m:
            continue
        name, number = m.group(1), int(m.group(2))
        if name in names:
            raise SystemExit(f"systab: duplicate syscall name {name}")
        if number in numbers:
            raise SystemExit(
                f"systab: duplicate ABI number {number}: {numbers[number]} and {name}")
        names.append(name)
        numbers[number] = name

    # Deliberately explicit: every historical number whose implementation was
    # retired.  These stay allocated in sys.h forever but emit a nil dispatch
    # entry, so a raw call cannot reach a removed legacy implementation.  A
    # new unexpected missing handler still fails the build.
    reserved = {
        # Phase 1: retired TCP6* and VOBJ* compatibility families.
        "systcp6listen", "systcp6connect", "systcp6accept", "systcp6read",
        "systcp6write", "systcp6close",
        "sysvobjcreate", "sysvobjread", "sysvobjinfo", "sysvobjclose",
        "sysvobjfifocreate", "sysvobjfifosub", "sysvobjfifosend",
        "sysvobjfiforecv", "sysvobjfifostat", "sysvobjgrant",
        "sysvobjrevoke", "sysvobjio", "sysvobjlist", "sysvobjopen",
        "sysvobjaccept",
        # Phase 2: retired pathname/FS calls.
        "sysbind", "syschdir", "sysexec", "sys_mount", "sysopen",
        "sys_stat", "syscreate", "sysfd2path", "sysremove", "sys_wstat",
        "sys_fwstat", "sysunmount", "sysstat", "syswstat", "sysfwstat",
        "sysmount",
        # Phase 2: retired always-failing legacy FD calls.
        "sysclose", "sysdup", "sys_fstat", "sys_read", "sysoseek",
        "sys_write", "syspipe", "sysseek", "sysfstat", "syspread",
        "syspwrite",
        # Phase 3: retired native Pipe, UART and KBD compatibility family
        # plus the one-shot PIPEINHERIT call.  The numbers stay allocated
        # forever; the five-call OBJ* ABI is the only entry point.
        "syspipenew", "syspiperead", "syspipewrite", "syspipeclose",
        "syskbdopen", "syskbdread", "syskbdclose", "syskbdleds",
        "sysuartcount", "sysuartopen", "sysuartread", "sysuartwrite",
        "sysuartioctl", "sysuartstatus", "sysuartclose",
        "syspipeinherit",
        # Phase 4: retired NET6* UDP6 compatibility family.  The address
        # lookup is OBJINVOKE(ObjOpNet6Addr), the endpoint is
        # OBJOPEN(ObjOpenUdp6), and data uses SendTo/RecvFrom/CLOSE.
        "sysnet6addr", "sysnet6open", "sysnet6sendto",
        "sysnet6recvfrom", "sysnet6close",
        # Console: IOWRITE(62) is retired.  Native output is an OBJ
        # console Handle (ObjOpenConsole) written with OBJINVOKE(Write).
        "sysiowrite",
        # Phase 6: process lifecycle.  _WAIT(36), AWAIT(47) and OSPAWN(92)
        # now dispatch to nothing; the typed ObjOpWait/Ospawn operations call
        # the same shared kern* cores.  RFORK(19), OTERMINATE(93) and
        # EXITS(8) are direct historical calls again: their shared
        # kernrfork/kernoterminate/pexit cores and the obootoid loader's
        # EXITS error path all stay active.  NOTIFY(28) and NOTED(29) are
        # direct again too; only their former ObjOp variants were retired.
        "sys_wait", "sysawait", "sysospawn",
    }
    # Non-handler helpers that also take sysva_list in the handler modules.
    non_handlers = {"syscallfmt", "sysretfmt"}

    expected: dict[str, str] = {}
    for name in names:
        fn = "sys" + name.lower()
        if name == "SYSR1":
            fn = "sysr1"
        if re.match(r"^sys_x[0-9]*$", fn):
            fn = "sysdeath"
        expected[name] = fn
    expected_fns = set(expected.values())

    defpat = re.compile(
        r"(?m)^([A-Za-z_][A-Za-z0-9_ *]*?)[ \t]*(?:\n[ \t]*|[ \t]+)"
        r"(sys[a-z0-9_]+)\s*\(([^)]*)\)\s*$")
    found: dict[str, tuple[str, Path, str]] = {}
    for obj, rel in sorted(KERNEL_SOURCES.items(), key=lambda kv: kv[1]):
        src = ROOT / rel
        if src.suffix != ".c":
            continue
        for m in defpat.finditer(src.read_text()):
            ret, fn, params = m.group(1).strip(), m.group(2), m.group(3)
            if fn in expected_fns and "sysva_list" not in params:
                raise SystemExit(
                    f"systab: handler {fn} in {rel} has no sysva_list parameter")
            if "sysva_list" not in params:
                continue
            if fn in found:
                prev = found[fn][1]
                raise SystemExit(
                    f"systab: duplicate handler {fn}: {prev.relative_to(ROOT)} and {rel}")
            found[fn] = (ret, src, params)

    used_reserved: set[str] = set()

    def resolve(name: str) -> str | None:
        fn = expected[name]
        if fn == "sysdeath":
            return fn
        if fn in reserved:
            if fn in found:
                raise SystemExit(
                    f"systab: {fn} is listed reserved but is defined in "
                    f"{found[fn][1].relative_to(ROOT)}")
            used_reserved.add(fn)
            return None
        if fn not in found:
            raise SystemExit(
                f"systab: missing handler {fn} for {name}; it is not in the "
                "explicit reserved set")
        ret, src, params = found[fn]
        if ret != "uintptr":
            raise SystemExit(
                f"systab: handler {fn} in {src.relative_to(ROOT)} returns {ret}, not uintptr")
        return fn

    resolved = [(name, resolve(name)) for name in names]

    unknown = sorted(set(found) - expected_fns - non_handlers)
    if unknown:
        raise SystemExit(
            "systab: sysva_list helper(s) without an ABI name: " + ", ".join(unknown))

    GENDIR.mkdir(parents=True, exist_ok=True)
    for stale in ("systab-gcc.missing", "systab-gcc.unsupported",
                  "systab-gcc.reserved"):
        try:
            (GENDIR / stale).unlink()
        except FileNotFoundError:
            pass

    out = ['#include "runtime/abi/sys.h"', "",
           "typedef uintptr Syscall(sysva_list);", ""]
    declared: set[str] = set()
    for _, fn in resolved:
        if fn is None or fn in declared:
            continue
        declared.add(fn)
        ret = found.get(fn, ("uintptr", None, ""))[0]
        out.append(f"extern {ret}\t{fn}(sysva_list);")
    out += ["", "Syscall\tsysdeath;", "", "Syscall *systab[]={"]
    for name, fn in resolved:
        out.append(f"\t[{name}]\t{'nil' if fn is None else fn},")
    out += ["};", "", "char *sysctab[]={"]
    reserved_names = set()
    for name, fn in resolved:
        if fn is None:
            reserved_names.add(name)
    for name in names:
        c = name[0].lower() + name[1:].lower()
        c = re.sub(r"_$", "", c)
        if name == "SYSR1":
            c = "Running"
        if name == "RENDEZVOUS":
            c = "Rendez"
        out.append(f'\t[{name}]\t"{c}",')
    out += ["};", "", "/* Reserved historical numbers: reject as Ebadarg, no note. */",
            "char sysreserved[]={"]
    for name in names:
        if name in reserved_names:
            out.append(f"\t[{name}]\t1,")
    out += ["};", "", "int nsyscall = (sizeof systab/sizeof systab[0]);", ""]
    (GENDIR / "systab-gcc.h").write_text("\n".join(out) + "\n")
    (GENDIR / "systab-gcc.reserved").write_text(
        "\n".join(sorted(used_reserved)) + "\n")


def parse_config_rows(path: Path) -> dict[str, list[list[str]]]:
    sections: dict[str, list[list[str]]] = {}
    current: str | None = None
    for raw in path.read_text().splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if not raw[0].isspace():
            name = raw.split()[0]
            current = name if name in ("dev", "link", "misc", "ip", "port") else None
            if current:
                sections.setdefault(current, [])
            continue
        if current is None:
            continue
        fields = raw.split()
        if fields:
            sections[current].append(fields)
    return sections


def make_pc64() -> None:
    """Generate pc64.c from the canonical kernel/amd64/pc64 configuration."""
    cfg = parse_config_rows(ROOT / "kernel/amd64/pc64")
    links = [r[0] for r in cfg.get("link", [])]
    misc = [r[0] for r in cfg.get("misc", [])]
    ips = [r[0] for r in cfg.get("ip", [])]
    ports = [" ".join(r) for r in cfg.get("port", [])]

    arch = [n for n in misc if re.match(r"^arch", n) and n != "archcore"]
    sdifc = [n for n in misc if re.match(r"^sd", n)]
    ramdisk = "sdram" in sdifc
    devuart = "uartcore" in misc or "devuart" in misc
    physuart = ["i8250physuart" if n == "uarti8250" else n[4:] + "physuart"
                for n in misc if re.match(r"^uart", n)
                and n not in ("uartcore", "devuart")]
    i8237dma = sum(1 for r in cfg.get("dev", []) if "dma" in r[1:])

    out = ['#include "u.h"', '#include "lib.h"', '#include "mem.h"',
           '#include "dat.h"', '#include "fns.h"', '#include "io.h"',
           '#include "error.h"', ""]
    out += ["void (*i8237alloc)(void) = nil;", f"int i8237dma = {i8237dma};", ""]
    out += [f"extern void {n}link(void);" for n in links]
    out += ["void links(void){", "\tbootlinks();"]
    out += [f"\t{n}link();" for n in links] + ["}", ""]
    out += [f"extern PCArch {n};" for n in arch]
    out += ["PCArch* knownarch[] = {"] + [f"\t&{n}," for n in arch] + ["\tnil,", "};", ""]
    if sdifc:
        out += ['#include "sd.h"']
        out += [f"extern SDifc {n}ifc;" for n in sdifc]
        out += ["SDifc* sdifc[] = {"] + [f"\t&{n}ifc," for n in sdifc] + ["\tnil,", "};", ""]
    if not ramdisk:
        out += ["void ramdiskinit(void)", "{", "}", ""]
    if devuart:
        out += [f"extern PhysUart {n};" for n in physuart]
        out += ["PhysUart* physuart[] = {"] + [f"\t&{n}," for n in physuart] + ["\tnil,", "};", ""]
        if "i8250physuart" not in physuart:
            out += ["void i8250console(void)", "{", "}", ""]
    out += ["void bootscreeninit(void)", "{", "}", ""]
    if ips:
        out += ['#include "ip.h"']
        out += [f"extern void {n}init(Fs*);" for n in ips]
        out += ["void (*ipprotoinit[])(Fs*) = {"] + [f"\t{n}init," for n in ips] + ["\tnil,", "};", ""]
    out += ports + [""]
    out += ["void dtracytick(Ureg*)", "{", "}", ""]
    cfgbytes = (ROOT / "kernel/amd64/pc64").read_bytes()
    out += ["uchar configfile[]={"]
    for i in range(0, len(cfgbytes), 16):
        out.append(",".join(f"0x{b:02x}" for b in cfgbytes[i:i+16]) + ",")
    out += ["0,", "};", ""]
    out += ['char* conffile = "kernel/amd64/pc64";', "ulong kerndate = KERNDATE;", ""]
    (GENDIR / "pc64.c").write_text("\n".join(out) + "\n")


def make_pc64_rootc() -> None:
    out = ['#include "u.h"', '#include "lib.h"', '#include "mem.h"',
           '#include "dat.h"', '#include "fns.h"', '#include "io.h"',
           '#include "error.h"', "", "void bootlinks(void){", "}", ""]
    (GENDIR / "pc64.rootc.c").write_text("\n".join(out) + "\n")


USER_SYSCALLS = [
    ("alarm", 1, "ALARM"),
    ("sleep", 1, "SLEEP"),
    ("exits", 1, "EXITS"),
    ("ovolopen", 2, "OVOLOPEN"),
    ("ospaceopen", 3, "OSPACEOPEN"),
    ("ospaceget", 5, "OSPACEGET"),
    ("oexecute", 4, "OEXECUTE"),
]


def syscall_numbers() -> dict[str, int]:
    text = (ROOT / "runtime/abi/sys.h").read_text()
    out = {}
    for line in text.splitlines():
        m = re.match(r"#define\s+([A-Z0-9_]+)\s+([0-9]+)", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def make_user_stubs() -> Path:
    nums = syscall_numbers()
    lines = [".text"]
    for name, nargs, macro in USER_SYSCALLS:
        num = nums[macro]
        lines.append(f".globl {name}")
        lines.append(f".type {name},@function")
        lines.append(f"{name}:")
        lines.append(f"\tsubq ${(nargs+2)*8}, %rsp")
        lines.append("\tmovq %rbp, 0(%rsp)")
        regs = ["%rdi", "%rsi", "%rdx", "%rcx", "%r8"]
        for i in range(nargs):
            lines.append(f"\tmovq {regs[i]}, {8+i*8}(%rsp)")
        lines.append(f"\tmovq ${num}, %rbp")
        lines.append("\tsyscall")
        lines.append("\tmovq 0(%rsp), %rbp")
        lines.append(f"\taddq ${(nargs+2)*8}, %rsp")
        lines.append("\tret")
        lines.append(f".size {name}, .-{name}")
    lines.append('.section .note.GNU-stack,"",@progbits')
    path = GENDIR / "user_stubs.S"
    path.write_text("\n".join(lines) + "\n")
    return path


def make_user_image(name: str, src: Path, stubs: Path) -> None:
    obj = GENDIR / (name + ".o")
    stubobj = GENDIR / "user_stubs.o"
    elf = GENDIR / (name + ".elf")
    binp = GENDIR / (name + ".bin")
    ldscript = GENDIR / "user.ld"
    ldscript.write_text(
        "ENTRY(_main)\n"
        "SECTIONS { . = 0x200028; .text : { *(.text*) *(.rodata*) }\n"
        " /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) } }\n")
    if not stubobj.is_file():
        subprocess.run([CC_DEFAULT, "-c", "-o", str(stubobj), "-m64", "-fno-pie", str(stubs)],
                       check=True, cwd=ROOT)
    flags = ["-DPLAN2001_GCC", "-include", str(ROOT / "kernel/compat/plan2001-gcc.h"),
             "-std=gnu11", "-m64", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-pie", "-fno-pic",
             "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
             "-mno-red-zone", "-mgeneral-regs-only",
             "-I", str(ROOT / "include/amd64"), "-I", str(ROOT / "include")]
    subprocess.run([CC_DEFAULT, "-c", "-o", str(obj)] + flags + [str(src)],
                   check=True, cwd=ROOT)
    subprocess.run(["ld", "-T", str(ldscript), "-e", "_main", "-o", str(elf),
                    str(obj), str(stubobj)], check=True, cwd=ROOT)
    subprocess.run(["objcopy", "-O", "binary", str(elf), str(binp)],
                   check=True, cwd=ROOT)
    code = binp.read_bytes()
    if len(code) == 0 or len(code) > 4096 - 40:
        raise SystemExit(f"{name}: user bootstrap code size {len(code)} does not fit")
    import struct
    magic = 0x8000 | (((4 * 26) + 0) * 26 + 7)
    hdr = struct.pack(">8I", magic, len(code), 0, 0, 0, 0, 0, 0)
    ent = struct.pack(">Q", 0x200000 + 40)
    data = hdr + ent + code
    lines = [f"uchar {name}[]={{"]
    for i in range(0, len(data), 16):
        lines.append(",".join(f"0x{b:02x}" for b in data[i:i+16]) + ",")
    lines.append("};")
    (GENDIR / (name + ".i")).write_text("\n".join(lines) + "\n")


def make_binary_blob(name: str, src: Path, addr: int, entry: str) -> None:
    obj = GENDIR / (name + ".o")
    elf = GENDIR / (name + ".elf")
    binp = GENDIR / (name + ".bin")
    ldscript = GENDIR / (name + ".ld")
    ldscript.write_text(
        f"ENTRY({entry})\n"
        f"SECTIONS {{ . = 0x{addr:x}; .text : {{ *(.text*) *(.rodata*) }}\n"
        " /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) } }\n")
    subprocess.run([CC_DEFAULT, "-c", "-o", str(obj), "-m64", "-mcmodel=kernel",
                    "-fno-pie", str(src)], check=True, cwd=ROOT)
    subprocess.run(["ld", "-T", str(ldscript), "-o", str(elf), str(obj)],
                   check=True, cwd=ROOT)
    subprocess.run(["objcopy", "-O", "binary", "--only-section=.text", str(elf), str(binp)],
                   check=True, cwd=ROOT)
    data = binp.read_bytes()
    lines = [f"uchar {name}[]={{"]
    for i in range(0, len(data), 16):
        lines.append(",".join(f"0x{b:02x}" for b in data[i:i+16]) + ",")
    lines.append("};")
    (GENDIR / (name + ".i")).write_text("\n".join(lines) + "\n")


def make_generated() -> None:
    GENDIR.mkdir(parents=True, exist_ok=True)
    make_kbuildid()
    make_errstr()
    make_systab_gcc()
    make_pc64()
    make_pc64_rootc()
    make_binary_blob("apbootstrap", ROOT / "kernel/amd64/gcc/apbootstrap.S",
                     0x7000, "_ap_start")
    make_binary_blob("rebootcode", ROOT / "kernel/amd64/gcc/rebootcode.S",
                     0x11000, "_reboot_entry")
    stubs = make_user_stubs()
    make_user_image("obootoid", ROOT / "kernel/amd64/obootoid.c", stubs)


def base_command(cc: str, src: Path, obj: Path, extra: list[str] | None = None) -> list[str]:
    cmd = [cc, "-c", "-o", str(obj)]
    cmd += ["-DPLAN2001_GCC", "-include", str(ROOT / "kernel/compat/plan2001-gcc.h")]
    cmd += CFLAGS
    cmd += extra or []
    if src.name in ("ocontext.c", "sysgefs.c", "relations-kernel.c", "tcp.c", "systcp6.c"):
        # Kernel entry stacks retain Plan 9 alignment; namespace C must not
        # introduce aligned SSE stack stores or borrow the up/Mach registers.
        cmd += ["-mgeneral-regs-only", "-ffixed-r14", "-ffixed-r15"]
    if src.name in ("espctl.c", "espcontrol.c", "esp6.c"):
        cmd += ["-g", "-O2", "-mgeneral-regs-only", "-ffixed-r14", "-ffixed-r15"]
    for inc in INCLUDES:
        cmd += ["-I", str(inc)]
    cmd.append(str(src))
    return cmd


def generated_missing(src: Path) -> str:
    text = src.read_text(errors="replace")
    for name, generator in GENERATED_INCLUDES.items():
        if re.search(rf'#\s*include\s*[<"]([^">]*/)?{re.escape(name)}[">]', text):
            found = (src.parent / name).is_file() or (GENDIR / name).is_file()
            if not found:
                return f"{name}: {generator}"
    for marker, generator in (("rebootcode.i", ""), ("apbootstrap.i", ""),
                              ("obootoid.i", "")):
        if f'"{marker}"' in text and not (
                (src.parent / marker).is_file() or (GENDIR / marker).is_file()):
            return f"{marker}: {GENERATED_INCLUDES.get(marker, generator)}"
    return ""


def classify(output: str) -> tuple[str, str]:
    lines = output.splitlines()
    first = next((ln.strip() for ln in lines
                  if "error:" in ln or "fatal error:" in ln), "")
    firstlow = first.lower()
    low = output.lower()
    if "no such file or directory" in firstlow or "file not found" in firstlow:
        return "headers", first
    if "va_list" in firstlow or "va_arg" in firstlow or "va_start" in firstlow or "builtin_va" in firstlow:
        return "varargs", first
    if "incompatible pointer type" in firstlow or "incompatible types" in firstlow:
        return "ABI", first
    if "duplicate member" in firstlow or "conflicting types" in firstlow:
        return "types", first
    if "storage class" in firstlow or "register" in firstlow:
        return "extern register", first
    if "#pragma" in firstlow:
        return "pragmas", first
    if "no such file or directory" in low or "file not found" in low:
        return "headers", first
    if "va_list" in low or "va_arg" in low or "va_start" in low or "builtin_va" in low:
        return "varargs", first
    if "duplicate member" in low or "conflicting types" in low:
        return "types", first
    return "other", first


def compile_unit(unit: Unit, cc: str, objdir: Path, logdir: Path) -> Result:
    obj = objdir / f"{unit.obj}.o"
    log = logdir / f"{unit.obj}.log"
    cmdlog = logdir / f"{unit.obj}.cmd"
    missing = generated_missing(unit.src)
    obj.parent.mkdir(parents=True, exist_ok=True)
    log.parent.mkdir(parents=True, exist_ok=True)
    if missing:
        msg = f"NOT ATTEMPTED: missing generated input {missing}\n"
        log.write_text(msg)
        cmdlog.write_text("# not invoked: generated input missing\n")
        return Result(unit, "NOT ATTEMPTED", "generated source", missing, log)
    if unit.src.suffix in (".S", ".s"):
        cmd = [cc, "-c", "-o", str(obj), "-m64", "-mcmodel=kernel",
               "-fno-pie", "-mno-red-zone", str(unit.src)]
    else:
        cmd = base_command(cc, unit.src, obj, unit.extra_flags)
    cmdlog.write_text(shlex.join(cmd) + "\n")
    t0 = time.monotonic()
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    seconds = time.monotonic() - t0
    log.write_text(
        f"# {shlex.join(cmd)}\n"
        f"# exit={p.returncode} seconds={seconds:.3f}\n\n"
        f"{p.stdout}{p.stderr}"
    )
    if p.returncode == 0 and obj.is_file():
        return Result(unit, "PASS", "", "", log, seconds)
    cat, first = classify(p.stdout + p.stderr)
    if not first:
        first = (p.stdout + p.stderr).strip().splitlines()[0] if (p.stdout + p.stderr).strip() else "gcc failed"
    first = first.replace(str(ROOT) + "/", "")
    return Result(unit, "FAIL", cat, first, log, seconds)


# ---------------------------------------------------------------------------
# manifest, smoke check, report
# ---------------------------------------------------------------------------

def unit_group(obj: str, src: Path) -> str:
    if src.suffix in (".S", ".s"):
        return "ASM"
    rel = src.relative_to(ROOT).as_posix()
    if rel.startswith("ge/storage/"):
        return "GEFS"
    if rel.startswith("kernel/amd64/"):
        return "ARCH"
    if rel.startswith("kernel/net/"):
        return "NET"
    return "CORE"


def manifest_units() -> list[Unit]:
    units: list[Unit] = []
    for obj, rel in KERNEL_SOURCES.items():
        src = ROOT / rel
        if not src.is_file():
            raise SystemExit(f"kernel manifest source missing: {obj} -> {rel}")
        units.append(Unit(unit_group(obj, src), obj, src, rel, True,
                          "explicit GCC kernel manifest"))
    return sorted(units, key=lambda u: u.obj)


def build_selected() -> list[Unit]:
    return manifest_units()


def probe_units(selected: list[Unit]) -> tuple[list[Unit], list[tuple[str, str]]]:
    extra = [(obj, GENERATED_UNITS[obj]) for obj in sorted(GENERATED_UNITS)]
    return [], extra


def write_manifest(selected: list[Unit]) -> None:
    path = BUILD / "manifest.tsv"
    lines = ["group\tobject\tsource\tcanonical\tnote"]
    for u in selected:
        lines.append(f"{u.group}\t{u.obj}\t{u.relsrc}\t{u.canonical}\t{u.note}")
    path.write_text("\n".join(lines) + "\n")


def external_headers(cc: str, units: list[Unit], jobs: int) -> dict[str, list[str]]:
    """Return absolute headers outside the repository (host/glibc leakage)."""

    def one(unit: Unit) -> tuple[str, list[str]]:
        cmd = [cc, "-M"]
        cmd += ["-DPLAN2001_GCC", "-include", str(ROOT / "kernel/compat/plan2001-gcc.h")]
        cmd += CFLAGS
        for inc in INCLUDES:
            cmd += ["-I", str(inc)]
        cmd.append(str(unit.src))
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
        if p.returncode != 0:
            return unit.obj, [f"<preprocess failed: {p.stderr.splitlines()[0] if p.stderr else 'unknown'}>"]
        text = p.stdout.replace("\\\n", " ")
        bad = []
        for tok in text.split():
            if not tok.endswith(".h") or not tok.startswith("/"):
                continue
            if tok.startswith(str(ROOT) + "/"):
                continue
            bad.append(tok)
        return unit.obj, bad

    found: dict[str, list[str]] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for obj, bad in ex.map(one, units):
            if bad:
                found[obj] = bad
    return found


def smoke_check(selected_results: list[Result]) -> tuple[str, str]:
    built = [r for r in selected_results if r.status == "PASS"]
    if not built:
        return "FAIL", "no selected object was produced"
    obj = OBJDIR / f"{built[0].unit.obj}.o"
    lines = []
    ok = True
    if shutil.which("readelf"):
        p = subprocess.run(["readelf", "-h", str(obj)], capture_output=True, text=True, errors="replace")
        out = p.stdout + p.stderr
        lines.append(out.strip())
        for needle in ("ELF64", "Advanced Micro Devices X86-64", "REL (Relocatable file)"):
            if needle not in out:
                ok = False
                lines.append(f"MISSING: {needle}")
    elif shutil.which("file"):
        p = subprocess.run(["file", str(obj)], capture_output=True, text=True, errors="replace")
        out = p.stdout + p.stderr
        lines.append(out.strip())
        ok = ("ELF 64-bit" in out and "x86-64" in out and "relocatable" in out)
    else:
        return "NOT RUN", "neither readelf nor file is installed"
    return ("PASS" if ok else "FAIL"), "\n".join(lines)


def write_report(cc: str, version: str, selected: list[Unit],
                 selres: list[Result], probe: list[Result],
                 extra_generated: list[tuple[str, str]],
                 smoke_status: str, smoke_detail: str,
                 ext_headers: dict[str, list[str]]) -> None:
    passed = [r for r in selres if r.status == "PASS"]
    failed = [r for r in selres if r.status != "PASS"]
    lines: list[str] = []
    lines.append("Plan2001-O N9.1 Linux GCC compile-only report")
    lines.append("=" * 48)
    lines.append(f"generated: {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}")
    lines.append(f"cc: {cc}")
    lines.append(f"version: {version}")
    lines.append(f"include order: {' '.join(str(p.relative_to(ROOT)) for p in INCLUDES)}")
    lines.append(f"flags: {' '.join(CFLAGS)}")
    lines.append("")
    lines.append(f"selected subset: {len(passed)}/{len(selected)} PASS")
    for r in selres:
        if r.status != "PASS":
            lines.append(f"  {r.status}\t{r.unit.obj}\t{r.category}\t{r.detail}")
    lines.append(f"ELF smoke check: {smoke_status}")
    for ln in smoke_detail.splitlines():
        lines.append(f"  {ln}")
    if ext_headers:
        lines.append(f"external header check: FAIL ({len(ext_headers)} units)")
        for obj, heads in sorted(ext_headers.items()):
            lines.append(f"  {obj}: {', '.join(heads)}")
    else:
        lines.append("external header check: PASS (no host/glibc headers in selected units)")
    lines.append("")
    lines.append("broader canonical closure probe (does NOT affect selected PASS criteria)")
    lines.append("status\tcategory\tobject\tsource\tdetail")
    for r in probe:
        lines.append(f"{r.status}\t{r.category}\t{r.unit.obj}\t{r.unit.relsrc}\t{r.detail}")
    for obj, detail in extra_generated:
        lines.append(f"NOT ATTEMPTED\tgenerated source\t{obj}\t-\t{detail}")
    counts: dict[str, int] = {}
    for r in probe:
        counts[r.status] = counts.get(r.status, 0) + 1
    summary = ", ".join(f"{k}={v}" for k, v in sorted(counts.items())) or "none"
    lines.append("")
    lines.append(f"probe summary: {summary}, generated-only={len(extra_generated)}")
    lines.append("")
    lines.append("Note: the selected subset is emission-compatible C, not a linked kernel.")
    (BUILD / "report.txt").write_text("\n".join(lines) + "\n")


FULLDIR = BUILD / "full"
FULLLOG = BUILD / "full-log"


def full_units() -> list[Unit]:
    """The explicit manifest plus the generated C units."""
    units = manifest_units()
    units.append(Unit("GEN", "pc64", GENDIR / "pc64.c", "config", True,
                      "generated pc64.c from kernel/amd64/pc64",
                      ["-DKERNDATE=0"]))
    units.append(Unit("GEN", "pc64.rootc", GENDIR / "pc64.rootc.c", "config", True,
                      "generated pc64.rootc.c"))
    return units


def write_full_report(cc: str, version: str, results: list[Result]) -> None:
    lines = ["Plan2001-O N9.2 full C/assembly readiness report", "=" * 54,
             f"generated: {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}",
             f"cc: {cc}", f"version: {version}",
             f"flags: {' '.join(CFLAGS)}", ""]
    counts: dict[str, int] = {}
    groups: dict[str, dict[str, int]] = {}
    for r in results:
        counts[r.status] = counts.get(r.status, 0) + 1
        g = groups.setdefault(r.unit.group, {})
        g[r.status] = g.get(r.status, 0) + 1
    lines.append("summary: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    for g in sorted(groups):
        lines.append(f"  {g}: " + ", ".join(f"{k}={v}" for k, v in sorted(groups[g].items())))
    lines.append("")
    lines.append("status\tcategory\tobject\tgroup\tsource\tdetail")
    for r in results:
        lines.append(f"{r.status}\t{r.category}\t{r.unit.obj}\t{r.unit.group}\t"
                     f"{r.unit.relsrc}\t{r.detail}")
    lines.append("")
    lines.append("The full closure is INCOMPLETE unless every requested unit is PASS.")
    lines.append("NOT ATTEMPTED units are missing generated inputs, not C PASSes.")
    (BUILD / "full-report.txt").write_text("\n".join(lines) + "\n")


def symbol_sets(objects: list[Path]) -> tuple[set[str], set[str], dict[str, list[str]], int]:
    undef: set[str] = set()
    defined_all: set[str] = set()
    dup: dict[str, list[str]] = {}
    for obj in objects:
        p = subprocess.run(["nm", "-u", str(obj)], capture_output=True, text=True, errors="replace")
        for line in p.stdout.splitlines():
            parts = line.split()
            if parts:
                undef.add(parts[-1])
        p = subprocess.run(["nm", "-g", "--defined-only", str(obj)],
                           capture_output=True, text=True, errors="replace")
        for line in p.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3:
                sym, typ = parts[-1], parts[-2]
                defined_all.add(sym)
                if typ not in ("C", "W", "V"):
                    dup.setdefault(sym, []).append(obj.name)
    return undef, defined_all, dup, len(objects)


def link_audit(results: list[Result]) -> tuple[str, str]:
    objects = [FULLDIR / f"{r.unit.obj}.o" for r in results if r.status == "PASS"]
    lines = ["Plan2001-O N9.2 linker-readiness audit", "=" * 40,
             f"objects available: {len(objects)}"]
    incomplete = [r for r in results if r.status != "PASS"]
    lines.append(f"incomplete requested units: {len(incomplete)}")
    for r in incomplete:
        lines.append(f"  {r.status}\t{r.unit.obj}\t{r.detail}")
    if not objects:
        lines.append("no objects; audit empty")
        return "FAIL", "\n".join(lines)
    undef, defined, duplicates, nobj = symbol_sets(objects)
    unresolved = sorted(undef - defined)
    duplicates = {k: v for k, v in duplicates.items() if len(v) > 1}
    helpers = [s for s in unresolved if s.startswith("__") or
               s in ("memset", "memmove", "memcpy", "memcmp", "strlen", "strcmp",
                     "strcpy", "strncpy", "strcat", "strchr", "strncmp")]
    relocs: set[str] = set()
    for obj in objects:
        p = subprocess.run(["readelf", "-r", str(obj)], capture_output=True, text=True, errors="replace")
        for line in p.stdout.splitlines():
            m = re.search(r"(R_X86_64_[A-Z0-9_]+)", line)
            if m:
                relocs.add(m.group(1))
    lines.append(f"undefined symbols: {len(unresolved)}")
    for s in unresolved[:120]:
        lines.append(f"  U {s}")
    if len(unresolved) > 120:
        lines.append(f"  ... {len(unresolved)-120} more")
    lines.append(f"duplicate defined symbols: {len(duplicates)}")
    for k, v in sorted(duplicates.items())[:40]:
        lines.append(f"  D {k}: {', '.join(v)}")
    lines.append("compiler/runtime helper dependencies:")
    for s in helpers[:60]:
        lines.append(f"  H {s}")
    if not helpers:
        lines.append("  none")
    lines.append("relocation types: " + ", ".join(sorted(relocs)))
    lines.append("layout constraints: KTZERO=0xffffffff80110000, KZERO=0xffffffff80000000")
    lines.append("entry symbols: _efi64 and syscallentry are provided by GNU asm")
    lines.append("link: NOT ATTEMPTED in --full (use --kernel for the real link)")
    status = "PASS" if not incomplete and not unresolved and not duplicates else "INCOMPLETE"
    return status, "\n".join(lines)


def abi_check(cc: str) -> tuple[str, str]:
    src = BUILD / "abi-check.c"
    obj = BUILD / "abi-check.o"
    src.write_text(r"""
#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include <a.out.h>
#include <tos.h>

#define CHK(name, cond) _Static_assert((cond), #name)
CHK(uchar_is_1, sizeof(uchar) == 1);
CHK(ushort_is_2, sizeof(ushort) == 2);
CHK(uint_is_4, sizeof(uint) == 4);
CHK(ulong_is_4, sizeof(ulong) == 4);
CHK(vlong_is_8, sizeof(vlong) == 8);
CHK(uvlong_is_8, sizeof(uvlong) == 8);
CHK(ptr_is_8, sizeof(void*) == 8);
#ifdef PLAN2001_GCC
CHK(label_size, sizeof(Label) == 48);
CHK(label_rbx, offsetof(Label, rbx) == 16);
CHK(label_rbp, offsetof(Label, rbp) == 24);
CHK(label_r12, offsetof(Label, r12) == 32);
CHK(label_r13, offsetof(Label, r13) == 40);
#else
CHK(label_size, sizeof(Label) == 16);
#endif
CHK(label_sp, offsetof(Label, sp) == 0);
CHK(label_pc, offsetof(Label, pc) == 8);
CHK(sargs_size, sizeof(Sargs) == MAXSYSARG*BY2WD);
CHK(sargs_args, offsetof(Sargs, args) == 0);
CHK(ureg_ax, offsetof(Ureg, ax) == 0);
CHK(ureg_r14, offsetof(Ureg, r14) == 13*8);
CHK(ureg_r15, offsetof(Ureg, r15) == 14*8);
CHK(ureg_type, offsetof(Ureg, type) == 128);
CHK(ureg_ss, offsetof(Ureg, ss) == 176);
CHK(ureg_size, sizeof(Ureg) == 184);
CHK(lock_key, offsetof(Lock, key) == 0);
CHK(lock_pc, offsetof(Lock, pc) == 8);
CHK(lock_p, offsetof(Lock, p) == 16);
CHK(lock_m, offsetof(Lock, m) == 24);
CHK(rendez_p, offsetof(Rendez, p) == sizeof(Lock));
CHK(mach_splpc, offsetof(Mach, splpc) == 8);
CHK(exec_size, sizeof(Exec) == 32);
CHK(tos_cyclefreq, offsetof(Tos, cyclefreq) == 40);
CHK(bootinfo_magic, offsetof(BootInfo, magic) == 0);
CHK(bootinfo_acpi, offsetof(BootInfo, acpi) == 48);
CHK(bootinfo_epoch, offsetof(BootInfo, epoch) == 64);

Lock abi_Lock; Rendez abi_Rendez; Rendezq abi_Rendezq; Sema abi_Sema;
Proc *abi_Proc; Mach *abi_Mach; Conf abi_Conf; Sargs abi_Sargs;
Ureg abi_Ureg; Label abi_Label; Segdesc abi_Segdesc;
Exec abi_Exec; Tos abi_Tos; BootInfo abi_BootInfo;
""")
    cmd = [cc, "-c", "-g", "-fno-eliminate-unused-debug-types",
           "-o", str(obj), "-DPLAN2001_GCC",
           "-include", str(ROOT / "kernel/compat/plan2001-gcc.h")]
    cmd += CFLAGS
    for inc in INCLUDES:
        cmd += ["-I", str(inc)]
    cmd.append(str(src))
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    lines = ["Plan2001-O N9.2 GCC/Plan 9 C ABI check", "=" * 34,
             f"cc: {cc}", "compile-only layout verification; not a boot proof.", ""]
    if p.returncode != 0:
        lines.append("static/sizeof checks: FAIL")
        lines.append(p.stdout + p.stderr)
        (BUILD / "abi-report.txt").write_text("\n".join(lines) + "\n")
        return "FAIL", "\n".join(lines)
    lines.append("static/sizeof/offsetof checks: PASS")
    gsrc = BUILD / "gefs-abi-check.c"
    gobj = BUILD / "gefs-abi-check.o"
    gsrc.write_text(r"""
#include "gefs.h"
#define CHK(name, cond) _Static_assert((cond), #name)
CHK(gefs_key_size, sizeof(Key) == 16);
CHK(gefs_val_size, sizeof(Val) == 16);
CHK(gefs_kvp_size, sizeof(Kvp) == 32);
CHK(gefs_bptr_size, sizeof(Bptr) == 24);
Bptr abi_Bptr; Kvp abi_Kvp;
""")
    gcmd = [cc, "-c", "-g", "-fno-eliminate-unused-debug-types",
            "-o", str(gobj), "-DPLAN2001_GCC",
            "-include", str(ROOT / "kernel/compat/plan2001-gcc.h")]
    gcmd += CFLAGS
    for inc in INCLUDES:
        gcmd += ["-I", str(inc)]
    gcmd.append(str(gsrc))
    gp = subprocess.run(gcmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    if gp.returncode == 0:
        lines.append("GE/O in-memory/on-disk helper checks: PASS (Key=16 Val=16 Kvp=32 Bptr=24)")
    else:
        lines.append("GE/O helper checks: FAIL")
        lines.append(gp.stdout + gp.stderr)
    lines.append("")
    lines.append("Plan 9 C reference widths: char=1 short=2 int=4 long=4 vlong=8 pointer=8")
    lines.append("GCC selected widths: uchar=1 ushort=2 uint=4 ulong=4 vlong=8 pointer=8")
    lines.append("literal C long under GCC remains 8; kernel-internal LP64 audit continues.")
    if shutil.which("gdb"):
        for typ in ("Lock", "Rendez", "Proc", "Mach", "Conf", "Sargs", "Ureg",
                    "Label", "Exec", "Tos", "BootInfo"):
            q = subprocess.run(["gdb", "-q", "-batch", "-ex",
                                f"ptype /o struct {typ}", str(obj)],
                               capture_output=True, text=True, errors="replace")
            text = q.stdout.strip()
            if text:
                lines.append("")
                lines.append(f"--- struct {typ} ---")
                lines.append(text)
    else:
        lines.append("gdb: NOT AVAILABLE; struct layout dump skipped")
    (BUILD / "abi-report.txt").write_text("\n".join(lines) + "\n")
    return "PASS", "\n".join(lines)


def register_proof(results: list[Result]) -> tuple[str, str]:
    objects = [FULLDIR / f"{r.unit.obj}.o" for r in results
               if r.status == "PASS" and r.unit.group != "ASM"]
    bad_syms: list[str] = []
    bad_relocs: list[str] = []
    use14 = 0
    use15 = 0
    for obj in objects:
        p = subprocess.run(["nm", "-g", str(obj)], capture_output=True,
                           text=True, errors="replace")
        for line in p.stdout.splitlines():
            parts = line.split()
            if parts and parts[-1] in ("m", "up"):
                bad_syms.append(f"{obj.name}: {line.strip()}")
        p = subprocess.run(["readelf", "-r", str(obj)], capture_output=True,
                           text=True, errors="replace")
        for line in p.stdout.splitlines():
            parts = line.split()
            if parts and parts[-1] in ("m", "up"):
                bad_relocs.append(f"{obj.name}: {line.strip()}")
        p = subprocess.run(["objdump", "-d", str(obj)], capture_output=True,
                           text=True, errors="replace")
        text = p.stdout
        if "%r14" in text or "%R14" in text:
            use14 += 1
        if "%r15" in text or "%R15" in text:
            use15 += 1
    lines = ["Plan2001-O N9.2 R14/R15 global-register proof", "=" * 44,
             f"objects inspected: {len(objects)}",
             "GCC declarations: register Mach *m asm(\"r15\"); register Proc *up asm(\"r14\");",
             f"objects with %r14 references: {use14}",
             f"objects with %r15 references: {use15}",
             f"memory symbols named m/up: {len(bad_syms)}",
             f"relocations against symbols m/up: {len(bad_relocs)}"]
    for x in (bad_syms + bad_relocs)[:40]:
        lines.append("  " + x)
    lines.append("")
    lines.append("Caveat: register ownership is a compile-time invariant.  Entry,")
    lines.append("interrupt and context-switch assembly must establish/preserve R14/R15;")
    lines.append("the GNU entry/interrupt port is not complete in N9.2.")
    status = "PASS" if not bad_syms and not bad_relocs and use14 and use15 else "INCOMPLETE"
    return status, "\n".join(lines)


LIBOBJ = BUILD / "gcc-lib"
LIBLOG = BUILD / "gcc-lib-log"
KERNELELF = BUILD / "9pc64.elf"
LIB_SETS = {
    "libc": [ROOT / "runtime/libc/port", ROOT / "runtime/libc/fmt", ROOT / "runtime/libc/9sys"],
    "libip": [ROOT / "runtime/ip"],
    "libsec": [ROOT / "runtime/crypto"],
    "libesp": [ROOT / "runtime/ikecrypto"],
    "libmp": [ROOT / "runtime/mp"],
    "libaml": [ROOT / "runtime/aml"],
    "libfis": [ROOT / "runtime/fis"],
}
LIB_SKIP = {"aesXCBmac.c", "crttest.c", "testreadipifc.c"}


def build_gcc_libraries(cc: str, jobs: int) -> tuple[dict[str, Path], list[Result]]:
    LIBOBJ.mkdir(parents=True, exist_ok=True)
    LIBLOG.mkdir(parents=True, exist_ok=True)
    results: list[Result] = []
    units: list[Unit] = []
    for lib, dirs in LIB_SETS.items():
        for d in dirs:
            for src in sorted(d.rglob("*.c")):
                if lib == "libesp" and src.name not in {
                    "espcrypto.c", "aes_ct.c", "aes_ct_enc.c", "aes_ct_ctr.c", "ghash_ctmul.c", "gcm.c"
                }:
                    continue
                if src.name in LIB_SKIP:
                    continue
                rel = src.relative_to(ROOT).as_posix().replace("/", "_")
                obj = f"{lib}__{rel[:-2]}.o"
                units.append(Unit(lib, obj, src, lib, False, "GCC kernel library source"))
    def one(u: Unit) -> Result:
        obj = LIBOBJ / f"{u.obj}.o"
        log = LIBLOG / f"{u.obj}.log"
        cmd = [cc, "-c", "-o", str(obj),
               "-DPLAN2001_GCC", "-include", str(ROOT / "kernel/compat/plan2001-gcc.h")]
        cmd += CFLAGS
        cmd += ["-I", str(ROOT / "include/amd64"), "-I", str(ROOT / "include")]
        if u.group == "libsec":
            cmd += ["-I", str(ROOT / "runtime/mp/port")]
        if u.group == "libesp":
            cmd += ["-O2", "-mgeneral-regs-only", "-ffixed-r14", "-ffixed-r15", "-I", str(ROOT / "runtime/ikecrypto/bearssl/inc"),
                    "-I", str(ROOT / "runtime/ikecrypto/bearssl/src")]
        cmd.append(str(u.src))
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
        log.write_text(shlex.join(cmd) + "\n\n" + p.stdout + p.stderr)
        if p.returncode == 0:
            return Result(u, "PASS", "", "", log)
        cat, first = classify(p.stdout + p.stderr)
        return Result(u, "FAIL", cat, first.replace(str(ROOT) + "/", ""), log)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(one, units))
    archives: dict[str, Path] = {}
    for lib in LIB_SETS:
        objs = [str(LIBOBJ / f"{r.unit.obj}.o") for r in results
                if r.unit.group == lib and r.status == "PASS"]
        ap = BUILD / f"{lib}-gcc.a"
        if ap.exists():
            ap.unlink()
        if objs:
            subprocess.run(["ar", "rcs", str(ap)] + objs, check=True, cwd=ROOT)
            archives[lib] = ap
    return archives, results


def check_kernel_elf(elf: Path) -> tuple[bool, list[str]]:
    lines: list[str] = []
    ok = True
    if not elf.is_file():
        return False, ["kernel ELF missing"]
    h = subprocess.run(["readelf", "-h", str(elf)], capture_output=True, text=True,
                       errors="replace")
    lines.append(h.stdout.strip())
    for needle in ("Class:                             ELF64",
                   "Type:                              EXEC",
                   "Machine:                           Advanced Micro Devices X86-64"):
        if needle not in h.stdout:
            ok = False
            lines.append("MISSING: " + needle)
    p = subprocess.run(["readelf", "-lW", str(elf)], capture_output=True, text=True,
                       errors="replace")
    lines.append(p.stdout.strip())
    if "LOAD" not in p.stdout or "GNU_STACK" not in p.stdout:
        ok = False
        lines.append("MISSING PT_LOAD/PT_GNU_STACK")
    if "INTERP" in p.stdout or "DYNAMIC" in p.stdout:
        ok = False
        lines.append("unexpected INTERP/DYNAMIC segment")
    d = subprocess.run(["readelf", "-d", str(elf)], capture_output=True, text=True,
                       errors="replace")
    if "Dynamic section" in (d.stdout + d.stderr):
        ok = False
        lines.append("unexpected dynamic section")
    sec = subprocess.run(["readelf", "-SW", str(elf)], capture_output=True, text=True,
                         errors="replace")
    for bad in (".interp", ".dynamic", ".plt", ".got.plt", ".rela.plt"):
        if bad in sec.stdout:
            ok = False
            lines.append("unexpected section " + bad)
    und = subprocess.run(["nm", "-u", str(elf)], capture_output=True, text=True,
                         errors="replace")
    if und.stdout.strip():
        ok = False
        lines.append("undefined symbols remain:")
        lines.append(und.stdout.strip())
    if not ok:
        return False, lines
    # entry must be inside an executable PT_LOAD
    m = re.search(r"Entry point address:\s+0x([0-9a-fA-F]+)", h.stdout)
    entry = int(m.group(1), 16) if m else 0
    loads = []
    for line in p.stdout.splitlines():
        f = line.split()
        if len(f) >= 8 and f[0] == "LOAD":
            try:
                flags = " ".join(t for t in f[6:] if not t.startswith("0x"))
                loads.append((int(f[2], 16), int(f[3], 16), int(f[4], 16),
                              int(f[5], 16), flags))
            except ValueError:
                pass
    ok = any("E" in execflag and v <= entry < v + msz
             for v, paddr, fsz, msz, execflag in loads)
    if not ok:
        lines.append("entry is not inside an executable PT_LOAD")
    fsize = elf.stat().st_size
    for i, (v, paddr, fsz, msz, flags) in enumerate(loads):
        if fsz > msz:
            ok = False
            lines.append("PT_LOAD filesz > memsz")
        if paddr + msz < paddr:
            ok = False
            lines.append("PT_LOAD address overflow")
        if v != paddr + 0xffffffff80000000:
            ok = False
            lines.append(f"PT_LOAD {i}: vaddr != paddr + KZERO")
        for j in range(i):
            ov, op, of, om, ofl = loads[j]
            if paddr < op + om and op < paddr + msz:
                ok = False
                lines.append(f"PT_LOAD {i} overlaps PT_LOAD {j}")
    if loads and max(p + m for _, p, _, m, _ in loads) > fsize:
        pass  # file size does not bound physical memory size
    lines.append(f"entry=0x{entry:x} PT_LOADs={len(loads)}")
    return ok, lines


def run_kernel(cc: str, jobs: int) -> int:
    print("building GCC kernel libraries")
    archives, libresults = build_gcc_libraries(cc, jobs)
    libfail = [r for r in libresults if r.status != "PASS"]
    for r in libfail:
        print(f"LIB FAIL {r.unit.relsrc}: {r.category}: {r.detail}")
    objects = sorted(str(p) for p in FULLDIR.glob("*.o"))
    if not objects:
        print("kernel link: FAIL (no full-closure objects)")
        return 1
    cmd = [shutil.which("ld") or "ld", "-T", str(ROOT / "tools/host/kernel.ld"),
           "-nostdlib", "-static", "--build-id=none", "-z", "noexecstack",
           "-o", str(KERNELELF), "--start-group"] + objects
    cmd += [str(a) for a in archives.values()]
    cmd += ["--end-group"]
    (BUILD / "kernel-link.cmd").write_text(shlex.join(cmd) + "\n")
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    (BUILD / "kernel-link.log").write_text(p.stdout + p.stderr)
    lines = ["Plan2001-O N9.3 GCC kernel link report", "=" * 40,
             f"cc: {cc}", f"ld: {cmd[0]}", f"objects: {len(objects)}",
             f"libraries: {', '.join(sorted(archives))}",
             f"library source failures: {len(libfail)}"]
    for r in libfail:
        lines.append(f"  LIB FAIL {r.unit.relsrc}: {r.category}: {r.detail}")
    if p.returncode != 0:
        lines.append("link: FAIL")
        lines.append((p.stdout + p.stderr).strip())
        (BUILD / "kernel-report.txt").write_text("\n".join(lines) + "\n")
        with open(BUILD / "link-readiness.txt", "a") as f:
            f.write("kernel link: FAIL (see kernel-link.log)\n")
        print("link: FAIL")
        return 1
    ok, elfcheck = check_kernel_elf(KERNELELF)
    lines.append("link: PASS")
    lines.append("ELF checks: " + ("PASS" if ok else "FAIL"))
    lines.extend(elfcheck)
    if KERNELELF.is_file():
        import hashlib
        h = hashlib.sha256(KERNELELF.read_bytes()).hexdigest()
        lines.append(f"sha256: {h}")
        lines.append(f"size: {KERNELELF.stat().st_size}")
    aout = ROOT / "build/amd64/9pc64"
    if aout.is_file():
        import struct
        head = aout.read_bytes()[:32]
        amagic = struct.unpack(">I", head[0:4])[0]
        lines.append(f"a.out compatibility header: magic=0x{amagic:x} "
                     f"(S_MAGIC=0x{0x8000 | (((4*26)+0)*26+7):x}), 32-byte Exec")
    (BUILD / "kernel-report.txt").write_text("\n".join(lines) + "\n")
    with open(BUILD / "link-readiness.txt", "a") as f:
        f.write("kernel link: PASS -> build/gcc/9pc64.elf\n")
        f.write("kernel ELF checks: " + ("PASS" if ok else "FAIL") + "\n")
    print("link: PASS" if ok else "link: FAIL (ELF checks)")
    print(f"kernel: {KERNELELF.relative_to(ROOT)}")
    return 0 if ok else 1


def run_full(cc: str, jobs: int) -> int:
    FULLDIR.mkdir(parents=True, exist_ok=True)
    FULLLOG.mkdir(parents=True, exist_ok=True)
    units = full_units()
    wanted = {u.obj + ".o" for u in units}
    for stale in FULLDIR.glob("*.o"):
        if stale.name not in wanted:
            stale.unlink()
    print(f"full closure: {len(units)} C/generated/asm units")
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(lambda u: compile_unit(u, cc, FULLDIR, FULLLOG), units))
    write_full_report(cc, compiler_version(cc), results)
    passed = {r.unit.obj for r in results if r.status == "PASS"}
    cunits = [u for u in units if u.src.suffix == ".c" and u.obj in passed]
    ext = external_headers(cc, cunits, jobs)
    with open(BUILD / "full-report.txt", "a") as f:
        if ext:
            f.write("external header check: FAIL\n")
            for obj, heads in sorted(ext.items()):
                f.write(f"  {obj}: {', '.join(heads)}\n")
        else:
            f.write("external header check: PASS (no host/glibc headers in full C closure)\n")
    link_status, link_text = link_audit(results)
    abi_status, abi_text = abi_check(cc)
    reg_status, reg_text = register_proof(results)
    (BUILD / "link-readiness.txt").write_text(
        link_text + "\nregister proof: " + reg_status + " (see register-proof.txt)\n")
    (BUILD / "register-proof.txt").write_text(reg_text + "\n")
    counts: dict[str, int] = {}
    for r in results:
        counts[r.status] = counts.get(r.status, 0) + 1
    print("full closure: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    print(f"ABI check: {abi_status}")
    print(f"R14/R15 register proof: {reg_status}")
    print(f"external header check (full): {'PASS' if not ext else 'FAIL'}")
    print(f"link readiness: {link_status} (link not attempted)")
    print(f"reports: build/gcc/full-report.txt, abi-report.txt, link-readiness.txt")
    return 0 if counts.get("PASS", 0) == len(units) else 1


def run(cc: str, jobs: int, do_probe: bool, do_full: bool, do_kernel: bool) -> int:
    BUILD.mkdir(parents=True, exist_ok=True)
    GENDIR.mkdir(parents=True, exist_ok=True)
    make_generated()

    version = compiler_version(cc)
    if shutil.which(cc) is None:
        text = (
            "Plan2001-O N9.1 Linux GCC compile-only report\n"
            "NOT RUN (GCC unavailable)\n"
            f"cc: {cc}\n"
            "No objects were produced.  Install gcc and binutils, then re-run."
        )
        (BUILD / "report.txt").write_text(text + "\n")
        print(text)
        return 0

    selected = build_selected()
    write_manifest(selected)
    print(f"gcc-kernel: {version}")
    print(f"building {len(selected)} selected canonical C units with -j {jobs}")

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        selres = list(ex.map(lambda u: compile_unit(u, cc, OBJDIR, LOGDIR), selected))

    failed = [r for r in selres if r.status != "PASS"]
    for r in failed:
        print(f"FAIL {r.unit.obj}: {r.category}: {r.detail}")
    print(f"selected subset: {len(selres) - len(failed)}/{len(selres)} PASS")

    probe_results: list[Result] = []
    extra_generated: list[tuple[str, str]] = []
    if do_probe:
        units, extra_generated = probe_units(selected)
        print(f"probing {len(units)} additional canonical C units")
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            probe_results = list(
                ex.map(lambda u: compile_unit(u, cc, PROBEDIR, PROBELOG), units)
            )
    else:
        print("probe skipped (--no-probe)")

    smoke_status, smoke_detail = smoke_check(selres)
    ext_headers = external_headers(cc, selected, jobs)
    write_report(cc, version, selected, selres, probe_results,
                 extra_generated, smoke_status, smoke_detail, ext_headers)
    print(f"ELF smoke check: {smoke_status}")
    print(f"external header check: {'PASS' if not ext_headers else 'FAIL'}")
    print(f"report: {(BUILD / 'report.txt').relative_to(ROOT)}")
    print(f"objects: {OBJDIR.relative_to(ROOT)}")
    full_ok = True
    if do_full or do_kernel:
        full_ok = run_full(cc, jobs) == 0
    kernel_ok = True
    if do_kernel:
        kernel_ok = run_kernel(cc, jobs) == 0
    return 0 if (not failed and smoke_status == "PASS" and not ext_headers
                 and full_ok and kernel_ok) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-j", "--jobs", type=int,
                    default=int(os.environ.get("JOBS", "0")) or (os.cpu_count() or 1),
                    help="parallel compile jobs (also JOBS=N)")
    ap.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    ap.add_argument("--probe", dest="probe", action="store_true", default=True,
                    help="also probe the broader canonical C closure (default)")
    ap.add_argument("--no-probe", dest="probe", action="store_false",
                    help="build only the selected subset")
    ap.add_argument("--full", action="store_true",
                    help="also build the full canonical C closure, generated C and GNU .S")
    ap.add_argument("--kernel", action="store_true",
                    help="build full closure, GCC libraries and link build/gcc/9pc64.elf")
    ap.add_argument("--clean", action="store_true", help="remove build/gcc first")
    args = ap.parse_args()
    if args.jobs < 1:
        ap.error("-j/--jobs must be >= 1")
    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    return run(args.cc, args.jobs, args.probe, args.full, args.kernel)


if __name__ == "__main__":
    sys.exit(main())
