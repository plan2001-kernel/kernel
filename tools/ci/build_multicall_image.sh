#!/usr/bin/env bash
# Build, test and bundle the native multicall profile; regular files only.
set -euo pipefail
[ "$#" -eq 5 ] || { echo 'usage: build_multicall_image.sh KERNEL BUSYBOX NEWLIB_SOURCE REPORTS FRESH_OUTPUT' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1")
BUSYBOX=$(realpath "$2")
NEWLIB=$(realpath "$3")
REPORTS=$(realpath "$4")
OUT=$(realpath -m "$5")
[ ! -e "$OUT" ] || { echo 'Output must be fresh' >&2; exit 2; }
case "$OUT" in /dev/*|/sys/*|/proc/*) echo 'Regular workspace output required' >&2; exit 2;; esac
mkdir -p "$OUT"
python3 "$ROOT/tools/ci/newlib_full_link.py" --kernel "$KERNEL" --reports "$REPORTS"
# The older target still supplies the native supervisor, not the command ELF.
python3 "$ROOT/tools/ci/build_busybox_target.py" --kernel "$KERNEL" --busybox "$BUSYBOX" --reports "$REPORTS"
python3 "$ROOT/tools/ci/build_busybox_multicall.py" --kernel "$KERNEL" --busybox "$BUSYBOX" \
  --newlib-source "$NEWLIB" --reports "$REPORTS"
bash "$ROOT/tools/ci/guest_busybox_multicall.sh" "$KERNEL" "$REPORTS/busybox-multicall" "$OUT/native"
python3 "$ROOT/tools/ci/seed_busybox_spawn.py" --kernel "$KERNEL" \
  --elf "$REPORTS/busybox-target/unix-supervisor-f5-plan2001.elf" \
  --extra-elf "$REPORTS/busybox-multicall/busybox.elf" \
  --init "$ROOT/tests/guest/busybox_spawn_init.py" --reports "$OUT" --image "$OUT/multicall-o.img"
bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/multicall-o.img" "$OUT" multicall
mkdir -p "$OUT/eof-console"
bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/multicall-o.img" "$OUT/eof-console" multicall-eof
mkdir -p "$OUT/essentials-console"
bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/multicall-o.img" "$OUT/essentials-console" multicall-essentials
mkdir -p "$OUT/pipeline-console"
bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/multicall-o.img" "$OUT/pipeline-console" multicall-pipeline
AHCI_STATUS=PASS
if ! UX_HW_CONTROLLER=ahci bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/multicall-o.img" "$OUT" multicall; then
  AHCI_STATUS=FAIL
  echo 'FAIL: AHCI compatibility check; virtio image remains separately verified.' >&2
fi
printf '{"virtio":"PASS","ahci":"%s","hardware":"NOT RUN","arm64":"NOT RUN"}\n' "$AHCI_STATUS" > "$OUT/verification.json"
bash "$ROOT/tools/ci/package_multicall_image.sh" "$KERNEL" "$BUSYBOX" "$NEWLIB" "$REPORTS" "$OUT"
