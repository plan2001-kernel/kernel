#!/usr/bin/env bash
# AMD64 full-system acceptance test for Phase 2 capability-mapped regions
# (issue #38): one writable mapping, independent read-only sibling mappings,
# explicit capability delivery, exit order and MMU write rejection.
set -euo pipefail
T38_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T38_OUT=${T38_OUT:-$T38_ROOT/build/t38/run}
mkdir -p "$T38_OUT"
T38_OUT=$(realpath "$T38_OUT")
export QEMU_O_BOOT_LIB=1
source "$T38_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T38_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T38_ROOT/tools/host/t38/phase2-region-checks.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --timeout 300 --obootexec 0x4f45584543000003 \
  --output-dir "$T38_OUT" --expect '^T38-FINISH [0-9]+ PASS' "$@"
python3 - "$T38_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T38-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T38-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
