# GCC 14 kernel, Clang 19/LLD loader and GCC 14 MicroPython

Plan 2001-kernel builds its current full-system AMD64 kernel and MicroPython with GCC 14, and
its UEFI loader with Clang 19/LLD. MicroPython is **not** embedded in the
kernel: it is an independent freestanding ELF stored as a normal O object and
loaded through native `OEXECUTE`.

## Components

| Component | Toolchain | Output | Role |
| --- | --- | --- | --- |
| Kernel | GCC 14 | `build/gcc/9pc64.elf` | ELF64 `ET_EXEC`; native GE/O syscalls and O-server bootstrap; no `mpelf` payload |
| EFI loader | Clang 19 / LLD | `build/gcc/bootx64.efi` | PE32+ EFI application; reads `plan9.ini` and the kernel from its ESP directory |
| MicroPython | GCC 14 | `build/gcc/micropython.elf` | freestanding ELF64 `ET_EXEC`, stored in GE/O at OID `0x4f45584543000003` |
| Formatter/seeder | GCC 14, Linux host | `build/format-o/format-o` | real `ge/format/ream.c::reamo()` via `tools/mkovol` |
| `.mpy` compiler | GCC 14, Linux host | `build/mpy-cross/mpy-cross` | same-version MicroPython compiler for the seed |

The loader is intentionally built with the approved Clang/LLD COFF toolchain;
the kernel and MicroPython use GCC. These are separate builds; changing the
kernel does not rebuild the loader or the interpreter.

The experimental AArch64 boot kernel and its `BOOTAA64.EFI` loader are
different artifacts with their own Clang/LLD manifests. They are not part of
the amd64 build above and are described in
[ARCHITECTURE-SUPPORT](ARCHITECTURE-SUPPORT.md).

## Build

Prerequisites: GCC 14, Clang 19 and LLD (`clang-19`, `lld-link-19`), Python 3,
GNU make, glibc and `readelf`/`objdump`. No 9front VM, Ken C `6c`/`6l` or
Plan 9 `mk` is needed.

```sh
# native Linux host formatter + same-version mpy-cross
WHAT=kernel tools/build.sh

# independent GCC kernel
tools/build-gcc.sh --kernel -j 24

# independent Clang/LLD EFI loader
tools/build-efi-gcc.sh -j 24

# independent GCC MicroPython ELF
tools/build-mpy-gcc.sh -j 24
```

`tools/build-gcc.sh` checks the ELF class/type and rejects a kernel containing
`mpelf`/`mpelf_end`. `tools/build-efi-gcc.sh` checks the PE32+ EFI subsystem.
The current build outputs and their hashes are recorded in the build logs under
`build/`; they are not duplicated as stale constants here.

## O volume: seed and ream

The canonical seed is produced by `tools/mkovol` with the Linux GCC
formatter and Linux `mpy-cross`. `tools/host/oseed.py` hashes the source
bytes, compiles `tools/host/odemo.py` with the runtime-matching bytecode
configuration, writes the Environment manifest and seed directory, and
stores the MicroPython ELF byte for byte. The reamer itself writes the
flat `init -> SpaceID 1` catalog, arenas, logs, B-tree and both
superblocks. The OID layout is:

```text
OID_ENV     = 0xfffffffffffffffc   Environment manifest
OID_CODE    = 0xffffffff00000000   base of host-assigned code OIDs
OID_MP_EXEC = 0x4f45584543000003   GCC MicroPython ELF
```

`tools/gcc-ovol-ream.sh` is the operational wrapper:

```sh
# fresh image, exact p5 logical size (15276 MiB = 16,018,046,976 bytes)
tools/gcc-ovol-ream.sh --image build/fresh-p5.img 15276

# read-only preflight
tools/gcc-ovol-ream.sh --plan --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid> --image build/fresh-p5.img

# authorized partition-only write, fsync, full readback, read-only inspect
sudo -n tools/gcc-ovol-ream.sh --install --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid> --image build/fresh-p5.img
```

The formatter is the repository's real `reamo()` path built by GCC, not a
Linux header writer and not a simplified Python image writer. It writes only
a caller-selected regular image; installing that image on partition 5 remains
the separate hardened `--install` path. The GPT, ESP, other partitions,
firmware and old demo are untouched. Reaming is a separate operation from
installing the ESP files.

After reaming, install the kernel, loader and `plan9.ini` with
`tools/gcc-o-install.sh`; see [STATUS](STATUS.md) and
[GCC-HW-BOOT](GCC-HW-BOOT.md).

## `plan9.ini`

The canonical server configuration is:

```ini
bootfile=/EFI/plan2001-gcc/9pc64.elf
oboot=server
obootvol=PARTUUID=<live-p5-partuuid>
obootrepl=1
*ncpu=1
```

The final byte must be LF. `tools/gcc-o-install.sh` writes the file directly,
never through `$(...)`, and verifies the staged and installed bytes including
the final `0x0a`. The EFI loader reports `plan9.ini: missing final newline` and
stops if a nonempty last line lacks LF; it does not silently omit the last
setting or fall back to the default CPU count.

## Hardware status

The N10.1 GCC kernel and Clang UEFI loader were booted on the physical Dell
Precision 7520 from the single `Plan2001-O` GRUB entry and reached an
interactive `>>>` MicroPython REPL. The earlier pre-cleanup binary also
demonstrated PS/2 keyboard input, `dir()`, `import space`, `space.info()`
and real GE/O commits.

N11.2 reduced the inherited `runtime` sources to the live kernel,
MicroPython and formatter members and rebuilt both host tools from explicit
manifests. The kernel, MicroPython ELF and disposable 129 MiB O v1 image
remain byte-identical to N11.1. `oinspect` accepts both superblocks, arenas,
logs, the flat catalog, the Environment and the exact GCC MicroPython ELF.
One bounded QEMU O-server boot of that image reached `OVOLOPEN`,
`OEXECUTE`, `init.run()`, a GE/O commit, `PLAN2001-O: REPL READY` and the
`>>>` prompt. Cross-reboot persistence of a newly assigned REPL variable
has **not** yet been documented.

## Limits

- No embedded interpreter and no `/boot/mp` path in the canonical kernel.
- No general executable-object/program catalog: the fixed MicroPython OID is the
  bounded production path.
- The native formatter writes only a caller-selected regular image; the
  separate hardened installer writes a verified partition. There is no
  direct-to-`/dev` formatter mode.
- The host formatter uses a small Plan 9 C compatibility layer for
  `ge/format`; the kernel and MicroPython still compile the Plan 9-origin C
  runtime from source with GCC, not with Ken C.
- The repository retains no test harness, smoke script or debug boot selector.
