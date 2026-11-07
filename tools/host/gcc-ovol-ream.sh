#!/bin/bash
# N11: fresh GE/O v1 ream + seed with the native Linux host tools.
#
# The formatter is the repository's real GE/O ream
# (ge/format/ream.c::reamo) built as a Linux GCC host program by
# tools/build.sh.  tools/mkovol invokes it with the Linux mpy-cross
# seeder; no 9front VM or Ken C tool is involved.  The resulting image is
# a bare O volume of the requested logical size; it is written to a
# *partition* only.
#
#   tools/gcc-ovol-ream.sh --image OUT MIB
#       ream a new disposable image via mkovol + the current GCC MPY ELF
#       and tools/host/odemo.py; verify it with tools/oinspect.py.
#
#   tools/gcc-ovol-ream.sh --plan --device /dev/nvme0n1p5 \
#       --partuuid a91d7405-f2d0-4269-82b4-f528ac65b185 \
#       --image build/n97/fresh-p5.img
#       read-only identity/size/mount preflight; no writes.
#
#   sudo -n tools/gcc-ovol-ream.sh --install --device ... --partuuid ... \
#       --image ...
#       write the image to that partition only, fsync, read back SHA-256,
#       and inspect the device read-only.  Refuses any identity mismatch.
#
#   tools/gcc-ovol-ream.sh --verify --device ... --partuuid ... [--image ...]
#       read-only post-install inspection.
#
# The device is never guessed.  Only the named partition is written; the
# GPT, ESP, other partitions, firmware and the old demo are untouched.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mpyelf=$root/build/gcc/micropython.elf
demo=$root/tools/host/odemo.py
mkovol=$root/tools/mkovol
oinspect=$root/tools/oinspect.py
outdir=${GCC_OREAM_OUT:-$root/build/n11/ream}
mode=plan
mode_set=0
device=
puuid=
image=
mib=

die() { printf 'gcc-ovol-ream: %s\n' "$*" >&2; exit 1; }
say() { printf 'gcc-ovol-ream: %s\n' "$*"; }
usage() {
	cat >&2 <<'EOF'
usage:
  tools/gcc-ovol-ream.sh --image OUT MIB
  tools/gcc-ovol-ream.sh --image-check --image IMG
  tools/gcc-ovol-ream.sh --plan|--install|--verify \
      --device /dev/... --partuuid <LIVE-PARTUUID> [--image IMG]
  --install must be run with noninteractive sudo (sudo -n).
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--plan) mode=plan; mode_set=1 ;;
	--install) mode=install; mode_set=1 ;;
	--verify) mode=verify; mode_set=1 ;;
	--image-check) mode=image-check; mode_set=1 ;;
	--image)
		[ $# -ge 2 ] || die "--image needs a path"
		image=$2; shift
		if [ $# -ge 2 ] && [[ "${2:-}" =~ ^[0-9]+$ ]]; then
			mib=$2; shift
			[ "$mode_set" = 1 ] || mode=image
		fi ;;
	--device)
		[ $# -ge 2 ] || die "--device needs a path"
		device=$2; shift ;;
	--partuuid)
		[ $# -ge 2 ] || die "--partuuid needs a value"
		puuid=$2; shift ;;
	--mib)
		[ $# -ge 2 ] || die "--mib needs a number"
		mib=$2; shift ;;
	-h|--help) usage; exit 0 ;;
	*) usage; exit 2 ;;
	esac
	shift
done

need() { [ -e "$1" ] || die "missing $1"; }
check_mpy() {
	need "$mpyelf"; need "$demo"; need "$mkovol"; need "$oinspect"
	mpy_size=$(stat -c %s "$mpyelf")
	mpy_sha=$(sha256sum "$mpyelf" | awk '{print $1}')
	say "GCC MPY ELF: $mpyelf ($mpy_size bytes, $mpy_sha)"
}

inspect_image() {
	local img=$1 json=$2 tmp extract
	tmp=$(mktemp -d)
	extract=$tmp/objects
	python3 "$oinspect" "$img" --extract "$extract" >"$json" 2>"$json.err" || {
		cat "$json.err" >&2
		die "oinspect failed on $img"
	}
	python3 - "$json" "$extract" "$mpy_size" "$mpy_sha" "$demo" \
		"$root/tools/host" <<'PY'
import hashlib, json, pathlib, sys
sys.path.insert(0, sys.argv[6])
import oseed
d = json.load(open(sys.argv[1]))
extract = pathlib.Path(sys.argv[2])
want_len = int(sys.argv[3]); want = sys.argv[4]
demo = pathlib.Path(sys.argv[5])
o = d['objects'].get('4f45584543000003')
if not o:
    sys.exit("OID 0x4f45584543000003 missing")
if o['bytes'] != want_len or o['sha256'] != want:
    sys.exit("MPY OID mismatch: %r" % (o,))
env = d['objects'].get('fffffffffffffffc')
code = d['objects'].get('ffffffff00000000')
if not env or not code:
    sys.exit("Environment or init code object missing")
sbs = d['superblocks']
if len(sbs) != 2 or not all(s['hash']['ok'] for s in sbs):
    sys.exit("primary/backup superblock validation failed")
if any(s['flag'] != (1 << 63 | 1) for s in sbs):
    sys.exit("not an O v1 volume")
env_bytes = (extract / 'fffffffffffffffc.bin').read_bytes()
code_bytes = (extract / 'ffffffff00000000.bin').read_bytes()
if hashlib.sha256(env_bytes).hexdigest() != env['sha256']:
    sys.exit("extracted Environment digest mismatch")
if hashlib.sha256(code_bytes).hexdigest() != code['sha256']:
    sys.exit("extracted code digest mismatch")
lines = env_bytes.decode('ascii').splitlines()
if len(lines) != 2 or lines[0] != 'runtime ' + oseed.runtime_id():
    sys.exit("Environment runtime is not the current MicroPython/MPY value")
f = lines[1].split(' ')
if len(f) != 4 or f[0] != 'odemo':
    sys.exit("Environment module line malformed: %r" % (lines[1],))
if f[1] != hashlib.sha256(demo.read_bytes()).hexdigest():
    sys.exit("Environment definition is not the demo source digest")
if f[2] != hashlib.sha256(code_bytes).hexdigest():
    sys.exit(".mpy digest does not match the stored code object")
if f[3] != 'ffffffff00000000':
    sys.exit("initial code OID changed")
want_env = ("runtime %s\n%s %s %s %s\n" % (
    oseed.runtime_id(), 'odemo', f[1], f[2], f[3])).encode('ascii')
if env_bytes != want_env:
    sys.exit("Environment bytes are not canonical")
if len(code_bytes) < 4 or code_bytes[0] != ord('M'):
    sys.exit("stored code object has no .mpy magic")
if code_bytes[1] != oseed.mpy_version():
    sys.exit("stored .mpy version %d, expected %d"
             % (code_bytes[1], oseed.mpy_version()))
if ((code_bytes[2] >> 2) & 0x2F) != 0 or code_bytes[2] & 0x40:
    sys.exit("stored .mpy contains native code")
print("inspect: space=%s" % (d['space'],))
print("inspect: MPY=%d %s" % (o['bytes'], o['sha256']))
print("inspect: Environment=%d %s" % (env['bytes'], env['sha256']))
print("inspect: code=%d %s" % (code['bytes'], code['sha256']))
print("inspect: runtime=%s .mpy v%d" % (oseed.runtime_id(), oseed.mpy_version()))
print("inspect: superblocks primary+backup OK, O v1")
PY
	rm -rf "$tmp"
}

device_identity() {
	[ -n "$device" ] || die "--device is required for $mode"
	[ -n "$puuid" ] || die "--partuuid is required for $mode"
	[ -e "$device" ] || die "device $device does not exist"
	dev=$(readlink -f "$device")
	[ -b "$dev" ] || die "$device is not a block device"
	base=$(basename "$dev")
	sys=/sys/class/block/$base
	[ -r "$sys/partition" ] || die "$dev is not a partition"
	partn=$(cat "$sys/partition")
	[ "$partn" = 5 ] || die "$dev is partition $partn, expected the experimental p5"
	byuuid=$(readlink -f "/dev/disk/by-partuuid/$puuid" 2>/dev/null || true)
	[ "$byuuid" = "$dev" ] || die "PARTUUID $puuid does not resolve to $dev (got ${byuuid:-none})"
	dev_size=$(( $(cat "$sys/size") * 512 ))
	[ "$dev_size" -gt 0 ] || die "cannot read $sys/size"
	if findmnt -n "$dev" >/dev/null 2>&1; then
		die "$dev is mounted; refusing to write/verify it"
	fi
	holders=$(ls -A "$sys/holders" 2>/dev/null || true)
	[ -z "$holders" ] || die "$dev has holders: $holders"
	say "device: $dev partition=$partn size=$dev_size partuuid=$puuid not-mounted=yes"
}

size_matches_image() {
	[ -n "$image" ] || die "--image is required for $mode"
	[ -f "$image" ] || die "missing image $image"
	img_size=$(stat -c %s "$image")
	[ "$dev_size" = "$img_size" ] || \
		die "partition size $dev_size != image size $img_size"
	say "image: $image ($img_size bytes, sha256 $(sha256sum "$image" | awk '{print $1}'))"
}

mkdir -p "$outdir"
check_mpy

case "$mode" in
image)
	[ -n "$image" ] || die "--image needs an output path"
	[ -n "$mib" ] || die "--image needs MIB as its second argument: --image OUT MIB"
	[ "$mib" -ge 129 ] || die "MIB must be at least 129"
	say "reaming $image ($mib MiB) with the native Linux GE/O formatter"
	MPEXEC=$mpyelf "$mkovol" "$image" "$mib" "$demo" 2>&1 | tee "$outdir/mkovol.log"
	[ "$(stat -c %s "$image")" = "$((mib * 1048576))" ] || \
		die "mkovol output size is not ${mib} MiB"
	inspect_image "$image" "$outdir/image-inspect.json"
	{
		printf 'method=mkovol/reamo native Linux GCC formatter\n'
		printf 'image=%s\n' "$image"
		printf 'size=%s\n' "$(stat -c %s "$image")"
		printf 'sha256=%s\n' "$(sha256sum "$image" | awk '{print $1}')"
		printf 'mpy_elf=%s\nmpy_sha256=%s\nmpy_size=%s\n' "$mpyelf" "$mpy_sha" "$mpy_size"
		printf 'demo=%s\n' "$demo"
		printf 'date=%s\n' "$(date -Is)"
	} >"$outdir/image-manifest.txt"
	cat "$outdir/image-manifest.txt"
	;;
image-check)
	[ -f "$image" ] || die "missing image $image"
	inspect_image "$image" "$outdir/check-inspect.json"
	;;
plan|verify|install)
	device_identity
	size_matches_image
	if [ "$mode" = plan ]; then
		say "PLAN ONLY: no device write"
		say "would write $image -> $dev ($dev_size bytes), then fsync and read back"
		exit 0
	fi
	if [ "$mode" = install ] || [ "$mode" = verify ]; then
		sudo -n true 2>/dev/null || die "noninteractive sudo is unavailable"
	fi
	if [ "$mode" = install ]; then
		say "INSTALL: writing $image -> $dev"
		sudo -n dd if="$image" of="$dev" bs=16M conv=fsync status=none
		sudo -n sync
	fi
	readback=$(sudo -n sha256sum "$dev" | awk '{print $1}')
	want=$(sha256sum "$image" | awk '{print $1}')
	[ "$readback" = "$want" ] || die "readback SHA-256 mismatch: $readback != $want"
	say "readback SHA-256 matches image: $readback"
	sudo -n python3 "$oinspect" "$dev" >"$outdir/device-inspect.json" \
		2>"$outdir/device-inspect.err" || {
		cat "$outdir/device-inspect.err" >&2
		die "read-only oinspect of $dev failed"
	}
	python3 - "$outdir/device-inspect.json" "$mpy_size" "$mpy_sha" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
o = d['objects'].get('4f45584543000003')
if not o or o['bytes'] != int(sys.argv[2]) or o['sha256'] != sys.argv[3]:
    sys.exit("device MPY OID mismatch: %r" % (o,))
if not all(s['hash']['ok'] for s in d['superblocks']):
    sys.exit("device superblock validation failed")
print("device: MPY OID=%d %s space=%s superblocks=OK" %
      (o['bytes'], o['sha256'], d['space']))
PY
	{
		printf 'method=partition dd of native formatter image\n'
		printf 'device=%s\n' "$dev"
		printf 'partuuid=%s\n' "$puuid"
		printf 'size=%s\n' "$dev_size"
		printf 'image=%s\n' "$image"
		printf 'image_sha256=%s\n' "$want"
		printf 'readback_sha256=%s\n' "$readback"
		printf 'date=%s\n' "$(date -Is)"
	} >"$outdir/device-manifest.txt"
	cat "$outdir/device-manifest.txt"
	;;
*) usage; exit 2 ;;
esac
