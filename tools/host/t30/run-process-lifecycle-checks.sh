#!/usr/bin/env bash
# Executable Pgrp/OContext lifecycle and rollback regression (issue #30).
set -euo pipefail
T30_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T30_OUT=${T30_OUT:-$T30_ROOT/build/t30/run}
mkdir -p "$T30_OUT"
T30_OUT=$(realpath "$T30_OUT")
export QEMU_O_BOOT_LIB=1
source "$T30_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T30_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T30_ROOT/tools/host/t30/process-lifecycle-checks.py" \
    >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --timeout 300 --obootexec 0x4f45584543000003 \
  --output-dir "$T30_OUT" --expect '^T30-FINISH [0-9]+ PASS' "$@"
python3 - "$T30_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
# The opt-in lifecycle diag build emits complete kernel lines that can
# interleave with a console write.  Remove them before the strict marker
# checks; the diag runner consumes them separately.
s=re.sub(r'T30LIFE [goc][+-] seq=\d+ id=\d+ pgrp=-?\d+ octx=-?\d+\n','',s)
checks=re.findall(r'^T30-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T30-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
