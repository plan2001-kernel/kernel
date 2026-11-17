#!/usr/bin/env bash
# Compile and link within the actual guest, then cold boot the same GE/O disk.
set -euo pipefail
[ "$#" -eq 3 ] || { echo 'usage: guest_tinycc.sh KERNEL BUILD FRESH_OUTPUT' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1")
BUILD=$(realpath "$2")
OUT=$(realpath -m "$3")
[ ! -e "$OUT" ] || { echo 'refusing existing output' >&2; exit 2; }
mkdir -p "$OUT"
export QEMU_O_BOOT_LIB=1
source "$KERNEL/tools/qemu-o-boot.sh"
QOB_OUT="$OUT"
QOB_ACCEL=kvm:tcg
QOB_TIMEOUT=900
QOB_NO_REPL=1
QOB_INI_BOOTEXEC=0x4f4558454300000b
QOB_BUILD=never
qob_prepare_outdir
qob_preflight
trap qob_cleanup EXIT
python3 "$KERNEL/tools/host/oseed.py" --source "$ROOT/tests/guest/busybox_spawn_init.py" \
  --seed "$OUT/seed" --mp-exec "$KERNEL/build/gcc/micropython.elf" --mpy-cross "$QOB_MPYCROSS" > "$OUT/seed.log" 2>&1
python3 - "$ROOT/tools/ci" "$OUT/seed" "$KERNEL" "$BUILD" <<'PY'
import sys
sys.path.insert(0,sys.argv[1])
from seed_project_root import provision
from seed_toolchain import provision as toolchain
provision(sys.argv[2],sys.argv[3],second_root=True)
toolchain(sys.argv[2],sys.argv[3],sys.argv[4])
PY
"$QOB_FORMAT" --seed "$OUT/seed" --size 2049 "$OUT/o.img" > "$OUT/format.log" 2>&1
python3 "$KERNEL/tools/oinspect.py" "$OUT/o.img" > "$OUT/oinspect.json"
puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
qob_make_disk "$OUT/o.img" "$OUT/disk.img" "$OUT/esp.img" "$puuid" "$OUT/disk.layout"
for phase in initial reboot; do
  if ! qob_boot_once "$OUT/disk.img" "tinycc-$phase" PLAN2001-TINYCC-PASS norepl "" ""; then
    tail -60 "$OUT/boot-tinycc-$phase.log" >&2
    exit 1
  fi
done
grep -q 'admitted snapshot executable survived cold reboot' "$OUT/boot-tinycc-reboot.log"
grep -q 'guest-linked ELF survived cold reboot' "$OUT/boot-tinycc-reboot.log"
printf '{"guest_compilation":"PASS","guest_static_link":"PASS","cold_reboot":"PASS","generated_elf_execution":"PASS","scoped_capability_denial":"PASS","hardware":"NOT RUN","arm64":"NOT RUN"}\n' > "$OUT/result.json"
# Preserve the actual guest mutations when creating the interactive F6 image.
source "$OUT/disk.layout"
dd if="$OUT/disk.img" of="$OUT/installed-o.img" bs=512 skip="$o_start_lba" count="$o_sectors" conv=sparse status=none
echo "PASS: installed TinyCC/Newlib, guest compilation/static link and cold reboot: $OUT"
