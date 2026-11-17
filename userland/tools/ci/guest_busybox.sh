#!/usr/bin/env bash
# Launch the real Plan2001 AMD64 kernel with the BusyBox applet-subset
# *guest smoke image* in a disposable GE/O volume. Never touches disks.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: guest_busybox.sh KERNEL_SOURCE SMOKE_ELF REPORT_DIR" >&2
    exit 2
fi

KERNEL=$(cd "$1" && pwd)
TARGET=$(realpath "$2")
REPORTS=$(mkdir -p "$3" && cd "$3" && pwd)
OUT="$REPORTS/qemu-busybox"
mkdir -p "$OUT"

[ -r "$TARGET" ] || { echo "No native BusyBox smoke ELF: $TARGET" >&2; exit 1; }
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

# Userland fixture is installed only into a fresh regular-file disk image.
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
    raise SystemExit("GE/O executable OID mismatch for BusyBox smoke image")
print("Confirmed SHA-256 of exact BusyBox native smoke ELF stored at boot OID")
PY

puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
qob_make_disk "$OUT/o.img" "$OUT/disk.img" "$OUT/esp.img" \
    "$puuid" "$OUT/disk.layout"

marker='PLAN2001-BUSYBOX-GUEST-PASS'
if ! qob_boot_once "$OUT/disk.img" busyboxboot "$marker" norepl "" ""; then
    echo "Native BusyBox guest boot FAILED: $QOB_BOOT_REASON" >&2
    tail -90 "$OUT/boot-busyboxboot.log" >&2 || true
    exit 1
fi

log="$OUT/boot-busyboxboot.log"
# All markers must be emitted from the guest. Echo markers originate from
# *upstream echo_main*, not from a fake console printing test strings.
grep -aqF 'PLAN2001-BUSYBOX-ECHO-HELLO' "$log"
grep -aqF 'PLAN2001-BUSYBOX-ALIAS-OK' "$log"
grep -aqF 'PLAN2001-BUSYBOX-GUEST-PASS' "$log"
grep -aqF 'o: OEXECUTE oid=0x4f45584543000003' "$log"
if grep -aqF 'PLAN2001-BUSYBOX-GUEST-FAIL' "$log"; then
    echo "Guest returned explicit BusyBox smoke failure" >&2
    exit 1
fi

cat > "$REPORTS/busybox-guest.md" <<'EOF'
## BusyBox applet-subset execution in real Plan2001 QEMU

- **PASS:** Exact native freestanding smoke ELF stored at boot executable OID and verified by SHA-256.
- **PASS:** Plan2001 kernel OEXECUTEd the GE/O object on an OVMF/QEMU guest.
- **PASS:** Original BusyBox echo.c printed the expected stdout marker through the native ObjOpenConsole capability.
- **PASS:** Direct argv[0]=/bin/echo alias dispatch and stdout.
- **PASS:** Original BusyBox true.c and false.c returned 0 and 1, respectively; unknown applet returned 127.
- **PASS:** Guest printed a separate success marker only after all checks.
- **LIMITATION:** Native subset launcher with 3 original applets and a minimal libbb shim; not the full upstream BusyBox multicall build.
- **LIMITATION:** Owner boot process stays alive after the result because boot-owner exit is not yet an ordinary shell process exit.
- **NOT RUN:** Native shell, filesystem POSIX adapters, physical hardware.
EOF
echo "Native BusyBox true/false/echo QEMU guest execution: PASS ($log)"
