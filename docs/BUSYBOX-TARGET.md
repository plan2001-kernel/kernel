# Native BusyBox target subset and QEMU smoke

This is the **first executable stage** of the Plan2001 BusyBox port.
The GitHub Actions workflow is [busybox-target.yml](../.github/workflows/busybox-target.yml).
This stage **does not** claim a complete upstream BusyBox multicall build or
POSIX libc implementation.

## Inputs and boundaries

- The [source policy](../config/sources.json) fetches the latest kernel `main` for each run and pins upstream BusyBox 1.36.1 by immutable commit; the resolved kernel SHA is recorded per CI run.
- The original upstream sources `coreutils/true.c`, `false.c` and
  `echo.c` are compiled **unchanged** with GCC 14, `-ffreestanding`,
  `-nostdinc`, and a small, explicit `ports/busybox/libbb.h` surface.
- Native executable startup, linker script, syscall stubs and
  `libc-user.a` are obtained from the fetched kernel's normal
  `tools/build-mpy-gcc.sh` output. The target link uses
  `-nostdlib -nostartfiles -static` with native objects and `libgcc`.
- No glibc, Linux CRT, Linux syscalls, kernel POSIX pathname layer, or
  `libbb/appletlib.c` are linked into the target. The first minimal
  multicall dispatch and console bridge live in **userland**.
- The stdout bridge opens a native `ObjOpenConsole` capability and writes
  via `obj_write`; `STDOUT_FILENO` only maps to that authorized handle.
  No false success is returned on partial/failed writes.
- The adapter is **not** a general POSIX fd table or complete libbb.
  In particular, `errno`, malloc failure handling and process exit do not
  yet satisfy the whole BusyBox environment.

## Reproduce on Debian

Install `gcc-14`, `build-essential`, `binutils`, `clang`, `lld`,
`mtools`, `ovmf`, `qemu-system-x86`, `python3`, `file` and `git`.
Run from the userland checkout with fresh throwaway source/report paths:

```sh
python3 tools/ci/fetch_sources.py \
  --lock config/sources.json --dest /tmp/p2001-bb-src \
  --reports /tmp/p2001-bb-reports

(cd /tmp/p2001-bb-src/kernel && \
  CC=gcc-14 tools/build-gcc.sh --kernel -j 4 && \
  tools/build-efi-gcc.sh -j 4 && \
  CC=gcc-14 tools/build-mpy-gcc.sh -j 4 && tools/build.sh)

python3 tools/ci/build_busybox_target.py \
  --kernel /tmp/p2001-bb-src/kernel \
  --busybox /tmp/p2001-bb-src/busybox \
  --reports /tmp/p2001-bb-reports --cc gcc-14

bash tools/ci/guest_busybox.sh \
  /tmp/p2001-bb-src/kernel \
  /tmp/p2001-bb-reports/busybox-target/busybox-smoke-plan2001.elf \
  /tmp/p2001-bb-reports
```

No command touches an existing GE/O block device: the QEMU helper creates
new **regular-file** images in the report directory. Never overwrite an
existing user's physical O volume.

## Build artifacts and acceptance

The build creates two freestanding AMD64 executable ELF files in runner
scratch space:

| ELF | Purpose |
| --- | --- |
| `busybox-plan2001.elf` | Minimal native multicall dispatcher using `argv[0]` applet alias or `busybox NAME ...` |
| `busybox-smoke-plan2001.elf` | Same upstream applet objects but with a boot-owner guest test launcher |

The build checker rejects PT_INTERP, PT_DYNAMIC, undefined symbols, wrong
architecture/type, absent upstream `echo_main`/`true_main`/`false_main`
symbols, or a PT_LOAD count different from the kernel's required two.

The guest test seeds the smoke ELF at executable GE/O OID
`0x4f45584543000003`, verifies its exact SHA-256, boots the **real**
Plan2001 kernel with OVMF in QEMU, and requires all of:

- `o: OEXECUTE oid=0x4f45584543000003` in the guest log.
- The **original upstream BusyBox `echo_main`** writes
  `PLAN2001-BUSYBOX-ECHO-HELLO`.
- An alias-form `/bin/echo` call writes
  `PLAN2001-BUSYBOX-ALIAS-OK`.
- `true_main` returns 0, `false_main` returns 1 and unknown applet
  dispatch returns 127 (validated inside the guest before the final marker).
- The guest emits `PLAN2001-BUSYBOX-GUEST-PASS` after all tests, and
  does **not** emit `PLAN2001-BUSYBOX-GUEST-FAIL`.

The fixture holds the boot-owner alive after the test because ordinary
shell-like program exit is not yet implemented for the boot-owner. This
does **not** test process completion or launching a second program.

On each successful CI run, the `busybox-plan2001-amd64` artifact contains
`busybox-plan2001.elf` (the actual native three-applet executable).
The separate `busybox-native-target-qemu` artifact contains JSON ELF hashes,
link diagnostics, and guest serial logs. Always use the run's real
PASS/FAIL status and job summary as evidence. A passing compiler job alone is **not** guest evidence.
This workflow does not run full BusyBox `make`, native `sh`, filesystem
POSIX adapters, broad applets or hardware.

## Next engineering steps

Implement native argv-based process launch/return, a real POSIX descriptor
table over ObjectSpace capabilities, and the larger `libbb` surface. Only
then attempt a broader BusyBox-selected applet build. Keep compatibility
in the userland repository rather than adding Linux syscalls to Plan2001.

## Source provenance

Original applet sources are from the pinned upstream BusyBox checkout,
GPL-2.0-or-later as declared in their source headers. The BusyBox sources
are not vendored into userland and must remain credited when distributed.
