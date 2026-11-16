#!/usr/bin/env bash
# Real Plan2001 AMD64/OVMF test. Only fresh disposable files are formatted.
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo 'usage: guest_busybox_multicall.sh KERNEL MULTICALL_BUILD REPORTS' >&2
  exit 2
fi
KERNEL=$(realpath "$1")
BUILD=$(realpath "$2")
REPORTS=$(mkdir -p "$3" && realpath "$3")
OUT="$REPORTS/qemu-busybox-multicall"
[ ! -e "$OUT" ] || { echo "refusing existing output: $OUT" >&2; exit 1; }
mkdir -p "$OUT"
export QEMU_O_BOOT_LIB=1
source "$KERNEL/tools/qemu-o-boot.sh"
QOB_OUT="$OUT"
QOB_ACCEL=kvm:tcg
QOB_TIMEOUT=600
QOB_NO_REPL=1
QOB_INI_BOOTEXEC=0x4f45584543000003
QOB_BUILD=never
qob_prepare_outdir
qob_preflight
trap qob_cleanup EXIT
python3 "$KERNEL/tools/host/oseed.py" \
  --source "$KERNEL/tools/host/odemo.py" --seed "$OUT/seed" \
  --mp-exec "$BUILD/guest-probe.elf" --mpy-cross "$QOB_MPYCROSS" \
  > "$OUT/seed.log" 2>&1
python3 "$KERNEL/tools/host/object_format.py" pack "$BUILD/busybox.elf" "$OUT/seed/rawexec" --media-type application/x-elf
printf '4f45584543000005' > "$OUT/seed/rawexecoid"
python3 "$(dirname "$0")/seed_project_root.py" "$KERNEL" "$OUT/seed"
# Whole-object GE/O updates amplify repeated writes; keep the complete
# command, cwd and composition suite on a fresh 2 GiB scratch volume.
"$QOB_FORMAT" --seed "$OUT/seed" --size 2049 "$OUT/o.img" > "$OUT/format.log" 2>&1
python3 "$KERNEL/tools/oinspect.py" "$OUT/o.img" > "$OUT/oinspect.json"
python3 - "$BUILD" "$OUT/oinspect.json" "$KERNEL/tools/host" <<'PY'
import hashlib, json, pathlib, sys
sys.path.insert(0,sys.argv[3])
from object_format import frame_file
build = pathlib.Path(sys.argv[1])
objects = json.loads(pathlib.Path(sys.argv[2]).read_text())['objects']
for oid, name in [('4f45584543000003', 'guest-probe.elf'),
                  ('4f45584543000005', 'busybox.elf')]:
    data = frame_file(build / name,"application/x-elf")
    assert objects[oid]['bytes'] == len(data)
    assert objects[oid]['sha256'] == hashlib.sha256(data).hexdigest()
print('PASS: exact native driver and BusyBox bytes in fresh GE/O image')
PY
puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
qob_make_disk "$OUT/o.img" "$OUT/disk.img" "$OUT/esp.img" "$puuid" "$OUT/disk.layout"
if ! qob_boot_once "$OUT/disk.img" multicall PLAN2001-MULTICALL-PASS norepl "" ""; then
  echo "FAIL: native multicall guest: $QOB_BOOT_REASON" >&2
  tail -80 "$OUT/boot-multicall.log" >&2 || true
  printf '{"guest_execution":"FAIL","hardware":"NOT RUN","arm64":"NOT RUN"}\n' > "$OUT/result.json"
  exit 1
fi
python3 - "$BUILD" "$OUT" <<'PY'
import hashlib, json, pathlib, sys
build, out = map(pathlib.Path, sys.argv[1:])
manifest = json.loads((build / 'manifest.json').read_text())
manifest['guest_execution'] = 'PASS'
manifest['evidence'] = str(out / 'boot-multicall.log')
manifest['applets'] = json.loads((build / 'applets.json').read_text())
for applet in manifest['applets']['applets']:
    if applet['enabled']:
        applet['status'] = 'working & guest-tested'
        applet['verification'] = [manifest['evidence']]
(out / 'applets.json').write_text(json.dumps(manifest['applets'], indent=2) + '\n')
(out / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY
printf 'PASS: native AMD64 BusyBox multicall guest\nNOT RUN: physical hardware, ARM64\n'
