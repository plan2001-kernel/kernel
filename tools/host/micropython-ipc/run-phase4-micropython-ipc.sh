#!/usr/bin/env bash
# AMD64 full-system acceptance test for Phase 4 (issue #42): three independent
# MicroPython processes exchange scalar, delta and immutable 8 KiB records
# through capability regions and REGIONWAIT/REGIONWAKE only.  Region Handles
# are granted with explicit SPAWN vhandles; no ObjectFIFO/Pipe data path is
# present.  Three vCPUs are configured and the three roles are pinned to
# distinct advertised CPUs.
set -euo pipefail
T40_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T40_OUT=${T40_OUT:-$T40_ROOT/build/t40/run}
mkdir -p "$T40_OUT"
T40_OUT=$(realpath "$T40_OUT")
export QEMU_O_BOOT_LIB=1
source "$T40_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T40_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T40_ROOT/tools/host/micropython-ipc/phase4-micropython-ipc.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --cpus 3 --ncpu 3 --obootcpu 0 \
  --mib 1024 --timeout 1200 --obootexec 0x4f45584543000003 --output-dir "$T40_OUT" \
  --expect '^T40-FINISH [0-9]+ PASS' "$@"
python3 - "$T40_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T40-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T40-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1)), 'non-sequential assertions'
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
