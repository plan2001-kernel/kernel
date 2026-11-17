#!/usr/bin/env bash
# Produce and QEMU-test a regular-file UEFI image. Never writes a device.
set -euo pipefail
[ "$#" -eq 4 ] || { echo 'usage: build_hw_boot.sh KERNEL BUSYBOX NEWLIB_REPORTS FRESH_OUTPUT' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1")
BUSYBOX=$(realpath "$2")
REPORTS=$(realpath "$3")
OUT=$(realpath -m "$4")
[ ! -e "$OUT" ] || { echo 'Output must be a fresh directory' >&2; exit 2; }
case "$OUT" in /dev/*|/sys/*|/proc/*) echo 'Regular workspace output required' >&2; exit 2;; esac
mkdir -p "$OUT"
python3 "$ROOT/tools/ci/newlib_full_link.py" --kernel "$KERNEL" --reports "$REPORTS"
python3 "$ROOT/tools/ci/build_ash.py" --kernel "$KERNEL" --busybox "$BUSYBOX" --reports "$REPORTS"
python3 "$ROOT/tools/ci/build_busybox_target.py" --kernel "$KERNEL" --busybox "$BUSYBOX" --reports "$REPORTS"
python3 "$ROOT/tools/ci/seed_busybox_spawn.py" --kernel "$KERNEL" \
 --elf "$REPORTS/busybox-target/unix-supervisor-f5-plan2001.elf" \
 --extra-elf "$REPORTS/ash-native/ash.elf" \
 --init "$ROOT/tests/guest/busybox_spawn_init.py" --reports "$OUT" --image "$OUT/ash-o.img"
bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/ash-o.img" "$OUT" ash
UX_HW_CONTROLLER=ahci bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/ash-o.img" "$OUT" ash
cp "$OUT/qemu-busybox-f5/disk.img" "$OUT/plan2001-ash-amd64.img"
cp "$REPORTS/ash-native/ash.elf" "$OUT/ash.elf"
cp "$REPORTS/ash-native/LICENSE.busybox" "$OUT/LICENSE.busybox"
cp "$REPORTS/ash-native/busybox.config" "$OUT/busybox.config"
cp "$ROOT/config/sources.json" "$OUT/source-lock.json"
cp "$ROOT/config/ash-sources.json" "$OUT/ash-sources.json"
cp "$REPORTS/ash-native/manifest.json" "$OUT/ash-build.json"
cp "$ROOT/docs/HARDWARE-BOOT.md" "$OUT/README.md"
git -C "$BUSYBOX" archive --format=tar.gz --output="$OUT/busybox-source.tar.gz" HEAD
git -C "$KERNEL" archive --format=tar.gz --output="$OUT/kernel-source.tar.gz" HEAD
git -C "$KERNEL" diff --binary HEAD > "$OUT/kernel-worktree.patch"
tar --exclude=__pycache__ -czf "$OUT/userland-port-source.tar.gz" -C "$ROOT" ports config tools/ci docs AGENTS.md README.md
gzip -c "$OUT/plan2001-ash-amd64.img" > "$OUT/plan2001-ash-amd64.img.gz"
(cd "$OUT" && sha256sum plan2001-ash-amd64.img plan2001-ash-amd64.img.gz ash.elf *source.tar.gz kernel-worktree.patch > SHA256SUMS)
echo "QEMU-tested hardware candidate: $OUT/plan2001-ash-amd64.img"
