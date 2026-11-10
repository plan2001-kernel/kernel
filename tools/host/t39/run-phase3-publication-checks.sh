#!/usr/bin/env bash
# AMD64 full-system acceptance test for Phase 3 (issue #40): atomic
# publication, region-scoped RO WAIT/WAKE, lost-wakeup stress, key isolation,
# timeout/interruption/process-exit/writer-death cleanup and the retired
# public rendezvous/semaphore ABI.  Two vCPUs exercise the SMP ordering.
set -euo pipefail
T39_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T39_OUT=${T39_OUT:-$T39_ROOT/build/t39/run}
mkdir -p "$T39_OUT"
T39_OUT=$(realpath "$T39_OUT")
export QEMU_O_BOOT_LIB=1
source "$T39_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T39_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T39_ROOT/tools/host/t39/phase3-publication-checks.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --cpus 2 --timeout 420 \
  --obootexec 0x4f45584543000003 --output-dir "$T39_OUT" \
  --expect '^T39-FINISH [0-9]+ PASS' "$@"
python3 - "$T39_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T39-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T39-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
