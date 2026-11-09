#!/usr/bin/env bash
set -euo pipefail
T15_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T15_OUT=${T15_OUT:-$T15_ROOT/build/t15/run}
mkdir -p "$T15_OUT"
T15_OUT=$(realpath "$T15_OUT")
export T15_REAL_QEMU=${T15_QEMU:-$(command -v qemu-system-x86_64)}
cat > "$T15_OUT/qemu-data" <<'WRAP'
#!/usr/bin/env bash
extra=()
if [ -n "${QEMU_DATA_DIR:-}" ]; then extra=(-L "$QEMU_DATA_DIR"); fi
exec "$T15_REAL_QEMU" "${extra[@]}" "$@"
WRAP
chmod +x "$T15_OUT/qemu-data"
python3 - "$T15_ROOT" "$T15_OUT/init.py" <<'PY'
from pathlib import Path
import sys
r=Path(sys.argv[1]);files=['user/ipv6/node.py','user/ipv6/esp_demo.py','tools/host/t15/demo-config.py','tools/host/t15/node-child.py']
code='\n'.join((r/f).read_text() for f in files)
Path(sys.argv[2]).write_text((r/'tools/host/t15/boot-checks.py').read_text()+'\nNODE_SOURCE = '+repr(code)+'\n')
PY
export QEMU="$T15_OUT/qemu-data" QEMU_O_BOOT_LIB=1
source "$T15_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T15_ROOT/tools/mkovol" "$img" "$QOB_MIB" "$T15_OUT/init.py" >"$logdir/mkovol.log" 2>&1
}
python3 "$T15_ROOT/tools/host/t15/peer.py" 31501 31502 "$T15_OUT/peer.json" >"$T15_OUT/peer.log" 2>&1 &
T15_PEER_PID=$!
trap 'kill "$T15_PEER_PID" 2>/dev/null || true' EXIT
sleep .1
kill -0 "$T15_PEER_PID"
( qob_main --no-build --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 --timeout 180 \
  --udp6-peer 31501:31502 --obootexec 0x4f45584543000003 --output-dir "$T15_OUT" --expect '^T15-RESULT [0-9]+ PASS' "$@" )
python3 - "$T15_OUT" <<'PY'
import json,re,sys
from pathlib import Path
r=Path(sys.argv[1]);s=(r/'boot-boot.log').read_text();p=json.loads((r/'peer.json').read_text())
result=re.search(r'^T15-RESULT (\d+) PASS$',s,re.M)
checks=[int(n) for n in re.findall(r'T15-PASS (\d+) ',s)]
assert result and checks==list(range(1,int(result[1])+1))
assert not re.search(r'Traceback|AssertionError|panic:',s)
peer=(r/'peer.log').read_text()
assert 'Traceback' not in peer and p['result']=='PASS'
assert [int(n) for n in re.findall(r'^PEER-PASS (\d+) ',peer,re.M)]==list(range(1,p['checks']+1))
print('Verified',len(checks),'guest assertions and',p['checks'],'external peer assertions')
PY
