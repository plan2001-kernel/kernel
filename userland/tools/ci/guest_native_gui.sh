#!/usr/bin/env bash
# Two cold boots of native GUI + native peers, with no Unix ELF seeded.
set -euo pipefail
[ "$#" -eq 3 ] || { echo 'usage: guest_native_gui.sh KERNEL GUI_ELF FRESH_OUTPUT' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1")
ELF=$(realpath "$2")
OUT=$(realpath -m "$3")
[ ! -e "$OUT" ] || { echo 'refusing existing output' >&2; exit 2; }
mkdir -p "$OUT"
export QEMU_O_BOOT_LIB=1
source "$KERNEL/tools/qemu-o-boot.sh"
QOB_OUT="$OUT"
QOB_BUILD=never
qob_prepare_outdir
qob_preflight
python3 "$KERNEL/tools/host/oseed.py" --source "$ROOT/tests/guest/native_gui_peer.py" \
  --seed "$OUT/seed" --mp-exec "$QOB_MPY" --mpy-cross "$QOB_MPYCROSS" > "$OUT/seed.log" 2>&1
python3 "$ROOT/tools/ci/seed_native_gui.py" --seed "$OUT/seed" --kernel "$KERNEL" --elf "$ELF"
"$QOB_FORMAT" --seed "$OUT/seed" --size 129 "$OUT/o.img" > "$OUT/format.log" 2>&1
mkdir -p "$OUT/accept"
UX_GUI_NATIVE_ONLY=1 bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/o.img" "$OUT/accept" gui
