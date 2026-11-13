#!/usr/bin/env bash
# End-to-end BootInfo manifest handoff and independent process roots.
# All images are disposable regular files, never host block devices.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
QOB_OUT=${BOOT_MANIFEST_OUT:-"$ROOT/build/bootmanifest/run"}
QOB_TIMEOUT=240
QOB_ACCEL=${QOB_ACCEL:-kvm:tcg}
QOB_CPUS=2
QOB_INI_NCPU=2
QOB_BUILD=never
trap qob_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
qob_prepare_outdir
qob_preflight
qob_build_artifacts

puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
printf '%s\n' "$puuid" > "$QOB_OUT/disk.partuuid"
qob_new_o_image "$QOB_OUT/o.img" "$QOB_OUT/ream"
qob_write_ini "$QOB_OUT/plan9.ini" "$puuid" /9pc64.elf 0
printf 'bootmanifest=/processes.conf\nbootconsoletrace=1\n' >> "$QOB_OUT/plan9.ini"
cat > "$QOB_OUT/processes.conf" <<EOF
version=1
[process.memoryprobe]
source=memory:selftest
owner=probe
cpu=1
required=false
[process.missing]
source=geo:PARTUUID=$puuid:OID=0x10020304
owner=missing
cpu=1
required=false
[process.python]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=python
cpu=0
args=-N
required=true
EOF
qob_make_esp "$QOB_OUT/plan9.ini" "$QOB_OUT/esp.img"
mcopy -i "$QOB_OUT/esp.img" "$QOB_OUT/processes.conf" ::/processes.conf
qob_make_disk "$QOB_OUT/o.img" "$QOB_OUT/disk.img" \
 "$QOB_OUT/esp.img" "$puuid" "$QOB_OUT/disk.layout"

if ! qob_boot_once "$QOB_OUT/disk.img" manifest \
 'PLAN2001-O: SYSTEM IDLE' norepl '' 'PLAN2001-O: BOOT SUCCESS'; then
  qob_show_tail "$QOB_OUT/boot-manifest.log"
  qob_die "manifest boot failed: $QOB_BOOT_REASON"
fi
log="$QOB_OUT/boot-manifest.log"
python3 - "$log" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text(errors="replace")
for marker in [
    "[P2 L12] separate boot manifest loaded",
    "boot-manifest: validated 3 independent process entries",
    "boot-manifest: START memoryprobe source=memory cpu=1 required=0",
    "boot-manifest: START missing source=geo cpu=1 required=0",
    "boot-manifest: START python source=geo cpu=0 required=1",
    "PLAN2001-O: BOOT SUCCESS",
    "PLAN2001-O: SYSTEM IDLE",
]:
    assert marker in s, f"missing marker: {marker}"
assert "panic:" not in s, "kernel panic"
assert "boot process died:" not in s, "one process failure terminated system"
assert "o: OEXECUTE oid=0x4f45584543000003" in s, "main GE/O executable never ran"
assert "o-server: OID=0x10020304" in s, "missing GE/O executable not attempted"
print("PASS: EFI manifest -> BootInfo -> three independent process starts")
PY

# Separate full-system boot: two live REPL processes on separate CPUs,
# independent of an intentionally terminated memory process and a failing
# executable.  No user process owns the display or physical keyboard.
qob_new_o_image "$QOB_OUT/o-consoles.img" "$QOB_OUT/ream-consoles"
qob_write_ini "$QOB_OUT/plan9-consoles.ini" "$puuid" /9pc64.elf 0
printf 'bootmanifest=/consoles.conf\nbootconsoletrace=1\n' >> "$QOB_OUT/plan9-consoles.ini"
cat > "$QOB_OUT/consoles.conf" <<EOF
version=1
[process.alpha]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=alpha
cpu=0
args=-I
required=true
[process.beta]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=beta
cpu=1
args=-I
required=true
[process.memoryprobe]
source=memory:selftest
owner=probe
required=false
[process.missing]
source=geo:PARTUUID=$puuid:OID=0x10020304
owner=missing
required=false
EOF
qob_make_esp "$QOB_OUT/plan9-consoles.ini" "$QOB_OUT/esp-consoles.img"
mcopy -i "$QOB_OUT/esp-consoles.img" "$QOB_OUT/consoles.conf" ::/consoles.conf
qob_make_disk "$QOB_OUT/o-consoles.img" "$QOB_OUT/disk-consoles.img" \
  "$QOB_OUT/esp-consoles.img" "$puuid" "$QOB_OUT/disk-consoles.layout"
if ! qob_boot_once "$QOB_OUT/disk-consoles.img" consoles \
  'boot-console: SYSTEM READY framebuffer=1 keyboard=1' consoles \
  "$ROOT/tools/host/bootmanifest/inject-consoles.py" 'boot-console: FOCUS kernel'; then
  qob_show_tail "$QOB_OUT/boot-consoles.log"
  qob_die "kernel-owned console test failed: $QOB_BOOT_REASON"
fi
python3 - "$QOB_OUT/boot-consoles.log" <<'PY'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text(errors="replace").replace("\r", "")
for marker in (
    "boot-manifest: validated 4 independent process entries",
    "boot-console: SESSION alpha",
    "boot-console: SESSION beta",
    "boot-console: EXIT memoryprobe",
    "boot-console: EXIT missing",
    "boot-console: FOCUS alpha",
    "boot-console: FOCUS beta",
    "boot-console: FOCUS kernel",
    "777777",
    "555555",
):
    assert marker in s, f"missing marker: {marker}"
assert "panic:" not in s
assert "boot process died:" not in s
print("PASS: independent boot roots, separate console regions, kernel framebuffer and focus")
PY
