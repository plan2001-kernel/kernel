#!/bin/bash
# Build the Linux host GE/O v1 formatter with GCC.
#
# The formatter is the repository's real O reamer: ge/format/ream.c and
# the GE arena, log, B-tree, hash, pack and load code.  Only the Plan 9
# libc/process/file glue for a host process comes from
# tools/host/ge-compat; there is no gefs 9P server, FS root, adm tree,
# Ken C, 6c/6l, Plan 9 mk or 9front VM in this build.
#
# Output: build/format-o/format-o
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$root/build/format-o
obj=$out/obj
cc=${CC:-gcc}
mkdir -p "$obj"

CFLAGS=(
	-std=gnu11 -m64 -O2
	-fno-builtin -fno-strict-aliasing
	-ffunction-sections -fdata-sections
	-DPLAN2001_GCC -DPLAN2001_GCC_USER -DPLAN2001_HOST_FORMAT
	-include "$root/kernel/compat/plan2001-gcc.h"
	-I "$root/include/amd64" -I "$root/include"
	-I "$root/ge/format" -I "$root/runtime/libc/fmt"
)

objs=()
compile() {
	local src=$1 name=$2
	"$cc" "${CFLAGS[@]}" -c "$src" -o "$obj/$name.o"
	objs+=("$obj/$name.o")
}

for f in ream blk cache hash pack tree error load; do
	compile "$root/ge/format/$f.c" "ge_$f"
done
compile "$root/tools/host/ge-compat/avl.c" avl
compile "$root/tools/host/ge-compat/lib9compat.c" lib9compat
compile "$root/tools/host/format-o/format-core.c" format_core
# main.c is host-only: it must see the real glibc headers, not the Plan 9
# include/ directory used by the reamer sources.
"$cc" -std=gnu11 -m64 -O2 -Wall -c "$root/tools/host/format-o/main.c" \
	-o "$obj/format_main.o"
objs+=("$obj/format_main.o")
compile "$root/runtime/libc/port/rune.c" rune
compile "$root/runtime/libc/port/utflen.c" utflen
compile "$root/runtime/libc/port/ctype.c" ctype
# Explicit fmt manifest: the formatter link needs these formatted-output
# primitives and no others.  fmtquote.c needs the kernel's USED()/quote
# helpers and is not on this path.
for f in dofmt errfmt fltfmt fmt fmtfd fmtlock fmtprint fprint print \
	snprint sprint vfprint vsnprint; do
	compile "$root/runtime/libc/fmt/$f.c" "fmt_$f"
done
"$cc" -c "$root/tools/host/ge-compat/setjmp.S" -o "$obj/setjmp.o"
objs+=("$obj/setjmp.o")

"$cc" -m64 -Wl,--gc-sections -o "$out/format-o" "${objs[@]}" -lm
size=$(stat -c %s "$out/format-o")
sha=$(sha256sum "$out/format-o" | awk '{print $1}')
echo "format-o: $out/format-o ($size bytes, sha256 $sha)"
