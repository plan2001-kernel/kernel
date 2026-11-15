#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_main --no-build --accel kvm:tcg --obootexec 0x4f45584543000003 \
  --timeout 180 --repl-input "$ROOT/tools/host/object-framing/repl-create.txt" \
  --expect FRAMED-FUN-SAVED "$@"
qob_boot_once "$QOB_OUT/disk.img" reboot 'PLAN2001-O: REPL READY' repl \
  "$ROOT/tools/host/object-framing/repl-check.txt" FRAMED-FUN-RESTORED || \
  qob_die "framed REPL function did not survive cold boot: $QOB_BOOT_REASON"
if grep -Eq 'Traceback|commit failed' "$QOB_OUT/serial-boot.log" "$QOB_OUT/serial-reboot.log"; then
  qob_die 'REPL function test raised an exception or failed its commit'
fi
qob_say 'PASS: framed REPL function code restored across two QEMU cold boots' 
