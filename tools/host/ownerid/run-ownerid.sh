#!/usr/bin/env bash
# AMD64 full-system acceptance for issue #49: immutable OwnerID,
# trusted registered-program launch and owner-offered region publication.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${T49_OUT:-$ROOT/build/t49/run}
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$ROOT/tools/host/ownerid/ownerid.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 \
  --mib 129 --timeout 1200 --obootexec 0x4f45584543000003 \
  --output-dir "$OUT" --expect '^T49-FINISH [0-9]+ PASS' "$@"
python3 - "$OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T49-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T49-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
