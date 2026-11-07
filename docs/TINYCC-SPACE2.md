# Native TinyCC and real GE/O Space 2

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

TinyCC's x86-64 C compiler, assembler and ELF linker now run as a native
Plan2001 userspace executable. The VM test reads source/header objects,
assembles its startup code, compiles C, writes relocatable object files,
links a static ELF, and starts that ELF using the existing OSPAWN syscall.
All application inputs and outputs live in the real GE/O Space 2.

This is a bootstrap compiler port with a bounded C-library adapter, not a
complete hosted C development environment or a self-hosting claim.

An optional [named OID collection graph](OID-PATH-TEST.md) now provides
userspace traversal of directory-style paths. Select it with
`bash tools/qemu-tcc-space2.sh --graph`. The flat registry described below
is still the default test mode.

## Build and test

From the project root, with GCC 14, GNU ld, Python 3, QEMU, OVMF and mtools:

```sh
CC=gcc-14 tools/build-gcc.sh --kernel -j3
CC=gcc-14 tools/build-tcc.sh
CC=gcc-14 bash tools/host/build-format-o.sh
bash tools/qemu-tcc-space2.sh
```

The test also requires the normal `build/gcc/bootx64.efi` and
`build/mpy-cross/mpy-cross` build artifacts. Use `tools/build-efi-gcc.sh`
and `tools/build.sh` to build those if absent. `QEMU`, `OVMF_CODE` and
`OVMF_VARS` can select installed binaries/firmware. The test uses TCG,
disposable regular-file disks and the existing GPT/OVMF boot helpers.
No physical disk is written. Each run creates `build/tcc-space2.XXXXXX`.

The native compiler is `build/gcc/tcc.elf`. The optional test image boots it
from init-Space OID `0x4f45584543000004` using `obootexec`. Standard images
and the default MicroPython boot selector retain their existing behavior.
That OID is also used for iked in its separate optional test seed; these
seed modes are alternatives, not a shared program catalog.

## Flat names and genuine storage

The optional formatter seed creates these real GE/O catalog entries:

```text
"init" -> SpaceID 1
"/"    -> SpaceID 2
```

Slash is the exact name of the second store. It has no parent or children.
Space 2 contains ordinary opaque byte objects, including:

```text
/usr/opt/src/crt0.S
/usr/opt/src/main.c
/usr/opt/src/lib/answer.c
/usr/opt/src/include/api.h
/usr/opt/src/include/detail/value.h
/usr/opt/src/include/plan2001_syscall.h
/usr/opt/src/build/crt0.o
/usr/opt/src/build/main.o
/usr/opt/src/build/answer.o
/usr/opt/src/build/demo
```

These complete strings are names in a userspace registry at OID 64. The
registry stores `hex-OID TAB name LF`; it is a normal byte object, not a new
GE/O record type. There are no directory objects. The adapter's open/read/
seek/close functions operate on memory buffers loaded using OSPACEGET and
publish outputs using OSPACEPUT/OSPACECREATE and OSPACECOMMIT. There is no
host filesystem export, 9P service, Linux syscall layer or temporary
directory in the guest compilation path. TinyCC performs include-name
construction in userspace and opens the resulting exact names.

The formatter consumes an optional `rootspace` file listing strictly
16-digit hexadecimal OIDs followed by LF, with each payload in host seed
file `root/<OID>`. It writes the Space 2 header and records using the same
GE/O v1 layout as Space 1. `seed-tcc-demo.py` prepares the fixture and
registry. Normal seeds without `rootspace` do not create Space 2.

Existing names retain their OIDs when changed. New output objects use the
kernel's existing local OID allocator. Creation and name registration are
separate commits; interruption can leave an unnamed object. This registry
is a single-writer experiment, not a general namespace service.

## Target ABI and port boundary

TinyCC is built with GCC as a freestanding static executable, using the
same two-PT_LOAD linker layout as other native programs. The build uses
host amd64 C declaration headers and a few compiler support routines from
libgcc; it links no host libc. Syscall assembly wrappers take their numbers
from `runtime/abi/sys.h`, which is unchanged.

The test application uses a small explicit Plan2001 runtime, not Unix libc.
TinyCC assembles that runtime's `_start`, IOWRITE and EXITS code inside the
VM. The parent explicitly grants a stdout Pipe to each child. C arguments,
initialized data, zeroed BSS, output and a clean exit are checked.

Upstream changes are confined to `vendor/tcc.h` and `vendor/tccelf.c`:

- Identify the target as Plan2001 rather than Linux; disable the native
  host JIT/run machinery. Execution is through the kernel ELF loader.
- Group read-only sections with executable text and align data segments
  to pages, producing the kernel's required two-segment static ELF layout.

The compiler driver selects `-nostdlib -nostdinc -static` and a text base
of `0x200000`. Its initial-process arguments are:

```text
oexec PARTUUID=... init                       # full smoke test
tcc   PARTUUID=... init [-c] -o /output /input...
```

The explicit form compiles an object with `-c`, otherwise links an ELF;
it uses the fixed demo include search name `/usr/opt/src/include`. This is
a small driver for libtcc, not the complete upstream command-line frontend.
Opening the volume/Space currently requires the trusted boot owner. This
does not add arbitrary Space-open authority to ordinary spawned processes.

The adapter has a 32 MiB heap, 128 named entries, names shorter than 256
bytes, 32 local stream slots and an 8 MiB limit per object. Its numeric
conversion and stdio functions are a compiler-focused subset; floating-point
edge cases, full libc, dynamic linking, libtcc1 library coverage and TinyCC
self-compilation have not been established. `__DATE__`/`__TIME__` use a
deterministic 1970 epoch in this adapter. The normal boot process is parked
after the smoke test so it remains alive until the VM helper stops QEMU.

## Verification and kernel fix

The VM test performs two complete native builds. The first executable
prints `SPACE2 APP: answer=42 bss=zero data=7`; after editing the header
object, the second prints `SPACE2 APP: answer=43 bss=zero data=7`.
Both children must exit successfully. A missing include must fail without
overwriting the previous target object.

The first port attempt exposed a kernel ELF validation gap: ELF allows
an unaligned data base if its file offset has the same page displacement,
but Plan2001's segment page maps require a page-aligned base. The loader
now rejects such data segments before creating a process image. The guest
test deliberately mutates a valid ELF to confirm that OSPAWN rejects it.
No new syscall or ABI structure was added.

After QEMU stops, `check-tcc-space2.py` independently reads the extracted
O partition using `oinspect.py --space-id 2`. It verifies catalog entries,
published transactions, exact imported source bytes, edited header bytes,
relocatable output objects, the executable's two aligned load segments,
and the absence of directory objects. Evidence in the run directory:

- `serial-tcc.log`: actual guest compiler and application output.
- `after.img`: GE/O volume containing the guest's committed outputs.
- `objects/`: independently extracted published Space 2 objects.
- `space2-inspect.json`: tree/hash checks and object versions/hashes.
- `report.json`: checked results and compiler/kernel artifact hashes.

The normal MicroPython boot/commit path was also tested with the updated
kernel and formatter. Original component versions, source hashes and
licenses are recorded in [UPSTREAM.json](../user/tcc/UPSTREAM.json).
