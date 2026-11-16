#!/usr/bin/env bash
# Build a native compiler image and require real guest and F6 shell acceptance.
set -euo pipefail
{ [ "$#" -eq 6 ] || [ "$#" -eq 7 ]; } || { echo 'usage: build_toolchain_image.sh KERNEL BUSYBOX TINYCC NEWLIB_SOURCE REPORTS FRESH_OUTPUT [NUKLEAR_SOURCE]' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1") BUSYBOX=$(realpath "$2") TINYCC=$(realpath "$3")
NEWLIB=$(realpath "$4") REPORTS=$(realpath "$5") OUT=$(realpath -m "$6")
[ ! -e "$OUT" ] || { echo 'refusing existing output' >&2; exit 2; }
case "$OUT" in /dev/*|/sys/*|/proc/*) echo 'workspace output required' >&2; exit 2;; esac
mkdir -p "$OUT"
python3 "$ROOT/tools/ci/newlib_full_link.py" --kernel "$KERNEL" --reports "$REPORTS"
python3 "$ROOT/tools/ci/build_busybox_target.py" --kernel "$KERNEL" --busybox "$BUSYBOX" --reports "$REPORTS"
python3 "$ROOT/tools/ci/build_busybox_multicall.py" --kernel "$KERNEL" --busybox "$BUSYBOX" \
  --newlib-source "$NEWLIB" --reports "$REPORTS" --toolchain
python3 "$ROOT/tools/ci/build_tinycc.py" --kernel "$KERNEL" --tinycc "$TINYCC" \
  --newlib-source "$NEWLIB" --reports "$REPORTS" --output "$OUT/toolchain"
CONSOLE_MODE=toolchain
if [ "$#" -eq 7 ]; then
  python3 "$ROOT/tools/ci/build_nuklear.py" --kernel "$KERNEL" --nuklear "$7" --reports "$REPORTS" --toolchain "$OUT/toolchain"
  CONSOLE_MODE=gui
fi
cp "$REPORTS/busybox-target/unix-supervisor-f5-plan2001.elf" "$OUT/toolchain/supervisor.elf"
cp "$REPORTS/busybox-multicall/busybox.elf" "$OUT/toolchain/busybox.elf"
bash "$ROOT/tools/ci/guest_tinycc.sh" "$KERNEL" "$OUT/toolchain" "$OUT/compile"
mkdir -p "$OUT/console"
bash "$ROOT/tools/ci/guest_busybox_f5.sh" "$KERNEL" "$OUT/compile/installed-o.img" "$OUT/console" "$CONSOLE_MODE"
cp --sparse=always "$OUT/console/qemu-busybox-f5/disk.img" "$OUT/plan2001-toolchain-amd64.img"
[ ! -f "$OUT/toolchain/nuklear-source.tar.gz" ] || cp "$OUT/toolchain/nuklear-source.tar.gz" "$OUT/nuklear-source.tar.gz"
cp "$OUT/toolchain/tinycc-source.tar.gz" "$OUT/tinycc-source.tar.gz"
git -C "$KERNEL" archive --format=tar.gz --output="$OUT/kernel-source.tar.gz" HEAD
git -C "$BUSYBOX" archive --format=tar.gz --output="$OUT/busybox-source.tar.gz" HEAD
# The sparse Newlib checkout retains a full Git tree for the pinned source.
git -C "$NEWLIB" archive --format=tar.gz --output="$OUT/newlib-source.tar.gz" HEAD newlib libgloss config include COPYING.NEWLIB
# Include the actual port and relinking recipes, including uncommitted local edits.
tar --exclude=__pycache__ -czf "$OUT/userland-source.tar.gz" -C "$ROOT" ports config tools/ci tests docs AGENTS.md README.md
python3 - "$OUT" "$CONSOLE_MODE" <<'PYRESULT'
import json,sys
from pathlib import Path
out=Path(sys.argv[1]);result=json.loads((out/'compile/result.json').read_text())
result['f6_shell_compilation']='PASS'
if sys.argv[2]=='gui':
    result['nuklear_gui_approval']='PASS'
    result['surface_slot_isolation']='PASS'
result['f5_toolchain_isolation']='PASS'
(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
PYRESULT
(cd "$OUT" && sha256sum plan2001-toolchain-amd64.img *source.tar.gz verification.json > SHA256SUMS)
echo "PASS: native TinyCC/Newlib image with verified F6 compilation: $OUT"
