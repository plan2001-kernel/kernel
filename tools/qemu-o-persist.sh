#!/usr/bin/env bash
# Two independent QEMU/OVMF boots against the SAME disposable GPT disk and
# the SAME writable native GE/O partition.  Boot one initializes a fresh O
# image, runs init.run() and commits a real state change; boot two restores
# that committed state and commits the next version.  The O partition is
# never recreated or reseeded between the boots.
#
# Both boots use the normal canonical build artifacts and the same host
# logic as tools/qemu-o-boot.sh (sourced as a library).  The default test is
# the existing persistent odemo Counter:
#
#   boot 1: init has no graph, run() -> 1, committed init version 1
#   boot 2: restored init version 1, run() -> 2, committed init version 2
#
# --boot1-input/--boot2-input and --boot1-expect/--boot2-expect expose the
# native PS/2 REPL through the QEMU monitor.  They are intended for later
# function-persistence checks (for example after an accepted def/alias
# implementation); they do NOT claim that such function persistence works
# on current main.
#
# No physical block device is ever opened for writing.  Exit status:
# 0 PASS, 1 FAIL/blocked, 2 usage.

set -euo pipefail

QEMU_O_BOOT_LIB=1
# shellcheck source=tools/qemu-o-boot.sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/qemu-o-boot.sh"

QOB_OUT=$QOB_ROOT/build/qemu-o/persist
QOB_MIB=129
QOB_TIMEOUT=120
QOB_CONTROLLER=virtio
QOB_TCG=0
QOB_BUILD=auto
QOB_BOOT1_INPUT=
QOB_BOOT2_INPUT=
QOB_BOOT1_EXPECT=
QOB_BOOT2_EXPECT=

qop_say() { printf 'qemu-o-persist: %s\n' "$*"; }
qop_die() { printf 'qemu-o-persist: %s\n' "$*" >&2; exit 1; }

qop_usage()
{
	cat <<'EOF'
usage: tools/qemu-o-persist.sh [options]

Boot one fresh disposable GPT disk twice.  Boot one commits a GE/O state
change; boot two must restore that state and commit the next version from
the same O partition.  The default check uses the persistent odemo Counter.

options:
  --rebuild             run the canonical GCC builds before booting
  --no-build            fail if any canonical build artifact is missing
  --output-dir DIR      logs/images directory (default build/qemu-o/persist)
  --mib N               fresh O volume size in MiB (default 129)
  --controller NAME     virtio (default, verified) or ahci (diagnostic;
                        intermittent sdE0: bad disk under this QEMU/q35)
  --timeout SEC         per-boot timeout (default 120)
  --tcg                 use TCG instead of KVM (slow fallback)
  --jobs N              canonical build parallelism (default 8)
  --repl                use the native REPL (-I) on both boots
  --boot1-input FILE    type FILE into boot one's native REPL
  --boot2-input FILE    type FILE into boot two's native REPL
  --boot1-expect TEXT   require TEXT in boot one's serial log
  --boot2-expect TEXT   require TEXT in boot two's serial log
  -h, --help            this message

environment:
  QEMU, OVMF_CODE, OVMF_VARS, QOB_JOBS

generated files (all regular files, never block devices):
  disk.img, o.img, disk.layout, serial-boot{1,2}.log,
  boot-boot{1,2}.log, o-boot{1,2}.{img,json}, report.txt
EOF
}

qop_json_space_n()
{
	python3 - "$1" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print(d['space']['N'])
PY
}

qop_json_mp_sha()
{
	python3 - "$1" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print(d.get('objects', {}).get('4f45584543000003', {}).get('sha256', 'MISSING'))
PY
}

qop_json_graph_ok()
{
	python3 - "$1" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
g = d.get('graph')
print('yes' if g and 'roots' in g and 'objects' in g else 'no')
PY
}

qop_check_common()
{
	local log=$1 tag=$2
	local fails=$3

	qob_has_fixed '[P2 L01] Plan2001 loader' "$log" || { echo "FAIL: $tag loader did not run" >&2; fails=$((fails+1)); }
	qob_has_fixed '[P2 L22] ELF kernel file closed' "$log" || { echo "FAIL: $tag loader did not finish ELF load" >&2; fails=$((fails+1)); }
	qob_has_fixed 'Plan 9' "$log" || { echo "FAIL: $tag no kernel banner" >&2; fails=$((fails+1)); }
	qob_has_fixed 'KERNEL-READY' "$log" || { echo "FAIL: $tag no o-server kernel ready" >&2; fails=$((fails+1)); }
	qob_has_fixed 'o: O volume ' "$log" || { echo "FAIL: $tag O volume not opened" >&2; fails=$((fails+1)); }
	qob_has_fixed "PARTUUID=$QOP_PUUID" "$log" || { echo "FAIL: $tag wrong PARTUUID" >&2; fails=$((fails+1)); }
	qob_has_fixed 'o: OEXECUTE oid=0x4f45584543000003' "$log" || { echo "FAIL: $tag MicroPython OEXECUTE missing" >&2; fails=$((fails+1)); }
	qob_has_fixed 'init: done' "$log" || { echo "FAIL: $tag init: done missing" >&2; fails=$((fails+1)); }
	qob_has_fixed 'PLAN2001-O: BOOT SUCCESS' "$log" || { echo "FAIL: $tag BOOT SUCCESS missing" >&2; fails=$((fails+1)); }
	qob_has_fixed 'PLAN2001-O: GE/O COMMIT SUCCESS' "$log" || { echo "FAIL: $tag GE/O COMMIT SUCCESS missing" >&2; fails=$((fails+1)); }
	qob_has_ere 'sd: controllers initialized \([1-9][0-9]*\) native-only' "$log" ||
		{ echo "FAIL: $tag no native SD controller" >&2; fails=$((fails+1)); }
	if [ "$QOB_CONTROLLER" = virtio ]; then
		qob_has_fixed 'sdvirtio: flush PASS' "$log" || { echo "FAIL: $tag virtio flush PASS missing" >&2; fails=$((fails+1)); }
	fi
	if qob_has_ere 'panic:|suicide|PLAN2001-O: BOOT FAILED|bad disk|error syncing:|cache flush:|i/o error' "$log"; then
		echo "FAIL: $tag fatal panic/disk/flush/sync marker" >&2
		fails=$((fails+1))
	fi
	echo "$fails"
}

qop_main()
{
	local repl_mode=0 puuid n0 n1 n2 mpsha_before mpsha1 mpsha2
	local sha_before sha1 sha2
	local success failcount=0

	while [ $# -gt 0 ]; do
		case "$1" in
		--rebuild) QOB_BUILD=always ;;
		--no-build) QOB_BUILD=never ;;
		--output-dir) [ $# -ge 2 ] || qop_die "--output-dir needs a value"; QOB_OUT=$2; shift ;;
		--mib) [ $# -ge 2 ] || qop_die "--mib needs a value"; QOB_MIB=$2; shift ;;
		--controller) [ $# -ge 2 ] || qop_die "--controller needs a value"; QOB_CONTROLLER=$2; shift ;;
		--timeout) [ $# -ge 2 ] || qop_die "--timeout needs a value"; QOB_TIMEOUT=$2; shift ;;
		--tcg) QOB_TCG=1 ;;
		--jobs) [ $# -ge 2 ] || qop_die "--jobs needs a value"; QOB_JOBS=$2; shift ;;
		--repl) repl_mode=1 ;;
		--boot1-input) [ $# -ge 2 ] || qop_die "--boot1-input needs a file"; QOB_BOOT1_INPUT=$2; shift ;;
		--boot2-input) [ $# -ge 2 ] || qop_die "--boot2-input needs a file"; QOB_BOOT2_INPUT=$2; shift ;;
		--boot1-expect) [ $# -ge 2 ] || qop_die "--boot1-expect needs text"; QOB_BOOT1_EXPECT=$2; shift ;;
		--boot2-expect) [ $# -ge 2 ] || qop_die "--boot2-expect needs text"; QOB_BOOT2_EXPECT=$2; shift ;;
		-h|--help) qop_usage; exit 0 ;;
		*) qop_usage >&2; exit 2 ;;
		esac
		shift
	done

	if [ -n "$QOB_BOOT1_INPUT" ] || [ -n "$QOB_BOOT2_INPUT" ] ||
		[ -n "$QOB_BOOT1_EXPECT" ] || [ -n "$QOB_BOOT2_EXPECT" ]; then
		repl_mode=1
	fi
	[ -z "$QOB_BOOT1_INPUT" ] || [ -r "$QOB_BOOT1_INPUT" ] ||
		qop_die "missing --boot1-input file $QOB_BOOT1_INPUT"
	[ -z "$QOB_BOOT2_INPUT" ] || [ -r "$QOB_BOOT2_INPUT" ] ||
		qop_die "missing --boot2-input file $QOB_BOOT2_INPUT"

	trap qob_cleanup EXIT
	trap 'exit 130' INT
	trap 'exit 143' TERM
	qob_prepare_outdir
	qob_preflight
	qob_build_artifacts

	puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
	QOP_PUUID=$puuid
	printf '%s\n' "$puuid" >"$QOB_OUT/disk.partuuid"
	qob_new_o_image "$QOB_OUT/o.img" "$QOB_OUT/ream"
	qob_write_ini "$QOB_OUT/plan9.ini" "$puuid" /9pc64.elf "$repl_mode"
	qob_make_esp "$QOB_OUT/plan9.ini" "$QOB_OUT/esp.img"
	qob_make_disk "$QOB_OUT/o.img" "$QOB_OUT/disk.img" \
		"$QOB_OUT/esp.img" "$puuid" "$QOB_OUT/disk.layout"

	# Initial state (before the first boot) for the report and a sanity check.
	qob_extract_o "$QOB_OUT/disk.img" "$QOB_OUT/o-initial.img" "$QOB_OUT/disk.layout"
	qob_oinspect "$QOB_OUT/o-initial.img" "$QOB_OUT/o-initial.json" "$QOB_OUT/o-initial.err"
	n0=$(qop_json_space_n "$QOB_OUT/o-initial.json")
	mpsha_before=$(qop_json_mp_sha "$QOB_OUT/o-initial.json")
	sha_before=$(sha256sum "$QOB_OUT/disk.img" | awk '{print $1}')

	if [ "$repl_mode" = 1 ]; then
		success='PLAN2001-O: REPL READY'
	else
		success='PLAN2001-O: SYSTEM IDLE'
	fi

	qop_say "boot one: fresh O image, commit state"
	if ! qob_boot_once "$QOB_OUT/disk.img" boot1 "$success" \
		"$([ "$repl_mode" = 1 ] && echo repl || echo norepl)" \
		"$QOB_BOOT1_INPUT" "$QOB_BOOT1_EXPECT"; then
		qob_show_tail "$QOB_OUT/boot-boot1.log"
		qop_die "boot one FAIL: $QOB_BOOT_REASON (logs in $QOB_OUT)"
	fi
	sha1=$(sha256sum "$QOB_OUT/disk.img" | awk '{print $1}')
	qob_extract_o "$QOB_OUT/disk.img" "$QOB_OUT/o-boot1.img" "$QOB_OUT/disk.layout"
	qob_oinspect "$QOB_OUT/o-boot1.img" "$QOB_OUT/o-boot1.json" "$QOB_OUT/o-boot1.err"
	n1=$(qop_json_space_n "$QOB_OUT/o-boot1.json")
	mpsha1=$(qop_json_mp_sha "$QOB_OUT/o-boot1.json")

	qop_say "boot two: same disk and O partition, restore state"
	if ! qob_boot_once "$QOB_OUT/disk.img" boot2 "$success" \
		"$([ "$repl_mode" = 1 ] && echo repl || echo norepl)" \
		"$QOB_BOOT2_INPUT" "$QOB_BOOT2_EXPECT"; then
		qob_show_tail "$QOB_OUT/boot-boot2.log"
		qop_die "boot two FAIL: $QOB_BOOT_REASON (logs in $QOB_OUT)"
	fi
	sha2=$(sha256sum "$QOB_OUT/disk.img" | awk '{print $1}')
	qob_extract_o "$QOB_OUT/disk.img" "$QOB_OUT/o-boot2.img" "$QOB_OUT/disk.layout"
	qob_oinspect "$QOB_OUT/o-boot2.img" "$QOB_OUT/o-boot2.json" "$QOB_OUT/o-boot2.err"
	n2=$(qop_json_space_n "$QOB_OUT/o-boot2.json")
	mpsha2=$(qop_json_mp_sha "$QOB_OUT/o-boot2.json")

	local log1="$QOB_OUT/boot-boot1.log" log2="$QOB_OUT/boot-boot2.log"
	local expected_mp off1 off2 v1_commit v2_restore v2_commit
	expected_mp=$(sha256sum "$QOB_MPY" | awk '{print $1}')
	off1=$(grep -ao 'O volume [^@]*@[0-9]*' "$log1" | head -1 || true)
	off2=$(grep -ao 'O volume [^@]*@[0-9]*' "$log2" | head -1 || true)
	v1_commit=$(grep -ao 'space: O: committed init version [0-9]*' "$log1" | tail -1 | awk '{print $NF}' || true)
	v2_restore=$(grep -ao 'space: O: restored init version [0-9]*' "$log2" | head -1 | awk '{print $NF}' || true)
	v2_commit=$(grep -ao 'space: O: committed init version [0-9]*' "$log2" | tail -1 | awk '{print $NF}' || true)

	failcount=$(qop_check_common "$log1" boot1 "$failcount")
	failcount=$(qop_check_common "$log2" boot2 "$failcount")

	# Boot one: a genuinely fresh image and its first commit.
	qob_has_fixed 'space: O: init has no graph' "$log1" || { echo "FAIL: boot1 image was not fresh" >&2; failcount=$((failcount+1)); }
	qob_has_fixed 'space: O: init initialized from O code (odemo)' "$log1" || { echo "FAIL: boot1 init not initialized" >&2; failcount=$((failcount+1)); }
	qob_has_fixed 'init: run() -> 1' "$log1" || { echo "FAIL: boot1 run() -> 1 missing" >&2; failcount=$((failcount+1)); }
	qob_has_fixed 'space: O: committed init version 1' "$log1" || { echo "FAIL: boot1 commit version 1 missing" >&2; failcount=$((failcount+1)); }
	qob_has_fixed 'init: shared Counter n 1, alias same' "$log1" || { echo "FAIL: boot1 persistent Counter n 1 missing" >&2; failcount=$((failcount+1)); }

	# Boot two: the value/identity must come from O, not from a fresh module.
	qob_has_fixed 'space: O: init restored (odemo), no module initialization' "$log2" || { echo "FAIL: boot2 reinitialized the module" >&2; failcount=$((failcount+1)); }
	qob_has_fixed 'init: run() -> 2' "$log2" || { echo "FAIL: boot2 run() -> 2 missing" >&2; failcount=$((failcount+1)); }
	qob_has_fixed 'init: shared Counter n 2, alias same' "$log2" || { echo "FAIL: boot2 persistent Counter n 2 missing" >&2; failcount=$((failcount+1)); }
	[ -n "$v1_commit" ] && [ -n "$v2_restore" ] && [ -n "$v2_commit" ] ||
		{ echo "FAIL: boot1/boot2 Space version markers missing" >&2; failcount=$((failcount+1)); }
	[ "$v2_restore" = "$v1_commit" ] ||
		{ echo "FAIL: boot2 restored version $v2_restore, boot1 final commit $v1_commit" >&2; failcount=$((failcount+1)); }
	[ "$v2_commit" -gt "$v2_restore" ] 2>/dev/null ||
		{ echo "FAIL: boot2 commit $v2_commit did not advance past restore $v2_restore" >&2; failcount=$((failcount+1)); }
	if [ -z "$QOB_BOOT1_INPUT" ] && [ -z "$QOB_BOOT2_INPUT" ]; then
		[ "$v1_commit" = 1 ] && [ "$v2_restore" = 1 ] && [ "$v2_commit" = 2 ] ||
			{ echo "FAIL: default Counter path versions are not 1 -> restore 1 -> commit 2" >&2; failcount=$((failcount+1)); }
	fi
	[ "$(grep -c '^init: shared Counter n 1, alias same' "$log1" || true)" = 1 ] ||
		{ echo "FAIL: boot1 Counter identity count is not 1" >&2; failcount=$((failcount+1)); }
	[ "$(grep -c '^init: shared Counter n 2, alias same' "$log2" || true)" = 1 ] ||
		{ echo "FAIL: boot2 Counter identity count is not 1" >&2; failcount=$((failcount+1)); }

	[ "$sha_before" != "$sha1" ] || { echo "FAIL: boot1 did not change the disk image" >&2; failcount=$((failcount+1)); }
	[ "$sha1" != "$sha2" ] || { echo "FAIL: boot2 did not commit a new disk state" >&2; failcount=$((failcount+1)); }
	[ -n "$off1" ] && [ "$off1" = "$off2" ] ||
		{ echo "FAIL: the O partition offset changed between boots" >&2; failcount=$((failcount+1)); }
	[ "$n1" -gt "$n0" ] 2>/dev/null || { echo "FAIL: boot1 did not advance O Space N (initial $n0, after $n1)" >&2; failcount=$((failcount+1)); }
	[ "$n2" -gt "$n1" ] 2>/dev/null || { echo "FAIL: boot2 did not advance O Space N (boot1 $n1, boot2 $n2)" >&2; failcount=$((failcount+1)); }
	[ "$(qop_json_graph_ok "$QOB_OUT/o-boot1.json")" = yes ] ||
		{ echo "FAIL: boot1 O image has no persistent graph" >&2; failcount=$((failcount+1)); }
	[ "$(qop_json_graph_ok "$QOB_OUT/o-boot2.json")" = yes ] ||
		{ echo "FAIL: boot2 O image has no persistent graph" >&2; failcount=$((failcount+1)); }
	[ "$mpsha_before" = "$expected_mp" ] && [ "$mpsha1" = "$expected_mp" ] && [ "$mpsha2" = "$expected_mp" ] ||
		{ echo "FAIL: OID_MP_EXEC bytes changed or differ from the build" >&2; failcount=$((failcount+1)); }

	{
		printf 'Plan2001-O QEMU native O persistence check\n'
		printf 'result=%s\n' "$([ "$failcount" = 0 ] && echo PASS || echo FAIL)"
		printf 'controller=%s\n' "$QOB_CONTROLLER"
		printf 'accel=%s\n' "$([ "$QOB_TCG" = 1 ] && echo tcg || echo kvm)"
		printf 'boot_mode=%s\n' "$([ "$repl_mode" = 1 ] && echo repl || echo norepl)"
		printf 'qemu=%s\n' "$("$QOB_QEMU" --version | head -1)"
		printf 'ovmf_code=%s\novmf_vars=%s\n' "$QOB_OVMF_CODE" "$QOB_OVMF_VARS"
		printf 'kernel=%s\nkernel_sha256=%s\n' "$QOB_KERNEL" "$(sha256sum "$QOB_KERNEL" | awk '{print $1}')"
		printf 'loader_sha256=%s\n' "$(sha256sum "$QOB_EFI" | awk '{print $1}')"
		printf 'micropython_sha256=%s\n' "$expected_mp"
		printf 'disk=%s\n' "$QOB_OUT/disk.img"
		printf 'partuuid=%s\n' "$puuid"
		printf 'disk_sha256_before=%s\n' "$sha_before"
		printf 'disk_sha256_after_boot1=%s\n' "$sha1"
		printf 'disk_sha256_after_boot2=%s\n' "$sha2"
		printf 'o_volume_identity_boot1=%s\n' "$off1"
		printf 'o_volume_identity_boot2=%s\n' "$off2"
		printf 'o_space_N_before=%s\n' "$n0"
		printf 'o_space_N_after_boot1=%s\n' "$n1"
		printf 'o_space_N_after_boot2=%s\n' "$n2"
		printf 'o_mp_sha256=%s\n' "$mpsha2"
		printf 'boot1_restored_or_fresh=%s\n' "$(qob_stage "$log1" 'space: O: init has no graph')"
		printf 'boot1_commit_v1=%s\n' "$(qob_stage "$log1" 'space: O: committed init version 1')"
		printf 'boot1_final_commit_version=%s\n' "$v1_commit"
		printf 'boot2_restore_version=%s\n' "$v2_restore"
		printf 'boot2_final_commit_version=%s\n' "$v2_commit"
		printf 'boot2_restored_version_matches=%s\n' "$([ "$v2_restore" = "$v1_commit" ] && echo PASS || echo FAIL)"
		printf 'boot1_counter_n1=%s\n' "$(qob_stage "$log1" 'init: shared Counter n 1, alias same')"
		printf 'boot2_counter_n2=%s\n' "$(qob_stage "$log2" 'init: shared Counter n 2, alias same')"
		printf 'logs=%s\n' "$QOB_OUT"
		printf '%s\n' '--- boot1 serial tail ---'
		tail -35 "$log1" || true
		printf '%s\n' '--- boot2 serial tail ---'
		tail -35 "$log2" || true
	} | tee "$QOB_OUT/report.txt"

	if [ "$failcount" -ne 0 ]; then
		qop_die "$failcount persistence checkpoint(s) failed (report: $QOB_OUT/report.txt)"
	fi
	qop_say "PASS: same disposable disk/O partition, boot1 commit and boot2 restore confirmed ($QOB_OUT/report.txt)"
}

if [ "${QEMU_O_PERSIST_LIB:-0}" != 1 ]; then
	qop_main "$@"
fi
