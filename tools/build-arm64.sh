#!/bin/sh
# Linux-hosted Clang/LLD build of the experimental Plan2001-O ARM64 kernel.
# Compiles the explicit AArch64 manifest and links build/arm64/9karm64.elf.
# It does not boot and does not use 9front tools.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$root/tools/host/build-arm64.py" "$@"
