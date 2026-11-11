#!/usr/bin/env bash
# AMD64 full-system acceptance test for Phase 6 (issue #46): one authorized
# console owner claims the keyboard, two independent MicroPython REPLs run
# over input/output/diagnostic capability regions, focus switches atomically,
# UTF-8 and Ctrl-C/Ctrl-D cross only the focused session, and one client exit
# leaves the survivor usable.  No ObjectFIFO or Pipe endpoint exists.
set -euo pipefail
T46_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T46_OUT=${T46_OUT:-$T46_ROOT/build/t46/run}
mkdir -p "$T46_OUT"
T46_OUT=$(realpath "$T46_OUT")
export QEMU_O_BOOT_LIB=1
source "$T46_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T46_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T46_ROOT/tools/host/console-sessions/phase6-console.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --cpus 3 --ncpu 3 --obootcpu 0 \
  --mib 512 --timeout 900 --obootexec 0x4f45584543000003 \
  --no-repl --output-dir "$T46_OUT" \
  --expect '^T46-FINISH [0-9]+ PASS' "$@"
python3 - "$T46_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T46-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T46-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
