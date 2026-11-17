#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo 'usage: test_tinycc_host.sh KERNEL'; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1")
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-omit-frame-pointer
  -I"$KERNEL/runtime/abi" -I"$ROOT/ports/newlib/full" -I"$ROOT/ports/tinycc")
"${CC:-gcc}" "${flags[@]}" "$ROOT/tests/host/test_tinycc_input.c" "$ROOT/ports/tinycc/native.c" -o "$OUT/input"
"$OUT/input"
"${CC:-gcc}" "${flags[@]}" "$ROOT/tests/host/test_toolchain_install.c" "$ROOT/ports/tinycc/install.c" -o "$OUT/install"
"$OUT/install"
