#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo 'usage: test_surface_host.sh KERNEL'; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1")
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
"${CC:-gcc}" -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
 -fsanitize=undefined -fno-omit-frame-pointer -I"$KERNEL/runtime/abi" -I"$ROOT/ports/newlib/full" \
 "$ROOT/tests/host/test_surface.c" "$ROOT/ports/newlib/full/surface.c" -o "$OUT/surface"
"$OUT/surface"
