#!/usr/bin/env bash
# Boot an unchanged-source newlib-full probe in the real Plan2001 AMD64 QEMU guest.
# No existing GE/O volumes are touched.
set -euo pipefail
if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo "usage: guest_newlib-full.sh KERNEL_SOURCE NATIVE_ELF REPORT_DIR [full|process]" >&2
  exit 2
fi
KERNEL=$(cd "$1" && pwd)
TARGET=$(realpath "$2")
REPORTS=$(mkdir -p "$3" && cd "$3" && pwd)
MODE=${4:-full}
case "$MODE" in
  full) MARKER=PLAN2001-NEWLIB-FULL-PASS ;;
  process) MARKER=PLAN2001-PROCESS-PASS ;;
  *) echo "Unknown probe mode: $MODE" >&2; exit 2 ;;
esac
OUT="$REPORTS/qemu-newlib-$MODE"
mkdir -p "$OUT"
[ -r "$TARGET" ] || { echo "Missing native newlib-full ELF" >&2; exit 1; }
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

MPEXEC="$TARGET" MPYCROSS="$QOB_MPYCROSS" FORMATO="$QOB_FORMAT" \
  bash "$KERNEL/tools/mkovol" "$OUT/o.img" 129 \
  "$KERNEL/tools/host/odemo.py" > "$OUT/mkovol.log" 2>&1
python3 "$KERNEL/tools/oinspect.py" "$OUT/o.img" > "$OUT/oinspect.json"
python3 - "$TARGET" "$OUT/oinspect.json" <<'PY'
import hashlib
import json
import pathlib
import sys
binary = pathlib.Path(sys.argv[1]).read_bytes()
meta = json.loads(pathlib.Path(sys.argv[2]).read_text())
stored = meta["objects"].get("4f45584543000003")
if not stored or stored["bytes"] != len(binary) or stored["sha256"] != hashlib.sha256(binary).hexdigest():
    raise SystemExit("Exact native newlib-full ELF not present at boot OID")
print("Verified exact newlib-full ELF in GE/O via SHA-256")
PY
puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
qob_make_disk "$OUT/o.img" "$OUT/disk.img" "$OUT/esp.img" \
  "$puuid" "$OUT/disk.layout"
if ! qob_boot_once "$OUT/disk.img" newlib-fullboot \
      "$MARKER" norepl "" ""; then
  echo "newlib-full QEMU guest FAILED: $QOB_BOOT_REASON" >&2
  tail -90 "$OUT/boot-newlib-fullboot.log" >&2 || true
  exit 1
fi
log="$OUT/boot-newlib-fullboot.log"
grep -aqF 'o: OEXECUTE oid=0x4f45584543000003' "$log"
if [ "$MODE" = process ]; then
  grep -aqF 'PLAN2001-PROCESS-FORK-EXEC-WAIT-PASS' "$log"
  grep -aqF 'PLAN2001-PROCESS-SIGNALS-PASS' "$log"
  grep -aqF 'PLAN2001-PROCESS-STDIO-PASS' "$log"
  grep -aqF 'PLAN2001-PROCESS-SIGLONGJMP-PASS' "$log"
  grep -aqF 'PLAN2001-PROCESS-FILES-PASS' "$log"
  if grep -aqF 'PLAN2001-PROCESS-FAIL' "$log"; then
    echo 'Native process probe emitted failure marker' >&2; exit 1
  fi
  echo "Native Plan2001/QEMU fork, exec, wait and asynchronous signals PASS ($log)"
  exit 0
fi
for op in PRINTF MALLOC LIBM TIME ERRNO STDIN FD UXPIPE UXFILE; do
  grep -aqF "PLAN2001-NEWLIB-FULL-$op-PASS" "$log"
done
grep -aqF 'PLAN2001-NEWLIB-FULL-PASS' "$log"
if grep -aqF 'PLAN2001-NEWLIB-FULL-GUEST-FAIL' "$log"; then
  echo "Native newlib-full probe emitted failure marker" >&2
  exit 1
fi
cat > "$REPORTS/newlib-full-guest.md" <<'EOF'
### Full upstream Newlib libc executing in real Plan2001 QEMU

- **PASS:** native ELF seeded in disposable GE/O and verified by SHA-256.
- **PASS:** Plan2001 OEXECUTEd the ELF under QEMU/OVMF.
- **PASS:** upstream libc printf/snprintf, malloc/free, strtol, errno and libm executed; the native stdin ring and authorized stdout were exercised.
- **PASS:** no Plan2001 libc-user.a; native syscall stubs, target crt and upstream Newlib libc/libm.
- **NOT RUN:** complete POSIX namespace, multithreading, hardware.
EOF
echo "Native Plan2001/QEMU newlib-full probe PASS ($log)"
