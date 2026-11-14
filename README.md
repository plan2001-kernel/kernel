# Plan2001 Userland

A small Unix-like userspace built on the experimental [Plan2001 kernel](https://github.com/plan2001-kernel/kernel).

**Status:** Live kernel `main` CI, BusyBox-pinned source control and libc symbol audit are implemented. A new first-stage native target workflow builds original BusyBox `true`/`false`/`echo` applets with Plan2001's own libc and tests them on the real kernel in QEMU. The new MicroPython child-process integration uses native SPAWN/process Handle observation in QEMU. These are **limited three-applet subset** tests, not a complete upstream BusyBox multicall port or POSIX layer.

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

The [build mechanism](docs/BUILD.md) fetches the **latest kernel `main` on every CI run**, records the resolved commit as the run's source manifest, and keeps BusyBox pinned by SHA using [`config/sources.json`](config/sources.json). Actions checks out both as **sibling source trees** outside the userland repo, audits the native ABI/libc, and builds a minimal **Linux-host static BusyBox control** (`true`, `false`, `echo`). That control is intentionally linked against the Linux runner's libc and is **not** a Plan2001 program.

The native diagnostic workflow links the upstream BusyBox `true.c` applet against Plan2001's runtime and runs separate guest checks. The [F5 console test](docs/BUSYBOX-F5.md) also builds a native BusyBox-bearing ELF, executes upstream `echo_main` on kernel manifest slot 4 and reads PS/2 keyboard input through kernel-granted console regions, with real QEMU VGA verification. The dedicated [libc ABI audit](docs/libc-audit.md) compares host-compiled BusyBox object references with native library exports. These checks are not evidence of complete POSIX compatibility.

## Milestones (proposed)

1. Debian BusyBox dependency and static-link audit.
2. Minimal Plan2001 C runtime and POSIX adapters.
3. Small static applets running in Plan2001 QEMU.
4. ObjectSpace-backed paths and commands such as `ls`, `cat`, `cp`, `rm`, `mkdir`.
5. Shell process launch, redirection, userland pipes, and a broader Unix subset.

## Documentation

- [Build mechanism and local Debian commands](docs/BUILD.md)
- [GitHub Actions libc ABI audit and evidence](docs/libc-audit.md)
- [Native BusyBox target subset build and QEMU smoke test](docs/BUSYBOX-TARGET.md)
- [Userspace PVD and memory-only Unix directory tree (stage 2)](docs/UNIX-PVD.md)
- [MicroPython native BusyBox child-process launch, QEMU and command example](docs/BUSYBOX-SPAWN.md)
- [Native F5 console I/O adapter, capability rings and QEMU acceptance](docs/BUSYBOX-F5.md)
- [GitHub Actions build workflow](.github/workflows/build.yml)
- [Architecture](docs/ARCHITECTURE.md)
- [Minimal POSIX profile](docs/POSIX-PROFILE.md)
- [BusyBox porting plan](docs/PORTING.md)
- [BusyBox ash compilation and Newlib POSIX integration](docs/ASH-NEWLIB.md)
- [Shared GE/O UXFS service over capability-only UXPIPE RPC](docs/UXFS-SHARED-GEO.md)
- [Development instructions](AGENTS.md)

The authoritative native definitions are in the [kernel ObjectSpace architecture](https://github.com/plan2001-kernel/kernel/blob/main/docs/OBJECTSPACE.md) and [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md).

## Licensing

BusyBox is GPLv2-licensed. Record source provenance and licenses before importing BusyBox sources or kernel runtime code. No third-party sources are included at this stage.

## Stage 2: volatile Unix paths

The F5 acceptance process now carries a bounded userspace Present View Descriptor
(PVD) and the memory-only /bin, /dev, /etc, /home, /tmp, /usr/bin and /var
directory tree. Its interim console builtins implement pwd, cd and ls; these
are **not** upstream BusyBox applets or ash. GitHub Actions tests path rules
with host sanitizers and PS/2+VGA QEMU. Stage 2 alone provided no init, persisted objects, or
native pathname authority. See [PVD design](docs/UNIX-PVD.md).

## Stage 3: Unix init boot entry

The F5 boot-manifest executable now starts a [native Unix-init entry](docs/UNIX-INIT.md)
that constructs a fresh PVD root and hands it to its statically linked builtin
shell. The prompt starts at `unix:/$` and follows cwd. GitHub Actions verifies
a fresh root across **two QEMU boots of the same disposable GE/O disk**.
The initial shell is not yet upstream BusyBox ash or a separate process.
