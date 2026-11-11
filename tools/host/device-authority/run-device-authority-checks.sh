#!/usr/bin/env bash
# Issue #23 Part B: catalog authority, option validation and delegation for
# UART, PS/2 keyboard and USB.  Disposable QEMU xHCI + mouse and a COM2
# null backend; no physical device access.
set -euo pipefail
T23_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T23_OUT=${T23_OUT:-$T23_ROOT/build/t23/run}
mkdir -p "$T23_OUT"
T23_OUT=$(realpath "$T23_OUT")
export T23_REAL_QEMU=${T23_QEMU:-$(command -v qemu-system-x86_64)}
cat > "$T23_OUT/qemu-device" <<'WRAP'
#!/usr/bin/env bash
set -e
extra=()
if [ -n "${QEMU_DATA_DIR:-}" ]; then extra=(-L "$QEMU_DATA_DIR"); fi
exec "$T23_REAL_QEMU" "${extra[@]}" -device qemu-xhci,id=xhci \
  -device usb-mouse,bus=xhci.0 "$@" -serial null
WRAP
chmod +x "$T23_OUT/qemu-device"
export QEMU="$T23_OUT/qemu-device"
export QEMU_O_BOOT_LIB=1
source "$T23_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T23_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T23_ROOT/tools/host/device-authority/device-authority-checks.py" >"$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --timeout 240 --obootexec 0x4f45584543000003 \
  --output-dir "$T23_OUT" --expect '^T23-FINISH [0-9]+ PASS' "$@"
python3 - "$T23_OUT/boot-boot.log" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
checks=re.findall(r'^T23-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T23-FINISH (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1))
assert not re.search(r'^T23-FAIL|^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'sequential executed assertions')
PY
