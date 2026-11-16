#!/usr/bin/env bash
# Optional local test: disposable GE/O + actual native MicroPython REPL.
# Does not create/modify a GitHub Actions workflow or touch physical disks.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${SETTHEORY_TEST_OUT:-$ROOT/build/settheory/run}
export SETTHEORY=1
export QEMU_O_BOOT_LIB=1
export QOB_REPL_SYNC_LINES=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_main --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 \
  --obootexec 0x4f45584543000003 \
  --mib 129 --timeout 420 --repl-input \
  "$ROOT/tools/host/settheory/guest-repl.txt" \
  --output-dir "$OUT" --expect '^SETTHEORY-PASS' "$@"
python3 - "$OUT/boot-boot.log" <<'PY'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text(errors="replace")
assert "SETTHEORY-PASS" in s, "guest did not finish"
assert "Traceback" not in s and "panic:" not in s
print("PASS: native Plan2001 GE/O set algebra REPL")
PY
