#!/usr/bin/env bash
# AMD64 full-system acceptance test for Phase 5 (issue #44): independent
# process capabilities, multi-observer completion, delegated control,
# launcher-first exit, fault cleanup and lifecycle reclamation.  Three vCPUs
# are configured; roles are pinned to distinct advertised CPUs where useful.
set -euo pipefail
T41_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T41_OUT=${T41_OUT:-$T41_ROOT/build/t41/run}
mkdir -p "$T41_OUT"
T41_OUT=$(realpath "$T41_OUT")
export QEMU_O_BOOT_LIB=1
source "$T41_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T41_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T41_ROOT/tools/host/process-caps/phase5-process-caps.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --cpus 3 --ncpu 3 --obootcpu 0 \
  --mib 1024 --timeout 1200 --obootexec 0x4f45584543000003 --output-dir "$T41_OUT" \
  --expect '^T41-FINISH [0-9]+ PASS' "$@"
python3 - "$T41_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T41-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T41-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
