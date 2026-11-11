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
    "$T47_ROOT/tools/host/ethernet-frames/phase6-ethernet.py" >"$logdir/mkovol.log" 2>&1
}
python3 "$T47_ROOT/tools/host/ethernet-frames/peer.py" 31401 31402 >"$T47_OUT/peer.log" 2>&1 &
T47_PEER_PID=$!
trap 'kill "$T47_PEER_PID" 2>/dev/null || true' EXIT
sleep .1
kill -0 "$T47_PEER_PID"
( qob_main --no-build --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 --timeout 600 \
  --udp6-peer 31401:31402 --obootexec 0x4f45584543000003 \
  --no-repl --output-dir "$T47_OUT" --expect '^T47-RESULT [0-9]+ PASS' "$@" )
kill "$T47_PEER_PID" 2>/dev/null || true
wait "$T47_PEER_PID" 2>/dev/null || true
python3 - "$T47_OUT" <<'PY'
import re,sys
from pathlib import Path
out=Path(sys.argv[1]); s=(out/'boot-boot.log').read_text()
checks=re.findall(r'T47-PASS (\d+) (.*)$',s,re.M)
result=re.search(r'^T47-RESULT (\d+) PASS$',s,re.M)
assert result, 'missing executed PASS result'
assert int(result[1])==len(checks), 'result count does not match executed assertions'
assert [int(n) for n,_ in checks]==list(range(1,len(checks)+1)), 'non-sequential assertions'
assert not re.search(r'T47-FAIL ',s,re.M), 'a T47 assertion failed'
assert not re.search(r'Traceback|AssertionError|init: run failed|^panic:|PLAN2001-O: BOOT FAILED',s,re.M), 'fatal failure marker in boot log'
peer=(out/'peer.log').read_text()
assert 'Traceback' not in peer and 'AssertionError' not in peer, 'external peer failed'
for et in ('0800','86dd','0806','88b5'):
    assert re.search(r'^PEER-RX 60 %s '%et,peer,re.M), 'missing peer RX record for '+et
    assert re.search(r'^PEER-TX 60 %s '%et,peer,re.M), 'missing peer TX record for '+et
assert re.search(r'^PEER-RX 1514 0800 ',peer,re.M), 'missing large peer RX record'
assert re.search(r'^PEER-TX 1514 0800 ',peer,re.M), 'missing large peer TX record'
assert '5434372d4f525048414e' not in peer, 'orphaned detached TX frame reached the peer'
print('Verified',len(checks),'executed assertions and external peer records')
PY
