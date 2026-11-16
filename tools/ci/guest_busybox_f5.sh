#!/usr/bin/env bash
# Two real Plan2001 AMD64 QEMU boots: trusted Unix supervisor at manifest F5; Unix init is SPAWNed.
# Run twice with the SAME disposable GE/O disk. The project mode verifies a shared persistent namespace across cold boots.
set -euo pipefail
if [ "$#" -ne 3 ] && [ "$#" -ne 4 ]; then
  echo "usage: guest_busybox_f5.sh KERNEL_SOURCE SEEDED_GE_O_IMAGE REPORT_DIR" >&2
  exit 2
fi
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(cd "$1" && pwd)
IMAGE=$(realpath "$2")
REPORTS=$(cd "$3" && pwd)
MODE=${4:-default}
if [ "$MODE" != toolchain ] && [ "$MODE" != f6 ] && [ "$MODE" != project ] && [ "$MODE" != default ] && [ "$MODE" != newlib-supervised ] && [ "$MODE" != ash ] && [ "$MODE" != multicall ] && [ "$MODE" != multicall-eof ] && [ "$MODE" != multicall-essentials ] && [ "$MODE" != multicall-pipeline ]; then
  echo "unsupported acceptance mode: $MODE" >&2
  exit 2
fi
OUT="$REPORTS/qemu-busybox-f5"
if [ "${UX_HW_CONTROLLER:-virtio}" = ahci ]; then OUT="$OUT-ahci"; fi
mkdir -p "$OUT"
[ -f "$IMAGE" ] || { echo "Missing seeded GE/O image" >&2; exit 1; }

export QEMU_O_BOOT_LIB=1
# shellcheck disable=SC1091
source "$KERNEL/tools/qemu-o-boot.sh"
QOB_OUT="$OUT"
QOB_ACCEL=kvm:tcg
QOB_TIMEOUT=240
QOB_NO_REPL=1
QOB_BUILD=never
QOB_CPUS=2
QOB_INI_NCPU=2
qob_prepare_outdir
qob_preflight
trap qob_cleanup EXIT

puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
# The guest keyboard injector deliberately uses the US physical keymap.
# Set it explicitly for kernels supporting selectable layouts; older builds
# already use US scancode decoding for manifest console sessions.
printf 'keymap=us\nbootmanifest=/processes.conf\n' >> "$OUT/plan9.ini"
cat > "$OUT/processes.conf" <<EOF
version=1
[process.replA]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=replA
cpu=0
args=-R
required=true
[process.replB]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=replB
cpu=1
args=-R
required=true
[process.replC]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=replC
args=-R
required=true
[process.unixInitF5]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000005
owner=unixSupervisorF5
args=0x4f45584543000006${UX_GEO_PERSIST:+ 0x4f45584543000007}
required=true
EOF
if [ "$MODE" = f6 ] || [ "$MODE" = toolchain ]; then
  cat >> "$OUT/processes.conf" <<EOF
[process.unixInitF6]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000005
owner=unixSupervisorF6
args=0x4f45584543000006 project 0x4f45584543000008
required=true
EOF
fi
if [ "${UX_AHCI_DEBUG:-0}" = 1 ]; then printf '*ahcidebug=1\n' >> "$OUT/plan9.ini"; fi
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
mcopy -i "$OUT/esp.img" "$OUT/processes.conf" ::/processes.conf
qob_make_disk "$IMAGE" "$OUT/disk.img" "$OUT/esp.img" "$puuid" "$OUT/disk.layout"

# The original disk image is built once. Between boots only the QEMU
# process and its OVMF variable overlay are reset, not the GE/O disk.
phases=(initial reboot)
if [ "$MODE" = newlib-supervised ] || [ "$MODE" = ash ] || [ "$MODE" = multicall ] || [ "$MODE" = multicall-eof ] || [ "$MODE" = multicall-essentials ] || [ "$MODE" = multicall-pipeline ]; then phases=(initial); fi
for phase in "${phases[@]}"; do
  mon="$OUT/monitor-$phase.sock"
  serial="$OUT/serial-$phase.log"
  vars="$OUT/vars-$phase.fd"
  : > "$serial"
  rm -f "$mon"
  cp "$QOB_OVMF_VARS" "$vars"
  case "${UX_HW_CONTROLLER:-virtio}" in
    virtio) disk_device=(-device virtio-blk-pci-non-transitional,drive=d0,bootindex=0) ;;
    ahci) disk_device=(-device ich9-ahci,id=ashsata -device ide-hd,drive=d0,bus=ashsata.0,bootindex=0) ;;
    *) echo 'Unsupported hardware controller' >&2; exit 2 ;;
  esac
  "$QOB_QEMU" -name "plan2001-unix-init-$phase" -nodefaults \
    -m "$QOB_MEM" -smp "$QOB_CPUS" -machine "q35,accel=$QOB_ACCEL" -cpu max \
    -drive "if=pflash,format=raw,readonly=on,file=$QOB_OVMF_CODE" \
    -drive "if=pflash,format=raw,file=$vars" \
    -drive "file=$OUT/disk.img,format=raw,if=none,id=d0" \
    "${disk_device[@]}" \
    -device VGA -nic none -display none \
    -serial "file:$serial" -monitor "unix:$mon,server=on,wait=off" \
    -no-reboot > "$OUT/qemu-$phase.log" 2>&1 &
  QOB_CURRENT_PID=$!

  case "$MODE" in
    f6|project|toolchain) accept_mode="$MODE-$phase" ;;
    default) accept_mode="${UX_GEO_PERSIST:+persist-}$phase" ;;
    *) accept_mode="$MODE" ;;
  esac
  timeout 240 python3 "$ROOT/tools/ci/f5_console_accept.py" \
    "$mon" "$serial" "$OUT/screens/$phase" \
    "$KERNEL/kernel/core/fbconsolefont.h" "$accept_mode"
  qob_cleanup
done

if [ "$MODE" = toolchain ] || [ "$MODE" = f6 ] || [ "$MODE" = project ] || [ "$MODE" = ash ] || [ "$MODE" = multicall ] || [ "$MODE" = multicall-eof ] || [ "$MODE" = multicall-essentials ] || [ "$MODE" = multicall-pipeline ]; then
  echo "PASS: native ash $MODE acceptance; bootable disk at $OUT/disk.img"
  exit 0
fi
if [ "$MODE" = newlib-supervised ]; then
  cat > "$REPORTS/newlib-supervised-guest.md" <<'EOF'
## Native Newlib process over supervisor-delegated UXPIPE (real QEMU)

- **PASS:** trusted F5 supervisor boots a Newlib-only child with six attenuated Oinheritv region Handles and one execute-only GE/O volume.
- **PASS:** public read(0) receives keyboard input forwarded by the supervisor; write(1) and write(2) publish to separate supervisor-read streams.
- **PASS:** fstat(0,1,2) identifies streams as FIFO; close(0,1,2) publishes explicit stream close/EOF.
- **PASS:** F1 console isolation and F5 VGA output on actual Plan2001 QEMU.
- **NOT RUN:** arbitrary POSIX tty, filesystem RPC, physical hardware.
EOF
  echo "Native Newlib supervised stdio/UXPIPE real QEMU test PASS"
  exit 0
fi
cat > "$REPORTS/busybox-f5-guest.md" <<'EOF'
## Native trusted supervisor -> separate Unix init -> F5 shell in real QEMU

- **PASS:** Manifest F5 slot boots a trusted userland Unix supervisor, which retains the kernel-owned NON-delegable session rings.\n- **PASS:** The supervisor creates six fresh region capabilities, delegates one RW data / RO ACK channel for each direction using Oinheritv, and sends only Vexecute to the independent Unix init child.\n- **PASS:** Kernel console capabilities are NOT inherited by the child, and no kernel VFS, Pipe or Unix API is introduced.
- **PASS:** The separate child starts Unix init, constructs a fresh volatile Unix root PVD and enters its embedded F5 evaluator.
- **PASS:** First prompt displays unix:/$; changing cwd updates the path in the prompt.
- **PASS:** Real PS/2 input and kernel VGA verify pwd, cd and a separately spawned `ls` applet, errors, root confinement, F1 isolation and F5 focus restore.
- **PASS:** First boot ends in /usr/bin. Second distinct QEMU boot reuses the exact same scratch GE/O disk, executes init again, and enters / (pwd confirms).
- **LIMITATION:** Supervisor and Unix init are separate native processes, but the F5 evaluator within init is NOT BusyBox ash/sh or a POSIX TTY.
- **NOT RUN:** Physical hardware, full POSIX tty, BusyBox ash, and crash-recovery certification.
EOF
if [ -n "${UX_GEO_PERSIST:-}" ]; then
  cat >> "$REPORTS/busybox-f5-guest.md" <<'EOF'
- **PASS:** A separate native UXRPC client and the F5 shell use the same GE/O-backed UxService and checked persistent manifest.
- **PASS:** The child writes a file, the shell reads it, and a new child and shell both read it after a second cold QEMU boot using the same GE/O disk.
- **PASS:** The child receives only four attenuated UXPIPE region grants (no volume Handle), and writes outside the normalized /tmp policy are denied.
EOF
else
  cat >> "$REPORTS/busybox-f5-guest.md" <<'EOF'
- **NOT RUN in this console-only mode:** GE/O file-service persistence; see the separate Unix GE/O persistence workflow.
EOF
fi
echo "Native unix-init / F5 shell real VGA + cold-boot reset QEMU test PASS"
