# Userland architecture

**Status: proposed design, not a working POSIX implementation.**

The Plan2001 kernel's [ObjectSpace architecture](https://github.com/plan2001-kernel/kernel/blob/main/docs/OBJECTSPACE.md) is authoritative. Userland must not turn the kernel into Unix.

## Boundary

* **Kernel:** ObjectSpace graphs, capabilities/attenuation/revocation, GE/O persistent identity and storage, processes, memory regions, device access.
* **Userland:** Unix-style path and working-directory projection (PVD), per-process descriptor table, minimal C runtime, POSIX adapters, BusyBox tools.
* **Native apps:** can bypass POSIX entirely and use the native object ABI.
* **Static BusyBox:** libc and compatibility code are linked into one executable. There is no dynamic loader requirement at first.

No Linux syscall emulation, kernel VFS, pathname syscall, Plan 9 Chan/9P, kernel Pipe, ObjectFIFO, or new generic IPC transport is proposed.

## Paths as a projected view

A Unix `/usr/bin/cc` path can traverse the authorized graph:

```text
root ObjectSpace
    "usr" -> ObjectSpace U
               "bin" -> ObjectSpace B
                            "cc" -> executable GE/O object
```

A slash is parsed only in userland. Native ObjectSpace labels are local opaque names, not mandatory path components. Bindings can alias objects, duplicate labels, have multiple parents, and form cycles.

The PVD (present view descriptor) belongs to userspace and stores delegated root capabilities, current view, selected traversal stack and display path. `..` follows that view stack instead of assuming a native single parent. Paths may not escape the delegated view. Cycles require bounded traversal. Duplicate names are ambiguous: initial POSIX lookup must reject ambiguous selection, never arbitrarily choose one binding. Native callers can resolve by BindingID.

The current v1 ObjectSpace API limits a space to 64 bindings and labels to 47 bytes plus NUL. Persistent graph records have their own size limits. Do not assume an unbounded directory hierarchy exists.

## Identity, security, metadata

A regular file is a userspace interpretation of GE/O content and metadata. Durable identity can derive from (VolumeID, OID), but a synthetic POSIX inode number or path does not authorize access.

An `open()` adapter resolves the path using explicitly held capabilities, verifies requested rights, and creates a process-local descriptor referring to an authorized object or stream backend and offset state. `read`, `write`, `lseek` and `close` then operate through that entry. POSIX-compatible handling of partial I/O, append, truncate, durability and concurrent access must be defined.

`unlink` removes a binding, not automatically the GE/O object or an already-open Handle. `rename`, `link`, object lifetime and reclamation require separate atomicity rules.

OwnerID is provenance, not ambient authorization. Unix uid/gid and rwx metadata can be exposed as compatibility information only; capability checks remain authoritative.

## Processes, streams and console

Native SPAWN/RFORKEX and process Handle observation are not automatically full POSIX fork/exec/wait semantics. Early applets should avoid these dependencies. Later shell execution must explicitly delegate capabilities and descriptor state, preserving applet `argv[0]`.

Native IPC uses memory-publication regions with one RW publisher, RO observers and WAIT/WAKE. Future POSIX pipes may be constructed in userland with explicit stream buffering, EOF, backpressure and lifetime semantics. Do not restore a kernel pipe.

Console input/output must respect the native session/device authority model.

## Persistence boundary

Persistent ObjectSpace graph snapshots exist experimentally in the kernel. Root selection, complete lifetime/GC policy and hardware validation remain separate work. The early Unix view can use an explicitly supplied root capability. No compatibility-layer test may silently reformat GE/O storage.

Native ABI and implementation details must always be checked in the current [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md) and source before coding.

## First Unix init and F5 shell handoff

The stage-3 AMD64 boot-manifest F5 ELF enters the userland `p2001_unix_init`
function after validating native stdin/stdout/stderr region grants. Init
constructs a fresh, process-local volatile PVD with cwd `/`, and passes it
to a statically linked small shell builtin entry. The console prompt reflects
the current projected path. There is no Unix init-process ancestry or
separate shell process yet; no arbitrary ObjectSpace root or GE/O authority
is created by this memory-only directory tree. See [UNIX-INIT](UNIX-INIT.md).
