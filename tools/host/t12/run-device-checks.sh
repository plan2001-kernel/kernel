#!/usr/bin/env bash
# T12's focused reproducible check: canonical artifacts, ordinary O boot,
# disposable QEMU xHCI + mouse.  No debug kernel or physical device access.
set -euo pipefail
T12_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T12_OUT=${T12_OUT:-$T12_ROOT/build/t12-finish/run}
mkdir -p "$T12_OUT"
T12_OUT=$(realpath "$T12_OUT")
export T12_REAL_QEMU=${T12_QEMU:-$(command -v qemu-system-x86_64)}
cat > "$T12_OUT/qemu-usb" <<'WRAP'
#!/usr/bin/env bash
set -e
extra=()
if [ -n "${QEMU_DATA_DIR:-}" ]; then extra=(-L "$QEMU_DATA_DIR"); fi
exec "$T12_REAL_QEMU" "${extra[@]}" -device qemu-xhci,id=xhci \
  -device usb-mouse,bus=xhci.0 "$@"
WRAP
chmod +x "$T12_OUT/qemu-usb"
export QEMU="$T12_OUT/qemu-usb"
export QEMU_O_BOOT_LIB=1
source "$T12_ROOT/tools/qemu-o-boot.sh"
# mkovol's existing SOURCE argument seeds this tracked check module.  The
# sanctioned QEMU runner still verifies the normal boot, commit and ELF hash.
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T12_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T12_ROOT/tools/host/t12/device-checks.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --tcg --timeout 180 --obootexec 0x4f45584543000003 \
  --output-dir "$T12_OUT" --expect '^T12-FINISH [0-9]+ PASS' "$@"
python3 - "$T12_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T12-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T12-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1))
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
