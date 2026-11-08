#!/usr/bin/env bash
set -euo pipefail
T16_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T16_OUT=${T16_OUT:-$T16_ROOT/build/t16/run}
mkdir -p "$T16_OUT"
T16_OUT=$(realpath "$T16_OUT")
export QEMU_O_BOOT_LIB=1
source "$T16_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T16_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T16_ROOT/tools/host/t16/memory-checks.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --tcg --timeout 180 --obootexec 0x4f45584543000003 \
  --output-dir "$T16_OUT" --expect '^T16-PASS retired O Space, binding and namespace calls denied' "$@"
# Reboot the same writable GPT disk to verify committed GE objects and graph.
if ! qob_boot_once "$T16_OUT/disk.img" second 'PLAN2001-O: REPL READY' repl ''   '^T16-PASS retired O Space, binding and namespace calls denied'; then
  qob_show_tail "$T16_OUT/boot-second.log"
  qob_die "second boot FAIL: $QOB_BOOT_REASON"
fi
qob_has_fixed 'space: O: restored init' "$T16_OUT/boot-second.log" ||
  qob_die 'second boot did not restore persistent init graph'
qob_has_fixed 'T16-PASS GE restart reopen' "$T16_OUT/boot-second.log" ||
  qob_die 'second boot did not reopen previously committed GE object'
qob_has_fixed 'PLAN2001-O: GE/O COMMIT SUCCESS' "$T16_OUT/boot-second.log" ||
  qob_die 'second boot did not commit'
qob_say 'PASS: same disk restored and committed on second boot'
