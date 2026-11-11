#!/usr/bin/env bash
# Two QEMU/OVMF boots of the same disposable native GE/O disk.  Boot one
# runs the full owner acceptance and persists A/B objects plus the resolved
# OwnerIDs.  Boot two restores the same O volume, re-registers the trusted
# names, and proves the stored owners and cross-owner denials survived.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${T49_RB_OUT:-$ROOT/build/t49-reboot/run}
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"

QOB_OUT=$OUT
QOB_MIB=${T49_RB_MIB:-129}
QOB_TIMEOUT=${T49_RB_TIMEOUT:-1200}
QOB_CONTROLLER=virtio
QOB_ACCEL=${QOB_ACCEL:-kvm:tcg}
QOB_CPUS=2
QOB_INI_NCPU=2
QOB_INI_BOOTCPU=0
QOB_INI_BOOTEXEC=0x4f45584543000003

while [ $# -gt 0 ]; do
	case "$1" in
	--no-build) QOB_BUILD=never ;;
	--rebuild) QOB_BUILD=always ;;
	--tcg) QOB_ACCEL=tcg ;;
	--output-dir) [ $# -ge 2 ] || qob_die "--output-dir needs a value"; QOB_OUT=$(realpath -m "$2"); shift ;;
	--timeout) [ $# -ge 2 ] || qob_die "--timeout needs a value"; QOB_TIMEOUT=$2; shift ;;
	*) qob_die "usage: $0 [--no-build|--rebuild] [--tcg] [--output-dir DIR] [--timeout SEC]" ;;
	esac
	shift
done

qob_new_o_image() {
	local img=$1 logdir=$2
	mkdir -p "$logdir"
	qob_safe_rm "$img"
	MPEXEC="$QOB_MPY" "$ROOT/tools/mkovol" "$img" "$QOB_MIB" \
		"$ROOT/tools/host/ownerid/ownerid.py" >"$logdir/mkovol.log" 2>&1
}

trap qob_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
qob_prepare_outdir
qob_preflight
qob_build_artifacts

puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
printf '%s\n' "$puuid" >"$QOB_OUT/disk.partuuid"
qob_new_o_image "$QOB_OUT/o.img" "$QOB_OUT/ream"
qob_write_ini "$QOB_OUT/plan9.ini" "$puuid" /9pc64.elf 1
qob_make_esp "$QOB_OUT/plan9.ini" "$QOB_OUT/esp.img"
qob_make_disk "$QOB_OUT/o.img" "$QOB_OUT/disk.img" \
	"$QOB_OUT/esp.img" "$puuid" "$QOB_OUT/disk.layout"

success='PLAN2001-O: REPL READY'
if ! qob_boot_once "$QOB_OUT/disk.img" boot1 "$success" repl "" \
	'^T49-FINISH [0-9]+ PASS'; then
	qob_show_tail "$QOB_OUT/boot-boot1.log"
	qob_die "boot one FAIL: $QOB_BOOT_REASON"
fi
if ! qob_boot_once "$QOB_OUT/disk.img" boot2 "$success" repl "" \
	'^T49-REBOOT-FINISH [0-9]+ PASS'; then
	qob_show_tail "$QOB_OUT/boot-boot2.log"
	qob_die "boot two FAIL: $QOB_BOOT_REASON"
fi

python3 - "$QOB_OUT/boot-boot1.log" "$QOB_OUT/boot-boot2.log" <<'PY'
import re
import sys
from pathlib import Path

first = Path(sys.argv[1]).read_text()
second = Path(sys.argv[2]).read_text()
m = re.search(r'^T49-FINISH (\d+) PASS$', first, re.M)
r = re.search(r'^T49-REBOOT-FINISH (\d+) PASS$', second, re.M)
assert m, 'boot one missing T49-FINISH'
assert r, 'boot two missing T49-REBOOT-FINISH'
assert 'space: O: init restored' in second, 'boot two did not restore the O Space'
assert 'init has no graph' not in second, 'boot two rebuilt a fresh image'
assert not re.search(r'^Traceback|^AssertionError|^panic:', first + second, re.M)
print('Verified reboot: boot1', m.group(1), 'checks; boot2', r.group(1), 'checks')
PY
