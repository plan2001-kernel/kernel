#!/usr/bin/env bash
# Host tests for the same freestanding PVD and Unix init sources as the ELF.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CC=${CC:-gcc-14}
BASE=(-std=c11 -Wall -Wextra -Werror -pedantic
      -fsanitize=address,undefined -fno-omit-frame-pointer
      -I"$ROOT/ports/unix" -I"$ROOT/ports/busybox")
"$CC" "${BASE[@]}" "$ROOT/tests/host/test_pvd.c" \
    "$ROOT/ports/unix/pvd.c" -o "$OUT/test-pvd"
"$CC" "${BASE[@]}" "$ROOT/tests/host/test_pvd_cli.c" \
    "$ROOT/ports/unix/pvd.c" "$ROOT/ports/busybox/pvd_cli.c" \
    -o "$OUT/test-pvd-cli"
"$CC" "${BASE[@]}" "$ROOT/tests/host/test_unix_init.c" \
    "$ROOT/ports/unix/init.c" "$ROOT/ports/unix/pvd.c" \
    "$ROOT/ports/busybox/pvd_cli.c" -o "$OUT/test-unix-init"
"$OUT/test-pvd"
"$OUT/test-pvd-cli"
"$OUT/test-unix-init"
