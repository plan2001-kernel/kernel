#!/usr/bin/env bash
# Three independent GE/O MicroPython boot roots plus the kernel-owned
# SystemConsole command prompt.  Uses only disposable images, a virtual PS/2
# keyboard and real VGA screendumps; no serial REPL shortcut.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
QOB_OUT=${CONSOLE_PROMPT_OUT:-"$ROOT/build/console-prompt/run"}
QOB_TIMEOUT=240
QOB_ACCEL=${QOB_ACCEL:-kvm:tcg}
QOB_CPUS=2
QOB_INI_NCPU=2
QOB_MIB=512
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
printf 'bootmanifest=/processes.conf\n' >> "$QOB_OUT/plan9.ini"
cat > "$QOB_OUT/processes.conf" <<EOF
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
EOF
qob_make_esp "$QOB_OUT/plan9.ini" "$QOB_OUT/esp.img"
mcopy -i "$QOB_OUT/esp.img" "$QOB_OUT/processes.conf" ::/processes.conf
qob_make_disk "$QOB_OUT/o.img" "$QOB_OUT/disk.img" \
 "$QOB_OUT/esp.img" "$puuid" "$QOB_OUT/disk.layout"
cp "$QOB_OVMF_VARS" "$QOB_OUT/vars.fd"

mon="$QOB_OUT/monitor.sock"
serial="$QOB_OUT/serial-console.log"
: > "$serial"
rm -f "$mon"
"$QOB_QEMU" -name plan2001-console-prompt-test -nodefaults \
  -m "$QOB_MEM" -smp "$QOB_CPUS" -machine "q35,accel=$QOB_ACCEL" -cpu max \
  -drive "if=pflash,format=raw,readonly=on,file=$QOB_OVMF_CODE" \
  -drive "if=pflash,format=raw,file=$QOB_OUT/vars.fd" \
  -drive "file=$QOB_OUT/disk.img,format=raw,if=none,id=d0" \
  -device "virtio-blk-pci-non-transitional,drive=d0,bootindex=0" \
  -device VGA -nic none -display none \
  -serial "file:$serial" -monitor "unix:$mon,server=on,wait=off" \
  -no-reboot > "$QOB_OUT/qemu.log" 2>&1 &
QOB_CURRENT_PID=$!
timeout 240 python3 "$ROOT/tools/host/bootmanifest/console-prompt-accept.py" \
 "$mon" "$serial" "$QOB_OUT/screens" "$ROOT/kernel/core/fbconsolefont.h"
