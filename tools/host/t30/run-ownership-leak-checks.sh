#!/usr/bin/env bash
# Negative fixture for the PLAN2001_LIFECYCLE_DIAG ownership checker.
#
# A diag-only fault deliberately leaks a delegated Ohand/Ospace/origin.  The
# guest still reports T30-FINISH, so lifecycle_log.parse alone must reject the
# log.  This proves the checker fails on a real leak instead of trusting
# VobjInfo.holds.  The diag kernel is expected to be already built by
# run-lifecycle-diag.sh.
set -euo pipefail
T30N_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T30N_OUT=${T30N_OUT:-$T30N_ROOT/build/t30/leak-negative}
mkdir -p "$T30N_OUT"
T30N_OUT=$(realpath "$T30N_OUT")
export QEMU_O_BOOT_LIB=1
source "$T30N_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T30N_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T30N_ROOT/tools/host/t30/ownership-leak-checks.py" \
    >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --timeout 300 --obootexec 0x4f45584543000003 \
  --output-dir "$T30N_OUT" --expect '^T30-FINISH 0 PASS' "$@"
python3 - "$T30N_OUT/boot-boot.log" "$T30N_ROOT/tools/host/t30" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[2])
import lifecycle_log

text = Path(sys.argv[1]).read_text()
assert 'T30-LEAK-NOT-INJECTED' not in text, \
    'the leak fault token was not consumed by the fault point'
try:
    lifecycle_log.parse(text)
except ValueError as exc:
    message = str(exc)
    assert 'delegated capability resource' in message, message
    print('Verified negative ownership fixture rejected:', message)
else:
    raise SystemExit('FAIL: deliberately leaked delegated resources passed')
PY
