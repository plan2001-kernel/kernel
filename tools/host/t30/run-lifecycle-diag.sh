#!/usr/bin/env bash
# Build-time diagnostic run for issue #30: live Pgrp/OContext accounting.
#
# The kernel build adds non-ABI T30LIFE counter and sequence lines.  The
# parser verifies every marker against the baseline and checks per-Pgrp live
# IDs, allocation/release balance and sequence monotonicity.  The canonical
# non-diagnostic kernel is always rebuilt on exit; a failed restoration is a
# script failure even when the diagnostic test itself passed.
#
# Test-only hooks:
#   T30D_RESTORE_CMD          override the canonical rebuild command
#   T30D_SELFTEST=1           skip build/run/parse and exit with
#                             T30D_SELFTEST_STATUS to exercise the EXIT trap
#   T30D_SELFTEST_SKIP_VALIDATE=1  skip artifact validation for those tests
set -euo pipefail
T30D_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T30D_OUT=${T30D_OUT:-$T30D_ROOT/build/t30/diag}
mkdir -p "$T30D_OUT"
T30D_OUT=$(realpath "$T30D_OUT")
T30D_RESTORE_CMD=${T30D_RESTORE_CMD:-"$T30D_ROOT/tools/build-gcc.sh --kernel"}
T30D_CLEAN_BUILD_LOG=$T30D_OUT/canonical-build.log

# Confirm the effective canonical build, not merely the rebuild exit code.
validate_canonical()
{
	local cmd=$T30D_ROOT/build/gcc/log/pgrp.cmd
	local elf=$T30D_ROOT/build/gcc/9pc64.elf

	if [ ! -f "$cmd" ] || [ ! -f "$elf" ]; then
		echo "T30: canonical build artifacts are missing" >&2
		return 1
	fi
	if grep -q 'PLAN2001_LIFECYCLE_DIAG' "$cmd"; then
		echo "T30: canonical pgrp.cmd still contains the diagnostic define" >&2
		return 1
	fi
	if nm "$elf" 2>/dev/null | grep -qw 'lifepgrp'; then
		echo "T30: canonical kernel ELF still exports lifepgrp" >&2
		return 1
	fi
	return 0
}

restore()
{
	local status=$?
	local restore_status=0

	trap - EXIT
	if ! env -u PLAN2001_LIFECYCLE_DIAG bash -c "$T30D_RESTORE_CMD" \
			>"$T30D_CLEAN_BUILD_LOG" 2>&1; then
		restore_status=1
	elif [ "${T30D_SELFTEST_SKIP_VALIDATE:-0}" != 1 ] &&
			! validate_canonical; then
		restore_status=1
	fi
	if [ "$restore_status" -ne 0 ]; then
		echo "T30: ERROR: canonical non-diagnostic kernel restoration FAILED" >&2
		echo "T30: see $T30D_CLEAN_BUILD_LOG" >&2
		if [ "$status" -eq 0 ]; then
			exit 1
		fi
		echo "T30: diagnostic run also failed with status $status" >&2
	fi
	exit "$status"
}
trap restore EXIT

if [ "${T30D_SELFTEST:-0}" = 1 ]; then
	exit "${T30D_SELFTEST_STATUS:-0}"
fi

PLAN2001_LIFECYCLE_DIAG=1 "$T30D_ROOT/tools/build-gcc.sh" --kernel \
	>"$T30D_OUT/diag-build.log" 2>&1

T30_OUT="$T30D_OUT/run" "$T30D_ROOT/tools/host/t30/run-process-lifecycle-checks.sh" "$@"

python3 "$T30D_ROOT/tools/host/t30/lifecycle_log.py" \
	"$T30D_OUT/run/boot-boot.log"
