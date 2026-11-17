#!/usr/bin/env bash
# Package only an exact ELF/config already verified by native and F5 tests.
set -euo pipefail
[ "$#" -eq 5 ] || { echo 'usage: package_multicall_image.sh KERNEL BUSYBOX NEWLIB_SOURCE REPORTS TESTED_OUTPUT' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(realpath "$1")
BUSYBOX=$(realpath "$2")
NEWLIB=$(realpath "$3")
REPORTS=$(realpath "$4")
OUT=$(realpath "$5")
python3 - "$REPORTS" "$OUT" "$KERNEL/tools/host" <<'PY'
import hashlib, json, pathlib, sys
reports, out = map(pathlib.Path, sys.argv[1:3])
sys.path.insert(0,sys.argv[3])
from object_format import frame_file
build = reports / 'busybox-multicall'
result = json.loads((out / 'native/qemu-busybox-multicall/result.json').read_text())
assert result['guest_execution'] == 'PASS'
verification = json.loads((out / 'verification.json').read_text())
assert verification['virtio'] == 'PASS'
assert verification['f6_roots'] == 'PASS'
assert (out / 'f6-console/qemu-busybox-f5/screens/reboot/f5-after-f6.txt').is_file()
assert verification['ahci'] in ('PASS', 'FAIL', 'NOT RUN')
assert (out / 'qemu-busybox-f5/screens/initial/ash-isolation.txt').is_file()
digest = hashlib.sha256((build / 'busybox.elf').read_bytes()).hexdigest()
assert result['artifact']['sha256'] == digest
assert result['config_sha256'] == hashlib.sha256((build / 'busybox.config').read_bytes()).hexdigest()
seed = json.loads((out / 'busybox-spawn-oinspect.json').read_text())
assert seed['objects']['4f45584543000006']['sha256'] == hashlib.sha256(frame_file(build/'busybox.elf','application/x-elf')).hexdigest()
result.pop('applets',None)
result['verification']=verification
result['evidence']='native/qemu-busybox-multicall/boot-multicall.log'
(out / 'build.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: packaged ELF/config match native driver and F5-tested GE/O image')
PY
cp "$OUT/f6-console/qemu-busybox-f5/disk.img" "$OUT/plan2001-busybox-amd64.img"
cp "$OUT/multicall-o.img" "$OUT/p5.img"
cp "$REPORTS/busybox-multicall/busybox.elf" "$OUT/busybox.elf"
cp "$OUT/seed-busybox-child/exe" "$OUT/busybox.p2o"
cp "$OUT/seed-busybox-child/mp" "$OUT/micropython.p2o"
cp "$KERNEL/tools/host/object_format.py" "$OUT/object_format.py"
cp "$ROOT/docs/OBJECT-METADATA.md" "$OUT/OBJECT-METADATA.md"
cp "$REPORTS/busybox-multicall/LICENSE.busybox" "$OUT/LICENSE.busybox"
cp "$REPORTS/busybox-multicall/busybox.config" "$OUT/busybox.config"
cp "$OUT/native/qemu-busybox-multicall/applets.json" "$OUT/applets.json"
cp "$ROOT/docs/BUSYBOX-MULTICALL.md" "$OUT/README.md"
git -C "$BUSYBOX" archive --format=tar.gz --output="$OUT/busybox-source.tar.gz" HEAD
git -C "$KERNEL" archive --format=tar.gz --output="$OUT/kernel-source.tar.gz" HEAD
python3 - "$NEWLIB" "$OUT/newlib-source.tar.gz" <<'PYARCHIVE'
import subprocess, sys
source, output = sys.argv[1:]
entries = subprocess.check_output(['git','-C',source,'ls-tree','-z','HEAD']).decode().split('\0')
# Include every top-level support/license file and all target source subtrees.
paths = [e.split('\t',1)[1] for e in entries if e.startswith('100')]
paths += ['newlib','libgloss','config','include']
subprocess.run(['git','-C',source,'archive','--format=tar.gz','--output='+output,
                'HEAD',*paths],check=True)
PYARCHIVE
tar --exclude=__pycache__ -czf "$OUT/userland-source.tar.gz" -C "$ROOT" \
  ports config tools/ci tests docs AGENTS.md README.md
(cd "$OUT" && sha256sum plan2001-busybox-amd64.img p5.img busybox.elf busybox.p2o micropython.p2o object_format.py busybox.config \
  applets.json build.json verification.json *source.tar.gz > SHA256SUMS)
echo "PASS: virtio image and corresponding sources: $OUT"
echo 'NOT RUN: physical hardware, ARM64; consult verification.json for AHCI status.'
