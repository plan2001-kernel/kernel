#!/bin/sh
# Build the Plan2001 ARM64 UEFI loader (BOOTAA64.EFI) with Clang/LLD.
# Reuses boot/efi's C sources unchanged; only the AArch64 ABI bridge and
# cache/EL2 jump helpers are architecture-specific.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
jobs=1
while [ $# -gt 0 ]; do
	case "$1" in
	-j) jobs=$2; shift 2 ;;
	-j*) jobs=${1#-j}; shift ;;
	*) echo "usage: $0 [-j N]" >&2; exit 2 ;;
	esac
done
[ "$jobs" -ge 1 ] 2>/dev/null || jobs=1

clang=$(command -v clang-19 || command -v clang || true)
lld=$(command -v lld-link-19 || command -v lld-link || true)
readobj=$(command -v llvm-readobj || true)
if [ -z "$clang" ] || [ -z "$lld" ] || [ -z "$readobj" ]; then
	echo "build-efi-arm64: need clang (COFF target), lld-link and llvm-readobj" >&2
	exit 1
fi
out=$root/build/arm64
obj=$out/efi
mkdir -p "$obj"
rm -f "$obj"/*.obj "$out/bootaa64.efi" "$out/efi-report.txt"
cd "$root"

srcs="boot/efi/efi.c boot/efi/fs.c boot/efi/sub.c boot/efi/archaa64.c boot/efi/gcc/efi-gcc.c"
flags="--target=aarch64-pc-windows-msvc -DPLAN2001_EFI_GCC -DPLAN2001_GCC -fcommon -ffreestanding -fno-builtin -fno-stack-protector -fno-omit-frame-pointer -mgeneral-regs-only -mno-outline-atomics -I include/arm64 -I include -I boot/efi"
pids=""
for f in $srcs; do
	b=$(basename "$f" .c)
	"$clang" $flags -c -o "$obj/$b.obj" "$f" >"$obj/$b.log" 2>&1 &
	pids="$pids $!"
done
"$clang" --target=aarch64-pc-windows-msvc -c -o "$obj/aa64start.obj" boot/efi/gcc/aa64gcc.S >"$obj/aa64start.log" 2>&1 &
pids="$pids $!"
fail=0
for p in $pids; do wait "$p" || fail=1; done
if [ "$fail" -ne 0 ]; then
	echo "build-efi-arm64: compile FAIL (see $obj/*.log)" >&2
	exit 1
fi

"$lld" /subsystem:efi_application /entry:efimain /machine:arm64 /nodefaultlib \
	/out:"$out/bootaa64.efi" \
	"$obj/efi.obj" "$obj/fs.obj" "$obj/sub.obj" "$obj/archaa64.obj" \
	"$obj/efi-gcc.obj" "$obj/aa64start.obj" >"$out/efi-link.log" 2>&1 || {
		cat "$out/efi-link.log" >&2
		echo "build-efi-arm64: link FAIL" >&2
		exit 1
	}

"$readobj" --file-headers "$out/bootaa64.efi" >"$out/efi-headers.txt"
if ! grep -q "IMAGE_FILE_MACHINE_ARM64" "$out/efi-headers.txt"; then
	echo "build-efi-arm64: PE check FAIL (machine)" >&2
	exit 1
fi
if ! grep -q "IMAGE_SUBSYSTEM_EFI_APPLICATION" "$out/efi-headers.txt"; then
	echo "build-efi-arm64: PE check FAIL (subsystem)" >&2
	exit 1
fi
sha=$(sha256sum "$out/bootaa64.efi" | awk '{print $1}')
{
	echo "Plan2001-O ARM64 native UEFI loader build"
	echo "clang: $($clang --version | head -1)"
	echo "lld: $($lld --version | head -1)"
	echo "source: $srcs boot/efi/gcc/aa64gcc.S"
	echo "output: $out/bootaa64.efi"
	echo "sha256: $sha"
	cat "$out/efi-headers.txt"
} >"$out/efi-report.txt"
echo "build-efi-arm64: PASS $out/bootaa64.efi sha256 $sha"
