#!/usr/bin/env bash
# Boot a copy of the three-REPL console test disk with GTK and a local HMP socket.
# Usage: tools/qemu-console.sh [SOURCE_DISK [RUN_DIRECTORY]]
# QEMU monitor: socat -,echo=0 UNIX-CONNECT:<run-directory>/monitor.sock
# Monitor commands: sendkey ctrl-alt-f1 (kernel), ctrl-alt-f2 (REPL), quit.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_disk=${1:-$root/build/console-prompt/run/disk.img}
run=${2:-$root/.agent-work/qemu-console/$(date +%Y%m%d-%H%M%S)-$$}
code=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
vars=${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}
if (( $# > 2 )); then
    echo "usage: $0 [SOURCE_DISK [RUN_DIRECTORY]]" >&2
    exit 2
fi
for file in "$source_disk" "$code" "$vars"; do
    [[ -f "$file" && -r "$file" ]] || { echo "Missing readable file: $file" >&2; exit 1; }
done
[[ ! -e "$run" ]] || { echo "Run directory already exists: $run" >&2; exit 1; }
mkdir -p "$run"
run=$(cd "$run" && pwd)
cp --reflink=auto --sparse=always "$source_disk" "$run/disk.img"
cp "$vars" "$run/vars.fd"
printf 'Run directory: %s\nMonitor socket: %s/monitor.sock\nSerial log: %s/serial.log\n' "$run" "$run" "$run"
exec "${QEMU:-qemu-system-x86_64}" \
    -name plan2001-console -nodefaults \
    -machine q35,accel=kvm:tcg -cpu max -m 2048 -smp 2 \
    -drive "if=pflash,format=raw,readonly=on,file=$code" \
    -drive "if=pflash,format=raw,file=$run/vars.fd" \
    -drive "file=$run/disk.img,format=raw,if=none,id=d0" \
    -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
    -device VGA -display gtk -nic none \
    -serial "file:$run/serial.log" \
    -monitor "unix:$run/monitor.sock,server=on,wait=off" \
    -pidfile "$run/qemu.pid" -no-reboot
