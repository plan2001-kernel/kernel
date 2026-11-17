# Debian-first BusyBox compilation experiment

> **Historical initial porting plan.** The original Linux-host-only baseline
> has been superseded. Native Plan2001 ELF links and AMD64 QEMU tests now
> exist for small applets, genuine ash and a 30-command multicall profile.
> See [BUILD.md](BUILD.md), [ASH-NEWLIB.md](ASH-NEWLIB.md) and
> [BUSYBOX-MULTICALL.md](BUSYBOX-MULTICALL.md). The text below retains the
> early experiment's goals; a host-only smoke result must never be presented
> as native acceptance, and full POSIX conformance remains outstanding.

## Goal

Discover whether a tiny BusyBox configuration can compile and statically link against Plan2001's existing C runtime and thin POSIX adapters **without importing Debian's host libc**. Document missing interfaces rather than prematurely implementing all of POSIX.

## Inputs

* Kernel project: [plan2001-kernel/kernel](https://github.com/plan2001-kernel/kernel)
* Inspect `runtime/libc/port`, `runtime/libc/9sys`, `runtime/abi` and the actual kernel user-program build/startup/linker sources.
* BusyBox upstream: https://busybox.net/ . The initial mirror source revision (BusyBox 1.36.1) and exact SHA are pinned in [`config/sources.json`](../config/sources.json). Record origin and licensing for subsequent revisions.
* Initial target: Plan2001 AMD64 userspace ELF. ARM64 target support must be verified separately.

The automated baseline in [GitHub Actions](../.github/workflows/build.yml) builds **only a Linux-host BusyBox static control** with `CONFIG_STATIC`, `CONFIG_TRUE`, `CONFIG_FALSE`, and `CONFIG_ECHO` enabled. Its success does not imply a Plan2001 target ELF. At the time of this initial baseline, native compilation/link was future
work; it is now implemented in separate target workflows. This host-control
job still does not by itself prove native guest execution.

## Stage A: compile and dependency inventory on Debian

1. Inspect current Plan2001 executable loader, entry ABI, stack, argv, startup code, syscall stubs and target toolchain.
2. Select only a tiny BusyBox applet set, initially `true`, `false` and `echo` if they fit.
3. Attempt compilation using target headers/runtime, recording all missing headers, functions and nonportable assumptions.
4. Classify blockers by C runtime, POSIX fd/path/console, process/environment, ELF startup, toolchain support, and compiler runtime.
5. Attempt static linking. `-nostdlib` avoids automatic startup/libc, but the build must explicitly supply startup code and any required GCC support objects.
6. Inspect the artifact:

```sh
file path/to/busybox
readelf -hW path/to/busybox
readelf -lW path/to/busybox
readelf -dW path/to/busybox
nm -u path/to/busybox
```

A valid static target should not accidentally have a Linux `PT_INTERP`, Linux shared dependencies, Linux startup/TLS assumptions, unresolved target symbols or mismatched entry ABI. Default Debian GCC targeting AMD64 is not proof of Plan2001 compatibility.

A normal host BusyBox linked to Debian libc is only a baseline control.

## Stage B: adapt minimal userspace

Use minimal C runtime functions and explicit, auditable Plan2001 POSIX adapters. Never return fake success for unimplemented operations. Do not modify kernel ABI solely to make a link pass.

## Stage C: execute on Plan2001 QEMU

Create an authorized executable object, provide the necessary capabilities, boot the real Plan2001 kernel, and verify stdout, exit status, `argv[0]` multicall selection, and eventually GE/O-backed file operations. A Debian host execution is not a kernel execution test.

## Acceptance evidence

Each implementation PR should state the upstream BusyBox revision/config, compiler and linker versions/options, kernel ABI revision, C runtime sources, exact link diagnostics, host tests, QEMU tests, hardware tests and **NOT RUN** items.

The first implementation PR covered the **compile/link audit**, not the
entire shell or POSIX filesystem. Subsequent native profiles are covered
by their own build, runtime and QEMU evidence.
