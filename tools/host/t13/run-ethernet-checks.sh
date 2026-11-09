#!/usr/bin/env bash
set -euo pipefail
T13_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T13_OUT=${T13_OUT:-$T13_ROOT/build/t13/run}
mkdir -p "$T13_OUT"
T13_OUT=$(realpath "$T13_OUT")
export T13_REAL_QEMU=${T13_QEMU:-$(command -v qemu-system-x86_64)}
export T13_MONFILE="$T13_OUT/monitor-path"
cat > "$T13_OUT/qemu-data" <<'WRAP'
#!/usr/bin/env bash
extra=()
args=("$@")
for ((i=0;i<${#args[@]};i++)); do
  if [ "${args[i]}" = -monitor ]; then
    mon=${args[i+1]#unix:}
    printf '%s' "${mon%%,*}" > "$T13_MONFILE"
  fi
done
if [ -n "${QEMU_DATA_DIR:-}" ]; then extra=(-L "$QEMU_DATA_DIR"); fi
exec "$T13_REAL_QEMU" "${extra[@]}" "$@"
WRAP
chmod +x "$T13_OUT/qemu-data"
export QEMU="$T13_OUT/qemu-data" QEMU_O_BOOT_LIB=1
source "$T13_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T13_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T13_ROOT/tools/host/t13/ethernet-checks.py" >"$logdir/mkovol.log" 2>&1
}
python3 "$T13_ROOT/tools/host/t13/peer.py" 31301 31302 >"$T13_OUT/peer.log" 2>&1 &
T13_PEER_PID=$!
trap 'kill "$T13_PEER_PID" 2>/dev/null || true' EXIT
sleep .1
kill -0 "$T13_PEER_PID"
( qob_main --no-build --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 --timeout 180 \
  --udp6-peer 31301:31302 --obootexec 0x4f45584543000003 \
  --output-dir "$T13_OUT" --expect '^T13-RESULT [0-9]+ PASS' "$@" )
python3 - "$T13_OUT" <<'PY'
import re,sys
from pathlib import Path
out=Path(sys.argv[1])
s=(out/'boot-boot.log').read_text()
checks=re.findall(r'T13-PASS (\d+) (.*)',s)
result=re.search(r'^T13-RESULT (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1))
assert 'T13-HANDOFF-DROP PASS ' in s, 'handoff overflow was NOT demonstrated'
peer=(out/'peer.log').read_text()
assert 'Traceback' not in peer
assert 'PEER-RX 60 88b5 ' in peer and 'PEER-TX 60 88b5 ' in peer
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'executed assertions and external peer records')
PY
