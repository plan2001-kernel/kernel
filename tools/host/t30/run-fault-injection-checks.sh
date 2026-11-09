#!/usr/bin/env bash
# Deterministic post-allocation rollback run (issue #34).
#
# This runner requires the PLAN2001_LIFECYCLE_DIAG kernel already built by
# run-lifecycle-diag.sh.  The fault token is test-only and compiled out of the
# canonical kernel.
set -euo pipefail
T30F_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T30F_OUT=${T30F_OUT:-$T30F_ROOT/build/t30/fault}
mkdir -p "$T30F_OUT"
T30F_OUT=$(realpath "$T30F_OUT")
export QEMU_O_BOOT_LIB=1
source "$T30F_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T30F_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T30F_ROOT/tools/host/t30/fault-injection-checks.py" \
    >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --timeout 300 --obootexec 0x4f45584543000003 \
  --output-dir "$T30F_OUT" --expect '^T30-FINISH [0-9]+ PASS' "$@"
python3 - "$T30F_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
s=re.sub(r'T30LIFE [gocdsp][+-] seq=\d+ id=\d+ pgrp=-?\d+ octx=-?\d+\n','',s)
checks=re.findall(r'^T30-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T30-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
fpu=re.findall(r'T30FPU discard fpsave=(\d) kfpsave=(\d) live=(-?\d+)',s)
assert len(fpu)==4, 'expected 4 post-procfork FPU discards, got %r' % (fpu,)
assert all(fs=='0' and kfs=='0' for fs,kfs,_ in fpu), \
    'discarded Proc retained an FP save chain: %r' % (fpu,)
assert len({int(live) for _,_,live in fpu})==1, \
    'net FPU allocations grew across repetitions: %r' % (fpu,)
print('Verified',len(checks),'sequential assertions and',len(fpu),
      'post-procfork FPU discards at a constant live count')
PY
