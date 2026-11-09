#!/usr/bin/env bash
# Focused runtime regression for atomic process creation and Handle
# inheritance (issue #21 Part A): RFORKEX and OSPAWN.
set -euo pipefail
T21_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T21_OUT=${T21_OUT:-$T21_ROOT/build/t21/run}
mkdir -p "$T21_OUT"
T21_OUT=$(realpath "$T21_OUT")
export QEMU_O_BOOT_LIB=1
source "$T21_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T21_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T21_ROOT/tools/host/t21/process-checks.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --tcg --timeout 240 --obootexec 0x4f45584543000003 \
  --output-dir "$T21_OUT" --expect '^T21-FINISH [0-9]+ PASS' "$@"
python3 - "$T21_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T21-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T21-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
