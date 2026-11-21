#!/usr/bin/env python3
"""Honest cold-storage approximation: F0 partition-image vs ordinary files.

GitHub Actions runs in a VM, not a physical laptop. Source compilation is
measured separately from a deterministic, make-shaped filesystem replay.
"""
import hashlib
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
F0 = HERE / "f0bench"
BASE = Path(os.environ.get("F0_WORK", "/tmp/f0-cold-io"))
ROOT = BASE / "project"
BASE_IMG = BASE / "base.f0"
MANIFEST = BASE / "replay.txt"
IMPORT = BASE / "import.txt"


def run(args, *, cwd=None, capture=False, check=True):
    proc = subprocess.run([str(a) for a in args], cwd=cwd, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None, check=False)
    if check and proc.returncode:
        if capture:
            print(proc.stdout[-3000:], proc.stderr[-3000:], file=sys.stderr)
        raise RuntimeError(f"Command failed ({proc.returncode}): {args}")
    return proc


def build():
    run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-o", F0, HERE / "f0bench.c"])


def make_data():
    if BASE.exists():
        shutil.rmtree(BASE)
    for folder in (ROOT / "src", ROOT / "include", ROOT / "out", ROOT / "obj", ROOT / "blobs"):
        folder.mkdir(parents=True, exist_ok=True)
    for h in range(64):
        (ROOT / "include" / f"h{h:02d}.h").write_text(
            f"#ifndef F0_H{h:02d}\n#define F0_H{h:02d}\n#define V{h:02d} {h*17+13}\n#endif\n")
    manifest = []
    imports = []
    functions=[]
    for i in range(512):
        hs = [(i * 7 + 11 * j) % 64 for j in range(4)]
        src = f"src/f{i:04d}.c"
        incl = "".join(f'#include "h{h:02d}.h"\n' for h in hs)
        terms = "+".join(f"V{h:02d}" for h in hs)
        # Non-trivial but bounded C compiler work; same for all builds.
        body = f"{incl}\nunsigned int f{i:04d}(unsigned int x) {{\n"
        for j in range(40):
            body += f"  x = (x * {j*4+3}u + ({terms}) + {i+j}u) ^ (x >> 7);\n"
        body += "  return x;\n}\n"
        (ROOT / src).write_text(body)
        imports.append(f"R {src}\n")
        manifest.append(f"R {src}\n")
        for h in hs:
            manifest.append(f"R include/h{h:02d}.h\n")
        manifest.append(f"W out/f{i:04d}.o 4096\n")
        functions.append(f"unsigned int f{i:04d}(unsigned int);\n")
    main_src = "src/main.c"
    (ROOT / main_src).write_text("#include <stdio.h>\n" + "".join(functions) +
        "int main(void){unsigned int x=1;\n" +
        "".join(f"x += f{i:04d}(x);\n" for i in range(512)) +
        'printf("%u\\n",x);return 0;}\n')
    imports.append(f"R {main_src}\n")
    manifest.append(f"R {main_src}\nW out/main.o 4096\nW out/app 65536\n")
    for h in range(64):
        imports.append(f"R include/h{h:02d}.h\n")
    # Separate bulk-read phase, not part of make-shaped replay.
    for size, count in ((1024,128),(4096,128),(16384,128),(65536,64),(1048576,16)):
        for i in range(count):
            name=f"blobs/b{size}_{i:04d}.bin"
            seed=hashlib.blake2b(name.encode(),digest_size=32).digest()
            (ROOT / name).write_bytes((seed * ((size+31)//32))[:size])
            imports.append(f"R {name}\n")
    manifest.append("W out/final-archive.bin 65536\n")
    MANIFEST.write_text("".join(manifest))
    IMPORT.write_text("".join(imports))
    mk = ["CC=cc", "CFLAGS=-O2 -Iinclude", "SRC=$(wildcard src/*.c)",
          "OBJ=$(patsubst src/%.c,obj/%.o,$(SRC))", "all: app",
          "app: $(OBJ)", "\t$(CC) $(OBJ) -o app", "obj/%.o: src/%.c $(wildcard include/*.h)",
          "\t$(CC) $(CFLAGS) -c $< -o $@", ".PHONY: all clean", "clean:",
          "\trm -f obj/*.o app", ""]
    (ROOT / "Makefile").write_text("\n".join(mk))
    print(f"dataset: 513 source files, 64 headers, 464 synthetic blobs; {len(imports)} unique records", flush=True)


def drop_caches(backend=None, image=None):
    """Return GLOBAL, FILE_DATA_ONLY or UNAVAILABLE.

    FILE_DATA_ONLY is not a cold-boot approximation: it cannot evict VFS
    dentries, inodes, program binaries or shared libraries.
    """
    run(["sync"])
    cmd = ["sudo", "-n", "sh", "-c", "echo 3 > /proc/sys/vm/drop_caches"]
    proc = run(cmd, capture=True, check=False)
    if proc.returncode == 0:
        return "GLOBAL"
    if not hasattr(os, "posix_fadvise"):
        return "UNAVAILABLE"
    paths = []
    if backend == "f0" and image:
        paths = [Path(image)]
    elif backend in ("ext4", "make"):
        paths = [ROOT / line.split()[1] for line in IMPORT.read_text().splitlines()
                 if line.startswith("R ")]
    else:
        return "UNAVAILABLE"
    ok = 0
    for path in paths:
        try:
            with path.open("rb") as f:
                os.posix_fadvise(f.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
                ok += 1
        except OSError:
            return "UNAVAILABLE"
    return "FILE_DATA_ONLY" if ok else "UNAVAILABLE"


def parse(text):
    m=re.search(r"backend=(\w+) elapsed_ms=([\d.]+).*?digest=([a-f0-9]+)",text)
    if not m:
        raise RuntimeError(f"Missing replay metrics: {text}")
    return {"backend":m.group(1), "ms":float(m.group(2)), "digest":m.group(3),
            "read_bytes":int(re.search(r"read_bytes:\s+(\d+)",text).group(1)),
            "write_bytes":int(re.search(r"write_bytes:\s+(\d+)",text).group(1))}


def create_image():
    run([F0, "init", BASE_IMG])
    run([F0, "import", ROOT, BASE_IMG, IMPORT])
    run([F0, "verify", ROOT, BASE_IMG, IMPORT])
    run([F0, "info", BASE_IMG])


def verify_recovery():
    target=BASE/"recovery.f0"
    shutil.copyfile(BASE_IMG,target)
    run([F0,"damage-last-footer",target])
    out=run([F0,"info",target],capture=True).stdout
    print("recovery scan:",out.strip())
    if "invalid_tail=1" not in out:raise RuntimeError("recovery test failed")
    target.unlink()


def make_baseline(cold):
    run(["make", "clean"],cwd=ROOT,capture=True)
    if cold: cold=drop_caches("make")
    t=time.monotonic()
    proc=run(["make", "-j4"],cwd=ROOT,capture=True)
    sec=time.monotonic()-t
    out=run([ROOT/"app"],capture=True).stdout.strip()
    print(f"REAL MAKE: elapsed_ms={sec*1000:.2f} cold_eviction={cold} program_output={out}")


def measure(cold):
    rows=[]
    for round in range(3):
        for backend in (("ext4","f0") if round%2==0 else ("f0","ext4")):
            shutil.rmtree(ROOT/"out",ignore_errors=True)
            (ROOT/"out").mkdir()
            trial=BASE / "trial.f0"
            if trial.exists(): trial.unlink()
            if backend=="f0":shutil.copyfile(BASE_IMG,trial)
            evicted=drop_caches(backend, trial if backend=="f0" else None) if cold else "UNAVAILABLE"
            res=run([F0,"replay",backend,ROOT,trial,MANIFEST],capture=True)
            m=parse(res.stdout);m["round"]=round;m["cold_eviction"]=evicted
            print("REPLAY:",m,flush=True)
            rows.append(m)
            if backend=="f0":
                # Output should be physically persisted in image; check on reopen.
                info=run([F0,"info",trial],capture=True).stdout
                if "invalid_tail=" not in info:raise RuntimeError("volume reopen failed")
                trial.unlink()
    if any(row["digest"]!=rows[0]["digest"] for row in rows):
        raise RuntimeError("different data read by F0 vs ext4")
    print("SUMMARY:")
    for backend in ("ext4","f0"):
        subs=[r for r in rows if r["backend"]==backend]
        print(f"{backend} median_ms={statistics.median(r['ms'] for r in subs):.3f} "
              f"median_proc_read_bytes={statistics.median(r['read_bytes'] for r in subs):.0f} "
              f"median_proc_write_bytes={statistics.median(r['write_bytes'] for r in subs):.0f}")
    f0=statistics.median(r["ms"] for r in rows if r["backend"]=="f0")
    ext=statistics.median(r["ms"] for r in rows if r["backend"]=="ext4")
    print(f"F0 speedup ratio ext4/F0={ext/f0:.3f}x")
    has_cold=all(r["cold_eviction"]=="GLOBAL" and r["read_bytes"]>0 for r in rows)
    print("COLD_STORAGE_VERIFIED="+("YES" if has_cold else "NO"))
    print("FILE_DATA_READ_VERIFIED="+("YES" if all(r["read_bytes"]>0 for r in rows) else "NO"))
    if not has_cold: print("WARNING: Do not present the replay as verified cold physical disk I/O.")
    print("IMPORTANT: synthetic replay is NOT a real make execution on F0.")
    print("IMPORTANT: F0 uses a file-backed image on the host filesystem; not a raw partition.")


def main():
    build();make_data();create_image();verify_recovery()
    filesystem=run(["findmnt","-T",ROOT,"-n","-o","FSTYPE,SOURCE"],capture=True,check=False)
    print("BACKING FILESYSTEM:",filesystem.stdout.strip() if filesystem.returncode==0 else "unknown")
    cache_mode=drop_caches("make")
    print("CACHE_EVICTION_MODE=",cache_mode)
    make_baseline(cache_mode!="UNAVAILABLE")
    measure(cache_mode!="UNAVAILABLE")


if __name__=="__main__":main()