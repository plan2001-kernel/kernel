#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  MPEXEC="$QOB_MPY" MPYCROSS="$QOB_MPYCROSS" FORMATO="$QOB_FORMAT" \
    "$QOB_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$ROOT/tools/host/object-framing/accept.py" > "$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --obootexec 0x4f45584543000003 \
  --timeout 180 --expect FRAMED-NATIVE-LOADER-NEGATIVE-PASS "$@"
