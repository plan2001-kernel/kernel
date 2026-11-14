#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
"${CC:-gcc}" -std=gnu11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=undefined -fno-omit-frame-pointer -DPLAN2001_UXPIPE_NEWLIB \
  "$ROOT/tests/host/test_newlib_fd.c" \
  "$ROOT/ports/newlib/full/fd_table.c" "$ROOT/ports/unix/uxpipe.c" \
  -o "$OUT/test-newlib-fd"
"$OUT/test-newlib-fd"
echo 'PASS: descriptor aliases, redirection, UXPIPE EOF, exhaustion and rollback (host)'
