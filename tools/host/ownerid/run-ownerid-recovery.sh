#!/usr/bin/env bash
# Deterministic hard-kill/reboot recovery and owner-metadata corruption
# tests for the native GE/O owner engine.  Every case uses a disposable O
# image; the crash boots stop the guest at a printed checkpoint and the
# host SIGKILLs QEMU without guest shutdown or error unwinding.  See the
# adjacent README for what each checkpoint does and does not prove.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${T49_REC_OUT:-$ROOT/build/t49-recovery/run}
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"

QOB_OUT=$OUT
QOB_MIB=${T49_REC_MIB:-129}
QOB_TIMEOUT=${T49_REC_TIMEOUT:-1200}
QOB_CONTROLLER=virtio
QOB_ACCEL=${QOB_ACCEL:-kvm:tcg}
QOB_CPUS=1
QOB_INI_NCPU=1
QOB_INI_BOOTEXEC=0x4f45584543000003

while [ $# -gt 0 ]; do
	case "$1" in
	--no-build) QOB_BUILD=never ;;
	--rebuild) QOB_BUILD=always ;;
	--tcg) QOB_ACCEL=tcg ;;
	--output-dir) [ $# -ge 2 ] || qob_die "--output-dir needs a value"; OUT=$(realpath -m "$2"); QOB_OUT=$OUT; shift ;;
	--timeout) [ $# -ge 2 ] || qob_die "--timeout needs a value"; QOB_TIMEOUT=$2; shift ;;
	*) qob_die "usage: $0 [--no-build|--rebuild] [--tcg] [--output-dir DIR] [--timeout SEC]" ;;
	esac
	shift
done

trap qob_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
qob_prepare_outdir
qob_preflight
qob_build_artifacts

TEMPLATE=$ROOT/tools/host/ownerid/recovery-src.py

make_module() {
	local mode=$1 out=$2
	python3 - "$TEMPLATE" "$mode" "$out" <<'PY'
import sys
src, mode, out = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src).read()
if '__MODE__' not in text:
    raise SystemExit('recovery template has no __MODE__')
open(out, 'w').write(text.replace('__MODE__', mode))
PY
}

make_o_image() {
	local src=$1 raw=$2 img=$3 seed log
	seed=$(mktemp -d "$OUT/seed.XXXXXX")
	log=$seed.log
	python3 "$ROOT/tools/host/oseed.py" --source "$src" --seed "$seed" \
		--mp-exec "$QOB_MPY" --mpy-cross "$QOB_MPYCROSS" >"$log" 2>&1 || {
		cat "$log" >&2
		qob_die "seed compilation failed for $src"
	}
	[ -z "$raw" ] || cp "$raw" "$seed/raw"
	"$QOB_FORMAT" --seed "$seed" --size "$QOB_MIB" "$img" >"$log.fmt" 2>&1 || {
		cat "$log.fmt" >&2
		qob_die "O image ream failed for $src"
	}
	rm -rf "$seed" "$log" "$log.fmt"
}

make_case() {
	local mode=$1 raw=$2 tag=$3
	local dir=$OUT/$tag
	local src=$dir/recovery.py puuid
	mkdir -p "$dir"
	make_module "$mode" "$src"
	[ -z "$raw" ] || rm -rf "$dir/o.img"
	make_o_image "$src" "$raw" "$dir/o.img"
	puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
	printf '%s\n' "$puuid" >"$dir/disk.partuuid"
	QOB_OUT=$dir
	qob_write_ini "$dir/plan9.ini" "$puuid" /9pc64.elf 1
	qob_make_esp "$dir/plan9.ini" "$dir/esp.img"
	qob_make_disk "$dir/o.img" "$dir/disk.img" "$dir/esp.img" \
		"$puuid" "$dir/disk.layout" >"$dir/make-disk.log" 2>&1
	QOB_OUT=$OUT
	CASE_DIR=$dir
}

run_crash() {
	local dir=$1 tag=$2
	QOB_OUT=$dir qob_boot_once "$dir/disk.img" "$tag" \
		'OWNERID-CRASH' crash "" ""
}

run_verify() {
	local dir=$1 tag=$2 expect=$3
	QOB_OUT=$dir qob_boot_once "$dir/disk.img" "$tag" \
		'PLAN2001-O: REPL READY' repl "" "$expect"
}

assert_migrated_image() {
	local dir=$1
	qob_extract_o "$dir/disk.img" "$dir/o-after.img" "$dir/disk.layout"
	rm -rf "$dir/extract"
	python3 "$ROOT/tools/host/oinspect.py" "$dir/o-after.img" \
		--extract "$dir/extract" >"$dir/o-after.json"
	python3 "$ROOT/tools/host/oinspect.py" "$dir/o-after.img" \
		  >"$dir/o-after-3.json"
	python3 - "$dir" <<'PY'
import json
import pathlib
import sys
d = pathlib.Path(sys.argv[1])
doc = json.load(open(d / 'o-after.json'))
assert doc['owner_mode'] is True, 'legacy Space migration did not publish owner mode'
N = doc['volume']['N']
owners = doc['owners']
base, count = 0x10000000000, 120
live_owners = set()
for i in range(count):
    oid = base + i
    recs = [r for r in owners.get('%016x' % oid, [])
            if r['version'] <= N]
    if i % 10 == 9:
        assert not recs, ('tombstone gained owner metadata', i, recs)
        continue
    assert len(recs) == 1, ('legacy live object owner record', i, recs)
    assert recs[0]['bytes'] == 16 and int(recs[0]['owner'], 16) != 0, recs
    live_owners.add(recs[0]['owner'])
assert len(live_owners) == 1, ('live objects have different owners', live_owners)
empty = json.load(open(d / 'o-after-3.json'))
assert empty['owner_mode'] is True, 'empty legacy Space was not upgraded'
print('RECOVERY-IMAGE legacy live=%d tombstones=%d owner=%s PASS' %
      (count - 12, 12, live_owners.pop()))
PY
}

legacy_raw=$OUT/legacy.raw
corrupt_raw=$OUT/corrupt.raw
python3 "$ROOT/tools/host/ownerid/seedraw.py" legacy "$legacy_raw"
python3 "$ROOT/tools/host/ownerid/seedraw.py" corrupt "$corrupt_raw"

for case in 101 102 103; do
	make_case "create-$case" "" "create-$case"
	dir=$CASE_DIR
	if ! run_crash "$dir" crash1; then
		qob_show_tail "$dir/boot-crash1.log"
		qob_die "create-$case crash boot FAIL: $QOB_BOOT_REASON"
	fi
	if ! run_verify "$dir" verify1 \
		'^RECOVERY-VERIFY-FINISH case='"$case"' PASS'; then
		qob_show_tail "$dir/boot-verify1.log"
		qob_die "create-$case verify boot FAIL: $QOB_BOOT_REASON"
	fi
	qob_extract_o "$dir/disk.img" "$dir/o-after.img" "$dir/disk.layout"
	rm -rf "$dir/extract"
	python3 "$ROOT/tools/host/oinspect.py" "$dir/o-after.img" \
		--extract "$dir/extract" >"$dir/o-after.json"
	python3 - "$dir" "$case" <<'PY'
import json
import pathlib
import sys
d = pathlib.Path(sys.argv[1])
case = int(sys.argv[2])
st = (d / 'extract' / '4f45584543000100.bin').read_bytes().split()
assert st[0] == b'RECOVERY1' and st[1] == b'CRASH', st
assert int(st[2]) == case, st
target = int(st[3])
doc = json.load(open(d / 'o-after.json'))
assert doc['owner_mode'] is True, 'owner mode was not published'
recs = [r for r in doc['owners'].get('%016x' % target, [])
        if r['version'] <= doc['volume']['N']]
assert len(recs) == 1, ('target owner metadata is not write-once', target, recs)
assert recs[0]['bytes'] == 16 and int(recs[0]['owner'], 16) != 0, recs
print('RECOVERY-IMAGE case=%d target=%d owner-record=PASS' % (case, target))
PY
done

make_case migrate-104 "$legacy_raw" migrate-104
dir=$CASE_DIR
if ! run_crash "$dir" crash1; then
	qob_show_tail "$dir/boot-crash1.log"
	qob_die "migration crash boot FAIL: $QOB_BOOT_REASON"
fi
if ! run_verify "$dir" verify1 '^RECOVERY-VERIFY-FINISH case=104 PASS'; then
	qob_show_tail "$dir/boot-verify1.log"
	qob_die "migration verify boot FAIL: $QOB_BOOT_REASON"
fi
assert_migrated_image "$dir"

make_case abort-migrate "$legacy_raw" abort-migrate
dir=$CASE_DIR
if ! run_verify "$dir" verify1 '^RECOVERY-ABORT-FINISH PASS'; then
	qob_show_tail "$dir/boot-verify1.log"
	qob_die "migration abort boot FAIL: $QOB_BOOT_REASON"
fi
assert_migrated_image "$dir"

make_case corrupt "$corrupt_raw" corrupt
dir=$CASE_DIR
if ! run_verify "$dir" verify1 '^RECOVERY-CORRUPT-FINISH PASS'; then
	qob_show_tail "$dir/boot-verify1.log"
	qob_die "corruption boot FAIL: $QOB_BOOT_REASON"
fi

printf 'ownerid recovery/corruption: PASS (logs in %s)\n' "$OUT"
