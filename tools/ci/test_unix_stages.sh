#!/usr/bin/env bash
# Each stage runs independently; a failure in one does not suppress another.
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: test_unix_stages.sh 4|6|7 KERNEL_PATH" >&2; exit 2; }
STAGE=$1 KERNEL=$(realpath "$2")
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CC=${CC:-gcc}
BASE=(-std=c11 -O1 -g -Wall -Wextra -Werror -pedantic
      -fsanitize=undefined -fno-omit-frame-pointer
      -I"$ROOT/ports/unix" -I"$ROOT/ports/busybox")
case "$STAGE" in
  4)
    "$CC" "${BASE[@]}" "$ROOT/tests/host/test_uxfs.c" \
      "$ROOT/ports/unix/pvd.c" "$ROOT/ports/unix/uxfs.c" -o "$OUT/uxfs"
    "$OUT/uxfs"
    ;;
  6)
    "$CC" "${BASE[@]}" -I"$KERNEL/runtime/abi" \
      "$ROOT/tests/host/test_uxspawn.c" "$ROOT/ports/unix/uxspawn.c" \
      -o "$OUT/uxspawn"
    "$OUT/uxspawn"
    "$CC" -std=gnu11 -m64 -O2 -ffreestanding -nostdinc -fno-builtin \
      -Wall -Wextra -Werror -I"$KERNEL/runtime/abi" \
      -I"$ROOT/ports/unix" -c "$ROOT/ports/unix/uxspawn.c" -o "$OUT/native-spawn.o"
    "$CC" "${BASE[@]}" -I"$KERNEL/runtime/abi" \
      "$ROOT/tests/host/test_ls_child.c" "$ROOT/ports/busybox/ls_child.c" \
      -o "$OUT/native-ls"
    "$OUT/native-ls"
    "$CC" -std=gnu11 -m64 -O2 -ffreestanding -nostdinc -fno-builtin \
      -Wall -Wextra -Werror -I"$KERNEL/runtime/abi" \
      -I"$ROOT/ports/busybox" \
      -c "$ROOT/ports/busybox/ls_child.c" -o "$OUT/native-ls.o"
    "$CC" "${BASE[@]}" -I"$KERNEL/runtime/abi" \
      "$ROOT/tests/host/test_uxexternal.c" \
      "$ROOT/ports/unix/pvd.c" "$ROOT/ports/unix/uxfs.c" \
      "$ROOT/ports/unix/uxexternal.c" "$ROOT/ports/unix/uxspawn.c" \
      "$ROOT/ports/unix/uxpipe.c" -o "$OUT/uxexternal"
    "$OUT/uxexternal"
    "$CC" -std=gnu11 -m64 -O2 -ffreestanding -nostdinc -fno-builtin \
      -Wall -Wextra -Werror -I"$KERNEL/runtime/abi" \
      -I"$ROOT/ports/unix" -c "$ROOT/ports/unix/uxexternal.c" -o "$OUT/native-external.o"
    "$CC" "${BASE[@]}" -I"$KERNEL/runtime/abi" \
      "$ROOT/tests/host/test_supervisor.c" \
      "$ROOT/ports/unix/supervisor.c" \
      "$ROOT/ports/unix/uxspawn.c" "$ROOT/ports/unix/uxpipe.c" \
      -o "$OUT/unix-supervisor"
    "$OUT/unix-supervisor"
    "$CC" -std=gnu11 -m64 -O2 -ffreestanding -nostdinc -fno-builtin \
      -Wall -Wextra -Werror -I"$KERNEL/runtime/abi" \
      -I"$ROOT/ports/unix" -c "$ROOT/ports/unix/supervisor.c" \
      -o "$OUT/native-supervisor.o"
    echo "PASS: freestanding SPAWN, external ls, F5 adapter and Unix supervisor"
    ;;
  7)
    "$CC" "${BASE[@]}" "$ROOT/tests/host/test_uxshell.c" \
      "$ROOT/ports/unix/pvd.c" "$ROOT/ports/unix/uxfs.c" \
      "$ROOT/ports/unix/uxshell.c" -o "$OUT/uxshell"
    "$OUT/uxshell"
    "$CC" "${BASE[@]}" "$ROOT/tests/host/test_uxservice.c" \
      "$ROOT/ports/unix/pvd.c" "$ROOT/ports/unix/uxfs.c" \
      "$ROOT/ports/unix/uxshell.c" "$ROOT/ports/unix/uxservice.c" \
      "$ROOT/ports/unix/uxfd.c" -o "$OUT/uxservice"
    "$OUT/uxservice"
    "$CC" "${BASE[@]}" -pthread \
      "$ROOT/tests/host/test_uxrpc.c" \
      "$ROOT/ports/unix/pvd.c" "$ROOT/ports/unix/uxfs.c" \
      "$ROOT/ports/unix/uxshell.c" "$ROOT/ports/unix/uxservice.c" \
      "$ROOT/ports/unix/uxfd.c" "$ROOT/ports/unix/uxpipe.c" \
      "$ROOT/ports/unix/uxrpc.c" -o "$OUT/uxrpc"
    timeout 30 "$OUT/uxrpc"
    "$CC" "${BASE[@]}" -pthread \
      "$ROOT/tests/host/test_uxpipe.c" "$ROOT/ports/unix/uxpipe.c" \
      -o "$OUT/uxpipe"
    timeout 20 "$OUT/uxpipe"
    "$CC" -std=gnu11 -m64 -O2 -ffreestanding -nostdinc -fno-builtin \
      -Wall -Wextra -Werror -I"$ROOT/ports/unix" \
      -c "$ROOT/ports/unix/uxpipe.c" -o "$OUT/native-pipe.o"
    echo "PASS: two-region native pipe protocol compiles for Plan2001"
    ;;
  *) echo "Unknown stage $STAGE" >&2; exit 2;;
esac
