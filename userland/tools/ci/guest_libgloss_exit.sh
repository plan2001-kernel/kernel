#!/usr/bin/env bash
# Verify _exit in true child processes observed via Process Handles.
# Input GE/O image is a new temporary volume seeded by the Userland CI.
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo "usage: guest_libgloss_exit.sh KERNEL_SOURCE SEEDED_IMAGE REPORTS" >&2
  exit 2
fi
KERNEL=$(cd "$1" && pwd)
SOURCE_IMAGE=$(realpath "$2")
REPORTS=$(cd "$3" && pwd)
OUT="$REPORTS/qemu-libgloss-exit"
mkdir -p "$OUT"
[ -r "$SOURCE_IMAGE" ] || { echo "Missing seeded GE/O input" >&2; exit 1; }

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

puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
qob_make_disk "$SOURCE_IMAGE" "$OUT/disk.img" "$OUT/esp.img" \
  "$puuid" "$OUT/disk.layout"
if ! qob_boot_once "$OUT/disk.img" libgloss-exit \
    'PLAN2001-LIBGLOSS-EXIT-PASS' norepl "" ""; then
  echo "libgloss _exit child test FAILED: $QOB_BOOT_REASON" >&2
  tail -100 "$OUT/boot-libgloss-exit.log" >&2 || true
  exit 1
fi
log="$OUT/boot-libgloss-exit.log"
grep -aqF 'o: OEXECUTE oid=0x4f45584543000003' "$log"
grep -aqF 'PLAN2001-LIBGLOSS-EXIT-ZERO-PASS' "$log"
grep -aqF 'PLAN2001-LIBGLOSS-EXIT-NONZERO-PASS' "$log"
grep -aqF 'PLAN2001-LIBGLOSS-EXIT-PASS' "$log"
grep -aqF 'init: run() -> 0' "$log"
if grep -aqF 'PLAN2001-O: BOOT FAILED' "$log"; then
  echo "MicroPython exit-observer init failed" >&2
  exit 1
fi
cat > "$REPORTS/libgloss-exit.md" <<'EOF'
### Native libgloss _exit verified through Plan2001 process observation

- **PASS:** ordinary native _exit child ELF seeded into fresh GE/O and verified by SHA-256.
- **PASS:** native MicroPython SPAWNed both success and failure children.
- **PASS:** Process Handle completed with voluntary-exit cause; empty status
  for _exit(0) and nonempty status for _exit(7).
- **LIMITATION:** Plan2001 EXITS provides success/failure, not an exact numeric POSIX exit code.
- **NOT RUN:** entire Newlib libc/libgloss and POSIX process model.
EOF
echo "Native Plan2001 QEMU _exit child status: PASS ($log)"
