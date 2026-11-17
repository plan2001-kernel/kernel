#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=${1:-"$ROOT/../kernel"}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
"${CC:-gcc}" -std=gnu11 -O1 -g -Wall -Wextra -Werror \
  -I"$KERNEL/runtime/abi" -fsanitize=undefined -fno-omit-frame-pointer -DPLAN2001_UXPIPE_NEWLIB \
  "$ROOT/tests/host/test_newlib_fd.c" \
  "$ROOT/ports/newlib/full/fd_table.c" "$ROOT/ports/newlib/full/inherit.c" "$ROOT/ports/unix/uxpipe.c" \
  -o "$OUT/test-newlib-fd"
"$OUT/test-newlib-fd"
echo 'PASS: descriptor aliases, redirection, UXPIPE EOF, exhaustion and rollback (host)'
