#!/bin/sh
# N9.6: Linux-hosted GCC build of the real Plan2001-O MicroPython ELF.
#   tools/build-mpy-gcc.sh [-j N] [--reuse-headers] [--no-incremental]
exec python3 "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/host/build-mpy-gcc.py" "$@"
