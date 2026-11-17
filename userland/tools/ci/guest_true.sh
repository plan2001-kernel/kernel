#!/usr/bin/env bash
# Boot the real Plan2001 AMD64 kernel with a native-linked BusyBox true applet
# replacing MicroPython at the boot executable OID on a disposable GE/O volume.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: guest_true.sh KERNEL_SOURCE NATIVE_TRUE_ELF REPORT_DIR" >&2
    exit 2
fi

KERNEL=$(cd "$1" && pwd)
TARGET=$(realpath "$2")
REPORTS=$(mkdir -p "$3" && cd "$3" && pwd)
OUT="$REPORTS/qemu-busybox-true"
mkdir -p "$OUT"

[ -r "$TARGET" ] || { echo "No native BusyBox true ELF: $TARGET" >&2; exit 1; }
[ -f "$KERNEL/tools/qemu-o-boot.sh" ] || exit 1

export QEMU_O_BOOT_LIB=1
# shellcheck disable=SC1091
source "$KERNEL/tools/qemu-o-boot.sh"
QOB_OUT="$OUT"
QOB_ACCEL=kvm:tcg
QOB_TIMEOUT=180
QOB_NO_REPL=1
QOB_INI_BOOTEXEC=0x4f45584543000003
QOB_BUILD=never
qob_prepare_outdir
qob_preflight
trap qob_cleanup EXIT

# The kernel's approved QEMU/OVMF functions operate only on disposable files.
MPEXEC="$TARGET" MPYCROSS="$QOB_MPYCROSS" FORMATO="$QOB_FORMAT" \
    bash "$KERNEL/tools/mkovol" "$OUT/o.img" 129 \
    "$KERNEL/tools/host/odemo.py" > "$OUT/mkovol.log" 2>&1

python3 "$KERNEL/tools/oinspect.py" "$OUT/o.img" > "$OUT/oinspect.json"
python3 - "$TARGET" "$OUT/oinspect.json" <<'PY'
import hashlib
import json
import pathlib
import sys
target = pathlib.Path(sys.argv[1]).read_bytes()
meta = json.loads(pathlib.Path(sys.argv[2]).read_text())
oid = meta["objects"].get("4f45584543000003")
if oid is None or oid["bytes"] != len(target) or oid["sha256"] != hashlib.sha256(target).hexdigest():
    raise SystemExit("GE/O executable OID does not match the native-linked BusyBox true ELF")
print("Confirmed exact native BusyBox true ELF stored at boot executable OID")
PY

puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
qob_make_disk "$OUT/o.img" "$OUT/disk.img" "$OUT/esp.img" \
    "$puuid" "$OUT/disk.layout"

marker='PLAN2001-USERLAND-BUSYBOX-TRUE-EXECUTED'
if ! qob_boot_once "$OUT/disk.img" trueboot "$marker" norepl "" ""; then
    echo "Native BusyBox true guest boot FAILED: $QOB_BOOT_REASON" >&2
    tail -75 "$OUT/boot-trueboot.log" >&2 || true
    exit 1
fi

grep -aqF "$marker" "$OUT/boot-trueboot.log"
grep -aqF 'o: OEXECUTE oid=0x4f45584543000003' "$OUT/boot-trueboot.log"
cat >"$REPORTS/busybox-true-guest.md" <<'EOF'
## BusyBox true execution inside Plan2001 QEMU

- PASS: exact native-linked ELF seeded into a disposable GE/O volume and verified by SHA-256.
- PASS: kernel OEXECUTEd that object and userspace emitted a unique marker through an authorized native console capability.
- LIMITATION: single BusyBox true.c applet with a small diagnostic shim; not a BusyBox multicall executable or POSIX implementation.
- NOT RUN: physical hardware.
EOF
echo "Native BusyBox true guest execution: PASS ($OUT/boot-trueboot.log)"
