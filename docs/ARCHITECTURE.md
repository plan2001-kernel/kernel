# Plan 2001-kernel: architecture

Plan 2001-kernel is an experimental operating-system kernel, not a production
compatibility platform. The architecture is deliberately open to multiple
instruction-set architectures.

## Architectural invariants

- Persistent data is stored in GE/O Spaces, identified by SpaceID and local
  object identifier (OID). A persistent occurrence is not a file path.
- A runtime object, a process-local Handle/capability, a persistent OID, and a
  physical device identity are distinct concepts.
- Process namespaces are volatile, reference-counted `Pgrp`/`OContext` views,
  not persistent GE/O objects. Stored namespace definitions and object sets
  are data, distinct from the live process namespace.
- Hierarchical filesystem paths, Plan 9 Chan/fid/qid, and 9P are **not** the
  native kernel object interface. A hierarchy may be a userspace view, not
  the persistent identity model.
- The system-call boundary is intentionally small and coherent, **not** an
  exercise in reaching a predetermined syscall count. Use object operations
  where identity, capabilities, ownership, or lifetime have object semantics.
  Keep direct scalar calls where an object would only add ceremony.
- ObjectFIFO is the generic device-frame/IPC channel. It preserves message
  boundaries and capability checks; it is not a filesystem pipe.
- The current proof of concept uses MicroPython as its primary userspace
  program. Additional userspace facilities are not prerequisites for basic
  kernel progress.


## Runtime process namespaces (Plan 9 lifecycle model)

A process namespace is an in-memory, reference-counted `Pgrp`/`OContext`
structure of authorized object-role bindings, **not** a persistent GE/O object
and not an OID. Following Plan 9's process-group lifecycle semantics, a child
may share its parent's `Pgrp` by default, get a copy with `RFNAMEG`, or start
with an empty view with `RFCNAMEG`. A newly created `Pgrp` has its own
`OContext`. `OSPAWN` creates a fresh group and can populate it from explicitly
selected delegated roles/views. Namespace ownership is not necessarily
one-to-one with processes: shared groups remain alive while referenced, and
the last owning reference releases their runtime view.

A process may alter its namespace during its lifetime through authorized
role binding/removal and `NamespaceView` attachment/detachment. Handles already
opened from a role do not become invalid merely because that role was removed;
revocation is a separate authority operation. Namespace roles bind capabilities,
not paths, persistent membership edges or stored-object names. The existing
`Pgrp`, `OContext`, `OBJBIND` and `NamespaceView` mechanisms implement this
runtime foundation on the full-system AMD64 path. This does not mean that a
persistent-definition loader or the future set/query model already exists.

## Namespace definition objects (target, not yet implemented)

A persistent GE/O object may contain a *definition* used to initialize or
modify a process's volatile namespace. This follows the separation in Plan 9:
a stored namespace description is not the live namespace it constructs.
A userspace interpreter/loader may offer operations analogous to `newns`
(rebuild a view) and `addns` (apply changes), using existing authorized object
operations. The definition's syntax, resolution rules and loading workflow
are not yet specified or implemented; there is no requirement that each
process creation automatically reads such an object.

Loading or possessing a definition, knowing an OID, or naming a device must
not mint authority. Every resulting role must be resolved from an authorized
source capability with explicit rights attenuation. Editing a definition does
not retroactively alter already constructed process views; reapplication must
be explicit unless a later design specifies otherwise. No Plan 9 mount tree,
pathnames, `Chan`/fid/qid, or 9P namespace is reinstated.

## Persistent GE/O object sets (separate data model; target, not yet implemented)

GE/O objects may describe persistent sets and explicit membership relations
between fully scoped object identities, including references to other sets.
This is *stored data*, not a persistent process namespace. The same object
may belong to multiple sets without changing identity or bytes. Its optional
stored name (at most one, non-unique) remains independent of identity, set
membership and runtime role labels. A hierarchical view is optional userspace
presentation, never the native kernel namespace.

Potential set/query semantics include membership testing, union (`A ∪ B`),
intersection (`A ∩ B`), difference (`A \ B`), and dynamic expressions that
reflect subsequent changes in referenced sets. An initial implementation may
reject cycles; an unbounded universal complement is not required. Identity
comparisons must include the volume when crossing volumes. Membership never
grants capabilities and query results must be filtered by the caller's
authorized visibility. The kernel should supply only necessary identity,
storage and authorization mechanisms; richer expressions belong in userspace.

The legacy GE R1 hashtag/tag model is not the target and requires no
migration: experimental volumes may be reamed and objects reimported. No
persistent set-relation engine or automatic namespace-definition loader is
claimed as implemented.

## Architecture status

**AMD64:** The current full-system implementation includes UEFI boot,
scheduler, virtual memory, GE/O, object/system-call interfaces, device support,
and MicroPython startup. Its existing build and test paths remain applicable.

**ARM64/AArch64:** Active early-boot port. The repository contains an ARM64
ELF kernel, UEFI loader, shared BootInfo v1 validation, exception/EL1 entry,
early mappings, PL011 console, GICv2/GICv3 timer interrupts, and an early
physical-page allocator. The `tools/qemu-arm64-boot.sh` check exercises early
boot with QEMU `virt` and GICv2 or GICv3; it does not establish full-system
capability or physical-hardware compatibility. ARM64 does **not** yet implement
the full AMD64 scheduler, virtual memory, GE/O, syscalls, userspace, or
device/networking stack.

This describes implementation maturity, **not** an AMD64-only policy. Shared
boot changes must consider both architectures; architecture-specific changes
must not invent cross-architecture feature parity.

## Implementation sources

- `kernel/core/`: core kernel and shared early boot.
- `kernel/amd64/`, `kernel/arm64/gcc/`: architecture-specific code.
- `boot/efi/`: UEFI loader sources.
- `ge/storage/`: GE/O storage engine.
- `runtime/abi/sys.h`: declared kernel syscall ABI and status.
- `micropython/`: current userspace interpreter.
- `kernel/drivers/`: device integrations.

**The current source is authoritative** where these notes and actual code
disagree. Record measured behavior separately from desired design.
