#!/bin/bash
# Build the native Linux host preparation tools:
#
#   build/format-o/format-o   the real GE/O v1 reamer as a Linux process
#   build/mpy-cross/mpy-cross same-version MicroPython .mpy compiler
#
# These are host tools, not Plan2001 targets: they use the host GCC and
# glibc.  The canonical kernel, EFI loader and MicroPython ELF are built
# separately by tools/build-gcc.sh, tools/build-efi-gcc.sh and
# tools/build-mpy-gcc.sh.  WHAT=kernel is accepted as a historical alias;
# it does not change this build and never starts a VM.
set -euo pipefail
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${WHAT:-}" in
	''|kernel|format|host) ;;
	*) echo "build.sh: unknown WHAT=${WHAT:-}" >&2; exit 2 ;;
esac
"$here/build-format-o.sh"
"$here/build-mpy-cross.sh"
