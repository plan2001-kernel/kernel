#!/bin/bash
# Build the Linux host MicroPython 1.27.0 mpy-cross with GCC.
#
# It compiles the repository's own micropython/py sources (including the
# Plan2001 compiler changes) with the upstream v1.27.0 mpy-cross front
# end in micropython/mpy-cross.  The output is a normal Linux process;
# build/gcc/micropython.elf is the freestanding Plan2001 runtime and is
# never executed on the host.
#
# Output: build/mpy-cross/mpy-cross
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
make -C "$root/micropython/mpy-cross" BUILD="$root/build/mpy-cross" -j"$jobs"
out=$root/build/mpy-cross/mpy-cross
[ -x "$out" ] || { echo "build-mpy-cross: no $out" >&2; exit 1; }
size=$(stat -c %s "$out")
sha=$(sha256sum "$out" | awk '{print $1}')
echo "mpy-cross: $out ($size bytes, sha256 $sha)"
