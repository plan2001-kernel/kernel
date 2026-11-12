#!/usr/bin/env bash
# Cross-owner capability acceptance on disposable GE/O QEMU image.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${CAPAUTH_OUT:-$ROOT/build/capauth/run}
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$ROOT/tools/host/capability/owner-authority.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 \
  --mib 256 --timeout 900 --obootexec 0x4f45584543000003 \
  --output-dir "$OUT" --expect '^CAPAUTH-FINISH [0-9]+ PASS' "$@"
python3 - "$OUT/boot-boot.log" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text(errors="replace")
checks=re.findall(r"^CAPAUTH-PASS (\d+) (.+)$",s,re.M)
finish=re.search(r"^CAPAUTH-FINISH (\d+) PASS$",s,re.M)
assert finish, "missing owner-capability completion marker"
assert [int(n) for n,_ in checks]==list(range(1,int(finish[1])+1)), "skipped capability assertions"
assert not re.search(r"^Traceback|^AssertionError|^panic:|^ERR:",s,re.M)
print(f"PASS: {len(checks)} cross-OwnerID capability assertions executed")
PY
