#!/usr/bin/env bash
# Focused T14 test of the real userspace service over the sanctioned O boot.
set -euo pipefail
T14_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T14_OUT=${T14_OUT:-$T14_ROOT/build/t14/run}
mkdir -p "$T14_OUT"
T14_OUT=$(realpath "$T14_OUT")
export T14_REAL_QEMU=${T14_QEMU:-$(command -v qemu-system-x86_64)}
cat > "$T14_OUT/qemu-data" <<'WRAP'
#!/usr/bin/env bash
extra=()
if [ -n "${QEMU_DATA_DIR:-}" ]; then extra=(-L "$QEMU_DATA_DIR"); fi
exec "$T14_REAL_QEMU" "${extra[@]}" "$@"
WRAP
chmod +x "$T14_OUT/qemu-data"
python3 - "$T14_ROOT" "$T14_OUT/init.py" <<'PY'
import sys
from pathlib import Path
root=Path(sys.argv[1])
code=(root/'user/ipv6/node.py').read_text()+'\n'+(root/'tools/host/t14/node-child.py').read_text()
fixture=(root/'tools/host/t14/boot-checks.py').read_text()
Path(sys.argv[2]).write_text(fixture+'\nNODE_SOURCE = '+repr(code)+'\n')
PY
export QEMU="$T14_OUT/qemu-data" QEMU_O_BOOT_LIB=1
source "$T14_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T14_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T14_OUT/init.py" >"$logdir/mkovol.log" 2>&1
}
python3 "$T14_ROOT/tools/host/t14/peer.py" 31401 31402 "$T14_OUT/peer.json" "$T14_OUT/serial-boot.log" >"$T14_OUT/peer.log" 2>&1 &
T14_PEER_PID=$!
trap 'kill "$T14_PEER_PID" 2>/dev/null || true' EXIT
sleep .1
kill -0 "$T14_PEER_PID"
( qob_main --no-build --tcg --cpus 2 --ncpu 2 --obootcpu 0 --timeout 180 \
  --udp6-peer 31401:31402 --obootexec 0x4f45584543000003 \
  --output-dir "$T14_OUT" --expect '^T14-RESULT [0-9]+ PASS' "$@" )
python3 - "$T14_OUT" <<'PY'
import re,sys,json
from pathlib import Path
out=Path(sys.argv[1]);s=(out/'boot-boot.log').read_text()
checks=re.findall(r'T14-PASS (\d+) (.*)',s)
result=re.search(r'^T14-RESULT (\d+) PASS$',s,re.M)
assert result and [int(n) for n,_ in checks]==list(range(1,int(result[1])+1))
assert not re.search(r'Traceback|AssertionError|panic:',s)
peer=json.loads((out/'peer.json').read_text())
assert peer['result']=='PASS' and 'Traceback' not in (out/'peer.log').read_text()
assert peer['unknown_probes']==3 and peer['post_close_silence']
numbers=re.findall(r'^PEER-PASS (\d+) ',(out/'peer.log').read_text(),re.M)
assert [int(n) for n in numbers]==list(range(1,peer['final_checks']+1))
print('Verified',len(checks),'guest assertions and',peer['final_checks'],'external peer assertions')
PY
