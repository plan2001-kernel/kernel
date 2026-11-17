# Plan2001 Userland

A small Unix-like userspace built on the experimental [Plan2001 kernel](../README.md).

**Status:** AMD64 has a reduced, genuine BusyBox 1.36.1 **ash** statically linked with upstream Newlib 4.6.0 and native Plan2001 POSIX adapters. An F5 supervisor can start ash with keyboard/VGA, a persistent ObjectSpace-backed Unix file view shared with MicroPython, redirection and forked pipes. The native userland also implements bounded fork/exec/wait, descriptor inheritance and asynchronous signals through capability-based kernel primitives. A separate three-applet BusyBox target and a legacy F5 builtin-shell fixture remain regression tests. The reduced native multicall contains the configured applets; this is not a full POSIX distribution. The required native alarm startup fix (kernel PR #64) is merged into kernel `main`.

A separate [native upstream multicall profile](docs/BUSYBOX-MULTICALL.md) now builds 30 command names with generated executable aliases and a 434-name release inventory. It is an initial implementation of #17; the disabled-applet feasibility audit remains incomplete.

## Goal

Create a familiar Unix command-line environment using a statically linked, selectively configured BusyBox. Alpine Linux minimal base is the initial user-experience reference; OpenBSD base provides a secondary reference for Unix utility behavior. Neither Linux nor OpenBSD kernel ABI compatibility is required.

Plan2001's ObjectSpace, capability permissions, GE/O storage, and region-based IPC remain native. Unix paths, file descriptors, and a working-directory view belong in userland.

## Architecture

```text
static BusyBox ELF (applets + minimal libc + POSIX adapters)
                        |
       userspace paths / PVD / fd table
                        |
        Plan2001 native object ABI
                        |
     ObjectSpace / GE/O / capabilities
                        |
                 Plan2001 kernel
```

Static linking means the necessary C runtime functions are included inside the executable. It does not eliminate the need for libc functionality.

The familiar `/bin`, `/usr`, `/etc`, and `/tmp` directories are userland projections of ObjectSpace bindings, **not kernel directories**. Multiple bindings can refer to one BusyBox executable GE/O object; an applet launcher must preserve the requested `argv[0]`.

## Build and CI workspace

The [build workflow](docs/BUILD.md) tests kernel and userland from the same checkout and records that revision alongside the pinned BusyBox source. The Linux-host static BusyBox control is built separately from the native Plan2001 executables.

The native diagnostic workflow links the upstream BusyBox `true.c` applet against Plan2001's runtime and runs separate guest checks. The [F5 console test](docs/BUSYBOX-F5.md) also builds a native BusyBox-bearing ELF, executes upstream `echo_main` on kernel manifest slot 4 and reads PS/2 keyboard input through kernel-granted console regions, with real QEMU VGA verification. The dedicated [libc ABI audit](docs/libc-audit.md) compares host-compiled BusyBox object references with native library exports. These checks are not evidence of complete POSIX compatibility.

## Implementation milestones

1. **Implemented:** dependency and static-link audit, native Newlib/POSIX adapters and small BusyBox applets in AMD64 QEMU fixtures.
2. **Implemented with limits:** capability-backed process launch, `fork`/`execve`/`waitpid`, signals, redirection and UXPIPE pipelines.
3. **Implemented with limits:** persistent ObjectSpace-backed file projection and a 30-name native BusyBox multicall profile (`ls`, `cat`, `mkdir`, `mv`, `rm`, `ash`, etc.). The older memory-only PVD/UXFS stage fixtures remain separate.
4. **Outstanding:** remaining BusyBox applet families (including multicall `cp`), fuller POSIX semantics, job control/termios, cross-volume bindings, broader hardware support and physical-machine validation.

## Documentation

- [Classification of computer use by user purpose (A–D) and application review](docs/USER-PURPOSE-CLASSIFICATION.md)
- [Operator-free logical search matrix (stream-filter design proposal)](docs/LOGICAL-SEARCH-MATRIX.md)
- [Build mechanism and local Debian commands](docs/BUILD.md)
- [GitHub Actions libc ABI audit and evidence](docs/libc-audit.md)
- [Native BusyBox target subset build and QEMU smoke test](docs/BUSYBOX-TARGET.md)
- [Userspace PVD and memory-only Unix directory tree (stage 2)](docs/UNIX-PVD.md)
- [MicroPython native BusyBox child-process launch, QEMU and command example](docs/BUSYBOX-SPAWN.md)
- [Native F5 console I/O adapter, capability rings and QEMU acceptance](docs/BUSYBOX-F5.md)
- [GitHub Actions build workflow](../.github/workflows/build.yml)
- [Architecture](docs/ARCHITECTURE.md)
- [Minimal POSIX profile](docs/POSIX-PROFILE.md)
- [BusyBox porting plan](docs/PORTING.md)
- [BusyBox ash compilation and Newlib POSIX integration](docs/ASH-NEWLIB.md)
- [Native Unix process, file-descriptor and signal projection](docs/UNIX-PROCESS.md)
- [UEFI ash boot image, hardware limitations and operator guide](docs/HARDWARE-BOOT.md)
- [Native ash/Newlib QEMU integration gate](../.github/workflows/ash-integration.yml)
- [Shared GE/O UXFS service over capability-only UXPIPE RPC](docs/UXFS-SHARED-GEO.md)
- [Development instructions](AGENTS.md)

The authoritative native definitions are in the [kernel ObjectSpace architecture](https://github.com/plan2001-kernel/kernel/blob/main/docs/OBJECTSPACE.md) and [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md).

## Licensing

BusyBox is GPLv2-licensed. Record source provenance and licenses before importing BusyBox sources or kernel runtime code. No third-party sources are included at this stage.

## Historical stage 2 fixture: volatile Unix paths

The F5 acceptance process now carries a bounded userspace Present View Descriptor
(PVD) and the memory-only /bin, /dev, /etc, /home, /tmp, /usr/bin and /var
directory tree. Its interim console builtins implement pwd, cd and ls; these
are **not** upstream BusyBox applets or ash. GitHub Actions tests path rules
with host sanitizers and PS/2+VGA QEMU. Stage 2 alone provided no init, persisted objects, or
native pathname authority. See [PVD design](docs/UNIX-PVD.md).

## Historical stage 3 fixture: Unix init boot entry

The stage-3 F5 boot-manifest executable starts a [native Unix-init entry](docs/UNIX-INIT.md)
that constructs a fresh PVD root and hands it to its statically linked builtin
shell. The prompt starts at `unix:/$` and follows cwd. GitHub Actions verifies
a fresh root across **two QEMU boots of the same disposable GE/O disk**.
That stage-3 shell is the legacy builtin evaluator, not upstream ash; the separate ash hardware-boot profile uses a genuine ash ELF and a trusted F5 supervisor.
