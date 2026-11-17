# Userland architecture

**Status: an experimental native Unix projection; the BusyBox profile uses
persistent ObjectSpace files and capability-based processes and streams.**

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

Persistent ObjectSpaces grow dynamically and accept 255-byte labels. Each
directory has its own framed GE/O object. Allocation and the native GE/O
transaction budget bound growth; there is no fixed 64-binding directory limit.

## Identity, security, metadata

A regular file is a userspace interpretation of GE/O content and metadata. Durable identity can derive from (VolumeID, OID), but a synthetic POSIX inode number or path does not authorize access.

An `open()` adapter resolves the path using explicitly held capabilities, verifies requested rights, and creates a process-local descriptor referring to an authorized object or stream backend and offset state. `read`, `write`, `lseek` and `close` then operate through that entry. POSIX-compatible handling of partial I/O, append, truncate, durability and concurrent access must be defined.

`unlink` removes a binding, not automatically the GE/O object or an already-open Handle. `rename`, `link`, object lifetime and reclamation require separate atomicity rules.

OwnerID is provenance, not ambient authorization. Unix uid/gid and rwx metadata can be exposed as compatibility information only; capability checks remain authoritative.

## Portable Unix file metadata (target profile)

The binary layout of framed GE/O payloads is specified by the kernel's
portable-object footer contract. The userspace [Unix metadata profile](OBJECT-METADATA.md) recommends one object-owned descriptive name per OID,
creation instant, media type, ordinary stat attributes, optional ACLs and
extended attributes. Multiple distinct OIDs may share the same object
name; ObjectSpace labels and Unix pathnames remain unrelated and may be
changed independently of that name. Only the raw-first / metadata-last / fixed-footer
byte layout is normative; the CBOR metadata schema is a de facto
recommendation. The persistent project adapter reads and preserves this metadata on the
shared object; changing a binding does not change its descriptive name.
Names and metadata never mint kernel capabilities.

## Processes, streams and console

The [Unix process adapter](UNIX-PROCESS.md) implements bounded fork/exec/wait using native SPAWN, RFORKEX and process Handles. Shell execution explicitly delegates capabilities and descriptor state and preserves the applet `argv[0]`.

Native IPC uses memory-publication regions with one RW publisher, RO observers and WAIT/WAKE. Userland pipes use UXPIPE, with explicit buffering, EOF, backpressure and endpoint lifetime rules.

Console input/output must respect the native session/device authority model.

## Persistence boundary

The BusyBox project adapter operates directly on delegated persistent
ObjectSpace records. File descriptors hold object capabilities and share only
open offsets across fork/exec. Namespace changes and their metadata publish
atomically. Unlink and rename preserve open objects. There is no per-process
file-content or directory-tree mirror. Union/intersection views use live source
references; writes require an explicit source. Automatic orphan collection and
cross-volume durable bindings remain outside the current implementation. No
compatibility-layer test may silently reformat GE/O storage.

Native ABI and implementation details must always be checked in the current [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md) and source before coding.
