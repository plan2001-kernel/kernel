# mpy-cross front end provenance

This directory contains the MicroPython `mpy-cross` compiler front end
from release **v1.27.0**, included because this repository's MicroPython
source tree did not include a host compiler.

- Release: `v1.27.0`
- Release tarball:
  `micropython-1.27.0.tar.xz`, SHA-256
  `9874b20646c3bbe81d524f779a16875e5d088b7065e175ffd2aa2a02f50573c9`
- Imported: `mpy-cross/main.c`, `Makefile`, `mpconfigport.h`,
  `mphalport.h`, `qstrdefsport.h`, `gccollect.c`, `README.md`.

The Python `mpy-cross/mpy_cross/` convenience wrapper is not imported:
`build-mpy-cross.sh` invokes the compiled binary directly. The front end
is compiled against this repository's `micropython/py` sources, not a
duplicate upstream tree, so the emitted bytecode uses the same compiler
code and Plan2001 compiler changes as the resident runtime.

License: MIT, as in [`../LICENSE`](../LICENSE) and the included headers.
