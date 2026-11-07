#!/bin/sh
# Linux-hosted GCC 14 build of the canonical amd64 kernel.
# Compiles the explicit manifest, builds the GCC libraries and links
# build/gcc/9pc64.elf.  It does not boot and does not use 9front tools.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$root/tools/host/build-kernel.py" "$@"
