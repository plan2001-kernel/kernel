#!/usr/bin/env bash
# First-class volatile ObjectSpace integration, not a host-side mock.
# Reuses the native AMD64 kernel/UEFI/O-volume/QEMU path.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${OBJECTSPACE_TEST_OUT:-$ROOT/build/objectspace/run}
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_main --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 \
  --mib 129 --timeout 420 --repl-input \
  "$ROOT/tools/host/objectspace/guest-repl.txt" \
  --output-dir "$OUT" --expect '^OBJECTSPACE-PASS' "$@"
python3 - "$OUT/boot-boot.log" <<'PY'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text(errors="replace")
import re
assert re.search(r"^OBJECTSPACE-PASS", s, re.M), "missing executed guest proof marker"
assert "AssertionError" not in s and "Traceback" not in s and "panic:" not in s
print("PASS: native ObjectSpace REPL graph acceptance")
PY
