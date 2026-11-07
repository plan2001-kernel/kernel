#!/bin/sh
# Build the Plan2001 AMD64 UEFI loader as a native PE32+ EFI application
# using the installed Clang/LLD COFF toolchain.  This is independent of the
# 9front aout2efi path and does not require a kernel rebuild.
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
objdump=$(command -v objdump || true)
if [ -z "$clang" ] || [ -z "$lld" ]; then
	echo "build-efi-gcc: need clang (COFF target) and lld-link (e.g. apt install clang-19 lld-19)" >&2
	exit 1
fi
out=$root/build/gcc
obj=$out/efi
mkdir -p "$obj"
rm -f "$obj"/*.obj "$out/bootx64.efi" "$out/efi-report.txt"
cd "$root"

srcs="boot/efi/efi.c boot/efi/fs.c boot/efi/sub.c boot/efi/archx64.c boot/efi/gcc/efi-gcc.c"
flags="-DPLAN2001_EFI_GCC -DPLAN2001_GCC -fcommon -ffreestanding -fno-builtin -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone -I include/amd64 -I include -I boot/efi"
pids=""
for f in $srcs; do
	b=$(basename "$f" .c)
	"$clang" --target=x86_64-pc-windows-msvc -c -o "$obj/$b.obj" $flags "$f" >"$obj/$b.log" 2>&1 &
	pids="$pids $!"
done
"$clang" --target=x86_64-pc-windows-msvc -c -o "$obj/x64gcc.obj" boot/efi/gcc/x64gcc.S >"$obj/x64gcc.log" 2>&1 &
pids="$pids $!"
fail=0
for p in $pids; do wait "$p" || fail=1; done
if [ "$fail" -ne 0 ]; then
	echo "build-efi-gcc: compile FAIL (see $obj/*.log)" >&2
	exit 1
fi

"$lld" /subsystem:efi_application /entry:efimain /machine:x64 /nodefaultlib \
	/out:"$out/bootx64.efi" \
	"$obj/efi.obj" "$obj/fs.obj" "$obj/sub.obj" "$obj/archx64.obj" \
	"$obj/efi-gcc.obj" "$obj/x64gcc.obj" >"$out/efi-link.log" 2>&1 || {
		cat "$out/efi-link.log" >&2
		echo "build-efi-gcc: link FAIL" >&2
		exit 1
	}

if ! "$objdump" -p "$out/bootx64.efi" | grep -q "Subsystem.*0000000a.*EFI application"; then
	echo "build-efi-gcc: PE check FAIL (not an EFI application)" >&2
	exit 1
fi
if ! file "$out/bootx64.efi" | grep -q "PE32+ executable for EFI"; then
	echo "build-efi-gcc: PE check FAIL (file type)" >&2
	exit 1
fi
sha=$(sha256sum "$out/bootx64.efi" | awk '{print $1}')
{
	echo "Plan2001-O N9.4 native UEFI loader build"
	echo "clang: $($clang --version | head -1)"
	echo "lld: $($lld --version | head -1)"
	echo "source: $srcs boot/efi/gcc/x64gcc.S"
	echo "output: $out/bootx64.efi"
	echo "sha256: $sha"
	"$objdump" -p "$out/bootx64.efi" | grep -E "Magic|AddressOfEntryPoint|ImageBase|Subsystem|DLL Name" || true
} >"$out/efi-report.txt"
echo "build-efi-gcc: PASS $out/bootx64.efi sha256 $sha"
