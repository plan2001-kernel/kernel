#!/usr/bin/env bash
# Build a real upstream Newlib-4.6.0 libc.a + libm.a for native AMD64.
# This is isolated from Plan2001 libc-user.a; no host libc is archived.
set -euo pipefail
if [ "$#" -ne 2 ]; then
  echo "usage: build_newlib_libc.sh OUT_SOURCE_DIR REPORT_DIR" >&2
  exit 2
fi
SRC=$(realpath -m "$1")
REPORTS=$(realpath -m "$2")
COMMIT=8ba4275b83ec27529f67e0d477611fa6d8d6e6bd
mkdir -p "$REPORTS" "$SRC"
LOG="$REPORTS/newlib-full-build.log"
OUT="$REPORTS/newlib-full"
mkdir -p "$OUT"
echo "Building Newlib version: newlib-4.6.0 ($COMMIT)" >"$LOG"

# Use a sparse clone so this CI job does not need the enormous complete
# binutils/gcc/cygwin monorepository. The pinned annotated tag resolves to
# this exact commit and is verified before any compilation.
if [ ! -d "$SRC/.git" ]; then
  git clone --quiet --filter=blob:none --sparse --depth 1 \
    --branch newlib-4.6.0 https://github.com/cygwin/cygwin.git "$SRC" \
    >>"$LOG" 2>&1
fi
git -C "$SRC" sparse-checkout set newlib libgloss config include libiberty \
  >>"$LOG" 2>&1
git -C "$SRC" checkout --quiet HEAD >>"$LOG" 2>&1
actual=$(git -C "$SRC" rev-parse HEAD)
test "$actual" = "$COMMIT" || {
  echo "incorrect source commit $actual, expected $COMMIT" >&2
  exit 1
}
echo "Pinned Newlib commit verified: $actual"
test -x "$SRC/newlib/configure" || { echo "Newlib configure absent" >&2; exit 1; }

# Configure Newlib itself rather than the GCC/binutils super-project. This
# prevents accidental linking of Linux libc into the target. Explicitly
# select the non-hosted AMD64 ELF target, serial Newlib and external syscalls.
BUILD="$OUT/build"
PREFIX="$OUT/install"
mkdir -p "$BUILD" "$PREFIX"
(
  cd "$BUILD"
  CC=gcc-14 CXX=g++-14 AR=ar RANLIB=ranlib AS=as LD=ld NM=nm \
  CFLAGS="-O2 -g0 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin" \
  "$SRC/newlib/configure" --build=x86_64-pc-linux-gnu \
    --host=x86_64-unknown-elf \
    --prefix="$PREFIX" \
    --disable-multilib --disable-newlib-supplied-syscalls \
    --disable-newlib-multithread --enable-newlib-io-long-long \
    --enable-newlib-io-c99-formats \
    --disable-newlib-nano-malloc
) >>"$LOG" 2>&1 || { tail -100 "$LOG" >&2; exit 1; }

make -C "$BUILD" -j 4 >>"$LOG" 2>&1 || {
  tail -120 "$LOG" >&2; exit 1;
}
make -C "$BUILD" install >>"$LOG" 2>&1 || {
  tail -120 "$LOG" >&2; exit 1;
}
LIBC=$(find "$PREFIX" -type f -name libc.a -print -quit)
LIBM=$(find "$PREFIX" -type f -name libm.a -print -quit)
INCLUDE=$(find "$PREFIX" -type f -path '*/include/stdio.h' -print -quit)
test -n "$LIBC" && test -n "$LIBM" && test -n "$INCLUDE" || {
  echo "Newlib install has no libc.a/libm.a/stdio.h" >&2
  find "$PREFIX" -maxdepth 5 -type f | head -60 >&2
  exit 1
}
INC_DIR=$(dirname "$INCLUDE")
mkdir -p "$REPORTS/newlib-full"
cp "$LIBC" "$REPORTS/newlib-full/libc.a"
cp "$LIBM" "$REPORTS/newlib-full/libm.a"
# Link may need the actual generated target headers; copy whole public include.
cp -a "$INC_DIR" "$REPORTS/newlib-full/include"
nm --defined-only "$LIBC" > "$REPORTS/newlib-full/libc.symbols"
for sym in ' printf$' ' malloc$' ' free$' ' _impure_ptr$' ' snprintf$'; do
  if ! grep -Eq "$sym" "$REPORTS/newlib-full/libc.symbols"; then
    echo "Newlib libc.a missing required symbol: $sym" >&2
    exit 1
  fi
done
ar t "$LIBC" >"$REPORTS/newlib-full/libc.members"
ar t "$LIBM" >"$REPORTS/newlib-full/libm.members"
test "$(wc -l <"$REPORTS/newlib-full/libc.members")" -gt 100 || {
  echo "libc.a suspiciously small" >&2; exit 1;
}
( cd "$REPORTS/newlib-full" && sha256sum libc.a libm.a ) \
   >"$REPORTS/newlib-full/SHA256SUMS"
printf '### Upstream Newlib libc build\n\n- **PASS**: source commit %s.\n- **PASS**: full upstream libc.a and libm.a archived, headers installed.\n- **PASS**: stdio, malloc, reentrancy symbols audited.\n- **NOT YET RUN**: native Plan2001 link or QEMU guest execution.\n' \
  "$COMMIT" >"$REPORTS/newlib-full-build.md"
echo "Newlib full libc.a and libm.a build: PASS"
