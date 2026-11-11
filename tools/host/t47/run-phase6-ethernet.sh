#!/usr/bin/env bash
# Phase 6 raw Ethernet L2 acceptance: complete frames arrive through a
# read-only capability-region ring; no ObjectFIFO, Pipe or kernel L3 stack
# participates.  An external UDP-encapsulated peer supplies and checks frames.
set -euo pipefail
T47_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T47_OUT=${T47_OUT:-$T47_ROOT/build/t47/run}
mkdir -p "$T47_OUT"
T47_OUT=$(realpath "$T47_OUT")
export QEMU_O_BOOT_LIB=1
source "$T47_ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
  local img=$1 logdir=$2
  mkdir -p "$logdir"
  qob_safe_rm "$img"
  MPEXEC="$QOB_MPY" "$T47_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
    "$T47_ROOT/tools/host/t47/phase6-ethernet.py" >"$logdir/mkovol.log" 2>&1
}
python3 "$T47_ROOT/tools/host/t47/peer.py" 31401 31402 >"$T47_OUT/peer.log" 2>&1 &
T47_PEER_PID=$!
trap 'kill "$T47_PEER_PID" 2>/dev/null || true' EXIT
sleep .1
kill -0 "$T47_PEER_PID"
( qob_main --no-build --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 --timeout 600 \
  --udp6-peer 31401:31402 --obootexec 0x4f45584543000003 \
  --no-repl --output-dir "$T47_OUT" --expect '^T47-RESULT [0-9]+ PASS' "$@" )
python3 - "$T47_OUT" <<'PY'
import re,sys
from pathlib import Path
out=Path(sys.argv[1]); s=(out/'boot-boot.log').read_text()
checks=re.findall(r'T47-PASS (\d+) (.*)',s)
result=re.search(r'^T47-RESULT (\d+) PASS$',s,re.M)
assert result, 'missing executed result'
assert [int(n) for n,_ in checks]==list(range(1,int(result[1])+1))
peer=(out/'peer.log').read_text()
assert 'Traceback' not in peer
assert 'PEER-RX 60 88b5 ' in peer and 'PEER-TX 60 88b5 ' in peer
assert not re.search(r'^Traceback|^AssertionError|^panic:',s,re.M)
print('Verified',len(checks),'executed assertions and external peer records')
PY
