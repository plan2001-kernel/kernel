#!/usr/bin/env bash
# Boot an ordinary MicroPython init which SPAWNs the regular BusyBox ELF.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: guest_busybox_spawn.sh KERNEL_SOURCE SEEDED_GE_O_IMAGE REPORT_DIR" >&2
    exit 2
fi

KERNEL=$(cd "$1" && pwd)
SOURCE_IMAGE=$(realpath "$2")
REPORTS=$(cd "$3" && pwd)
OUT="$REPORTS/qemu-busybox-spawn"
mkdir -p "$OUT"

[ -r "$SOURCE_IMAGE" ] || { echo "Missing seeded GE/O image" >&2; exit 1; }
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

marker='PLAN2001-BUSYBOX-SPAWN-PASS'
if ! qob_boot_once "$OUT/disk.img" micropython-busybox "$marker" norepl "" ""; then
    echo "MicroPython SPAWN BusyBox guest FAILED: $QOB_BOOT_REASON" >&2
    tail -100 "$OUT/boot-micropython-busybox.log" >&2 || true
    exit 1
fi

log="$OUT/boot-micropython-busybox.log"
grep -aqF 'o: OEXECUTE oid=0x4f45584543000003' "$log"
grep -aqF 'PLAN2001-BUSYBOX-SPAWN-ECHO' "$log"
grep -aqF 'PLAN2001-BUSYBOX-SPAWN-PASS' "$log"
grep -aqF 'PLAN2001-EXTERNAL-LS-OUTPUT-START' "$log"
grep -aqF 'PLAN2001-EXTERNAL-LS-PASS' "$log"
grep -aqF 'PLAN2001-UXPIPE-QEMU-PASS' "$log"
grep -aqF 'bin' "$log"
grep -aqF 'usr' "$log"
grep -aqF 'init: run() -> 0' "$log"
if grep -aqF 'PLAN2001-O: BOOT FAILED' "$log"; then
    echo "MicroPython init boot failed after launch" >&2
    exit 1
fi

cat >"$REPORTS/busybox-spawn-guest.md" <<'EOF'
## BusyBox launched as an ordinary MicroPython child in Plan2001 QEMU

- **PASS:** Boot executable is the native MicroPython ELF at OID 0x4f45584543000003, not a BusyBox smoke harness.
- **PASS:** The normal three-applet BusyBox ELF is separately installed at GE/O OID 0x4f45584543000005 with SHA-256 verification.
- **PASS:** MicroPython space.spawn(oid, argv=..., vhandles=[(region, 256)]) creates a new process with an explicit writable output capability; plan9.process_wait(process_handle, 30000) observes normal exit.
- **PASS:** A separate native ls process reads an explicitly delegated RO directory-name snapshot (Vread) and writes to a separate output region (Vwrite). Parent verifies the complete ls listing before printing EXTERNAL-LS-PASS. This is a real guest SPAWN/ProcessWait, not a stub; it is NOT yet the F5 shell's external ls integration.
- **PASS:** Original BusyBox echo.c publishes stdout into an explicitly delegated writable region; the MicroPython parent reads it RO and forwards it through its authorized console. True returns success and false returns failure status.
- **PASS:** Two separate native SPAWN applets (uxpipe-send and uxpipe-recv) exchange 10,000 bytes across two GE/O-independent capability regions, using REGIONWAIT/WAKE, release/acquire cursors and an explicit startup rendezvous. The bootstrap process verifies producer/consumer sequence counters, ready and EOF bits.
- **PASS:** MicroPython init.run() returns zero after all child-process assertions.
- **LIMITATION:** Original BusyBox true/false/echo, one Plan2001-specific ls applet, two Plan2001-specific uxpipe applets. This is NOT upstream BusyBox ash, a complete POSIX fd layer, or an interactive concurrent shell pipeline.
- **NOT RUN:** Physical hardware, full shell, full BusyBox.
EOF
echo "Plan2001 QEMU: MicroPython SPAWN/wait for native BusyBox child PASS ($log)"
