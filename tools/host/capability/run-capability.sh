#!/usr/bin/env bash
# Native AMD64 QEMU proof: capability derivation and subtree revocation.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${CAPLINEAGE_TEST_OUT:-$ROOT/build/capability/run}
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_main --accel kvm:tcg --cpus 2 --ncpu 2 --obootcpu 0 \
  --obootexec 0x4f45584543000003 \
  --mib 129 --timeout 420 --repl-input \
  "$ROOT/tools/host/capability/guest-repl.txt" \
  --output-dir "$OUT" --expect '^CAPLINEAGE-PASS' "$@"
python3 - "$OUT/boot-boot.log" <<'PY'
from pathlib import Path
import re
import sys
s = Path(sys.argv[1]).read_text(errors="replace")
assert re.search(r"^CAPLINEAGE-PASS",s,re.M), "missing guest proof marker"
assert "AssertionError" not in s and "Traceback" not in s and "panic:" not in s
print("PASS: native capability derivation and descendant revocation")
PY
