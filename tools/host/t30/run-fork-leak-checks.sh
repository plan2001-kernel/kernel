#!/usr/bin/env bash
# Negative fixture for the PLAN2001_LIFECYCLE_DIAG New-Proc checker.
#
# A diag-only fault deliberately skips procdiscard(), leaving one reserved
# New Proc.  The guest still reports T30-FINISH 0 PASS, so lifecycle_log.parse
# alone must reject the log.  The diag kernel is expected to be already built
# by run-lifecycle-diag.sh.
set -euo pipefail
T30L_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T30L_OUT=${T30L_OUT:-$T30L_ROOT/build/t30/fork-leak-negative}
mkdir -p "$T30L_OUT"
T30L_OUT=$(realpath "$T30L_OUT")
export QEMU_O_BOOT_LIB=1
source "$T30L_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T30L_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T30L_ROOT/tools/host/t30/fork-leak-negative-checks.py" \
    >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --timeout 300 --obootexec 0x4f45584543000003 \
  --output-dir "$T30L_OUT" --expect '^T30-FINISH 0 PASS' "$@"
python3 - "$T30L_OUT/boot-boot.log" "$T30L_ROOT/tools/host/t30" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[2])
import lifecycle_log

text = Path(sys.argv[1]).read_text()
assert 'T30-LEAK-NOT-INJECTED' not in text, \
    'the New-Proc leak fault token was not consumed'
assert 'T30-LEAK-WRONG-ERROR' not in text, \
    'the New-Proc leak fault produced an unexpected error'
try:
    lifecycle_log.parse(text)
except ValueError as exc:
    message = str(exc)
    assert 'unpublished Proc' in message, message
    print('Verified negative New-Proc fixture rejected:', message)
else:
    raise SystemExit('FAIL: a deliberately leaked New Proc passed')
PY
