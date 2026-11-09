# Kernel interface principles

This document records current architectural boundaries, not a frozen binary
ABI and not a numbered syscall-reduction roadmap.

## Native object interface

Use object operations when the resource has identity, rights, ownership,
lifetime, delegation, or invocation semantics. Distinguish:

- Persistent GE/O identity: `(SpaceID, OID)`.
- Volatile object identity: boot/runtime scoped.
- Process-local Handle: capability/reference with bounded rights.
- Physical device selectors: provider-specific; not themselves authority.

A numeric selector must not grant access on its own. Rights must be checked at
the operation boundary. Revocation, final close, and process exit must handle
object/resource lifetimes coherently.

`runtime/abi/sys.h` and kernel dispatch/source implementations determine
**which syscalls currently exist and are active**. Do not infer active calls
from obsolete syscall inventories or try to meet a fixed count. The design
permits direct process/scalar calls when adding an object would not help.

## Operation groups and authority

Every active operation has exactly one owning group. The group is chosen by
responsibility and authority, not by argument count:

- **SYSCTL (144)** — system or own-process configuration and introspection.
  It owns the system snapshot, the caller's own Handle-table listing, process
  environment operations, privileged bootstrap volume activation and
  own-process CPU-affinity/memory-budget introspection. It does not transfer
  object data, mutate namespace bindings, create or wait for children, or
  issue device commands. Privileged mutations keep their existing authority
  (the boot owner for volume activation).
- **IOCTL (145)** — device configuration, control and device-specific status,
  authorized by a device Handle with per-operation rights: UART
  configuration/status, KBD LED control, USB provider administration and
  Ethernet status/detach. Read-only status and mutating control require
  distinct rights. Opening/closing devices and payload I/O stay in objects.
- **Objects (136–140)** — resource creation/open/close, capability grants,
  bindings, namespace views, content and message transport, and storage
  transactions.
- **Direct primitives** — `SPAWN`, `OEXECUTE`, `EXITS`, `OTERMINATE`,
  `PROCWAIT`, `RFORKEX`, memory (`BRK_`, `SEGBRK`, `SEGATTACH`/`DETACH`/
  `FREE`/`FLUSH`), time (`_NSEC`, `SLEEP`, `ALARM`), synchronization
  (`RENDEZVOUS`, `SEMACQUIRE`/`TSEMACQUIRE`/`SEMRELEASE`),
  `NOTIFY`/`NOTED` and `ERRSTR`. They may take structured arguments or
  authorized Handles but do not create a target-object capability.

`sysctl(scope, operation, SysctlReq*, size)` and
`ioctl(handle, operation, IoctlReq*, size)` use fixed-width, versioned,
bounded request records. The kernel validates version, reserved fields,
nested pointers, lengths and operation numbers, and rejects an unknown
operation. No textual command language or pathname-like tree is introduced.
Selection, a PID/OID, an operation number or a requested rights mask never
creates authority.

Read and write take no provider timeout: timeout zero keeps the provider's
existing blocking behavior, and a finite value is rejected explicitly rather
than silently ignored. ObjectFIFO receive keeps its separate bounded
timeout/non-blocking/infinite semantics.

## Runtime process namespaces and lifecycle

The active process namespace follows Plan 9's `Pgrp` lifecycle model, not a
persisted GE/O namespace object. A `Pgrp` owns a volatile `OContext` of
authorized role/Handle bindings. Processes may share one `Pgrp`, copy its view
with `RFNAMEG` or start a new empty view with `RFCNAMEG`. A newly created
`Pgrp` owns a new `OContext`; its final reference releases that context.
`SPAWN(146)` starts a fresh group whose delegated views/roles are explicitly
selected. These are runtime semantics implemented in the AMD64 full-system
path, not a guarantee of ARM64 feature parity.

Namespace views use the object ABI. `OBJOPEN(ObjOpenNamespaceCurrent)` grants
control over the caller's current namespace; the resulting Handle is bound to
that namespace and cannot control a different process group. Typed
`OBJINVOKE` operations create a sealed `VkindNamespaceView` from selected
delegable roles, attach it with replace/before/after precedence, detach it by
attachment token, and enumerate visible GE/O identities and rights into a
bounded `NsVisible` array. The request shapes and operation numbers are in
`runtime/abi/obj.h`; the record layout is in `runtime/abi/namespace.h`.
Visibility uses an epoch-validated snapshot and omits unauthorized identities.

`RFNOMNT` seals the caller's live `Pgrp` against namespace extension. The
seal is monotonic and group-scoped: it denies new or replacement `OBJBIND`
role bindings and `NamespaceView` creation/attachment, while removal of an
existing role, opening an unchanged role, visibility enumeration, view detach,
and already opened Handles remain permitted. Extension admission and the
corresponding `OContext` update are performed under one `Pgrp` gate, so a
concurrent `RFNOMNT` cannot be interleaved between the check and the mutation.
A plain `RFPROC` child shares the
same seal; `RFNAMEG` copies the seal together with an independent `OContext`;
`RFCNAMEG` keeps the inherited seal but starts with no roles. `SPAWN` still
creates a fresh unsealed group with only explicitly selected authorities,
while an explicit `SPAWN` SHARE joins the target group and therefore observes
its seal. There is no unseal operation.

Each namespace role holds one authorized object Handle. `OBJBIND` checks
delegation rights and can attenuate them; `OBJOPEN(ObjOpenBinding)` opens that
role through the same path for GE/O, FIFO, and other object kinds. Closing a
Handle releases one reference. `OBJBIND(ObjBindRemove)` removes a name without
invalidating Handles already opened from it. `OBJINVOKE(ObjOpRevoke)` revokes GE/O authority and its derived copies.
ObjectFIFO shutdown is the separate `OBJINVOKE(ObjOpFifoShutdown)` transport
operation; closing one Handle only releases its reference and never revokes
another resource. GE/O revocation follows the existing shared origin: sibling
copies of the same origin are affected, while independently opened grants to
the same OID remain valid. Persistent object identity and bytes remain intact.

## Process creation and capability inheritance

Process creation and object-Handle inheritance are one explicit, atomic
operation. `RFORKEX(143)` is the canonical fork boundary: ordinary fork flags
plus a bounded, versioned list of parent Handles and their rights attenuation.
An ordinary fork is the libc `rfork()` wrapper with an empty Handle plan; the
historical `RFORK(19)` number is reserved and never copies an object Handle.
`SPAWN(146)` is the direct child-creation call taking an authorized Space
Handle and OID, consistent with `OEXECUTE`. The `Oinherit`/`Oinheritv` options
are translated into the same internal validation and commit path, so the
entry points cannot diverge.

Namespace-group sharing/copying/creation is a separate decision from
Handle-capability inheritance. Sharing a `Pgrp` must not imply that a child
receives every process-local Handle; copying role bindings must preserve the
underlying authority and attenuation checks. A newly started process need not
create a distinct namespace when it explicitly shares a group.

The complete Handle plan is validated against the parent before a child is
published: each source Handle must be live, carry `Vdelegate`, not be a
subscription, and the requested rights must be an equal or attenuated subset.
Destination-slot conflicts are rejected. The parent's Handle table and
reference counts are never mutated by the attempt. The child starts with an
empty Handle table and receives only the declared entries; child Handles get
fresh child-local generations, and RFORKEX returns the assigned numbers
explicitly when the caller supplies an output array. The result is one child
Handle per input entry, in input order; the same parent Handle may be listed
more than once and then produces independent child Handles with independently
attenuated rights. An ordinary fork or an unlisted Handle never crosses.

SPAWN's `stdoutpipe` names one parent Pipe. It must resolve to exactly one
declared plan entry; an absent source or a source listed more than once is
ambiguous and is rejected explicitly rather than silently selecting the first
match.

`OBJINVOKE`/`SYSCTL`/`IOCTL` are not configuration side channels for a later process creation:
there is no one-shot inheritance mark. A provider-specific argument such as a
UART mode or USB kind/mode is validation input to its opened Handle, not
ambient authority and not a hidden process-creation option.


## Stored namespace definitions (architectural target, not implemented)

A GE/O object may store *instructions/data to construct a runtime namespace*,
not the namespace itself. A userspace loader can read an authorized definition
and apply role-binding operations to a process's `OContext`, analogous to Plan
9's `newns` and `addns`. This need not be a new syscall, a new kernel parser,
or an on-volume namespace identity. The definition format, when and by whom
it is applied, and any initial bootstrap authority are future design work.
Loading a definition does not automatically grant access to any OID, device,
role or referenced object; each binding requires a legitimately delegated
capability and checked rights. Changing stored instructions does not
automatically rewrite an existing runtime namespace.

Live namespace visibility and capability authority remain on the process
`Pgrp`/`OContext` and authorized `NamespaceView` boundary. An `OBJBIND` role
is an in-memory capability-binding label, not a persistent object name or a
membership edge. Unbinding only removes a role; independently held Handles
remain valid until closed/revoked under their own lifetime rules. Runtime
namespace expiry is independent of GE/O object contents and versions.

## Persistent object sets (distinct future data/query semantics)

A GE/O object may separately represent a persistent set with explicit
membership relations to fully scoped object identities, including other
sets. Such a set is **not** a process namespace. The possible set model
supports membership, union, intersection, and difference (`A \ B`), without
requiring a universal complement. Dynamic expressions can evaluate against
later memberships; initial implementations may reject cyclic dependencies.
Set queries must not expose unauthorized identities or grant object access
merely from knowledge of an OID or membership. Identity comparisons include
volume identity when crossing volumes.

An object's optional stored name (at most one, not globally unique) is
separate from its identity, set membership, and runtime namespace roles.
Finding a name inside a set may yield multiple authorized identities. Rich
query expressions, tags, and application query languages belong in userspace;
a separate syscall for each set operation is unnecessary. A persistent
set-relation engine is not yet implemented.

The legacy GE R1 hashtag implementation has been removed from the kernel
and MicroPython; `OQUERY` syscall 106 is reserved without a handler. No old
R1 metadata migration is required: experimental volumes may be explicitly
reamed and their objects reimported.

## Device messages and ObjectFIFO

`docs/DEVICE-ABI.md` is the normative device authority contract. All
supported physical families (UART, PS/2 keyboard, Ethernet, USB control and
USB endpoints) are discovered with `ObjOpDeviceList` and opened with
`ObjOpenDevice` through an authorized catalog Handle. Device selectors are
hints, never authority; options and rights are validated before acquisition.
Device configuration/control uses `IOCTL`; byte, frame and transfer payload
stay on the object interface.

ObjectFIFO is the common capability-aware transport for framed messages and
IPC. Frame bytes and transport metadata should remain separate from object
identity. A FIFO does not grant independent device-control rights.

The current device integration distinguishes provider semantics:

- USB control transfers have synchronous provider completion and a
  session-owned pending response; do not claim that this is universal FIFO
  completion behavior.
- Raw Ethernet transmission reports accepted/enqueued L2 bytes, not physical
  completion. Receiver-channel acquisition (`ObjOpDeviceRx`) is a distinct
  operation from retrieving one completed USB message (`ObjOpDeviceRecv`).
  Device SEND results carry a typed accepted-bytes or completion-sequence
  discriminator in `ObjCall.aux`.
- `ObjOpInfo` exposes only generic identity/type/size/rights metadata;
  `ObjOpStat` is the separate FIFO/subscription statistics contract.
- `ObjOpCommit` is a Space-transaction commit. Committing through a GE handle
  commits the owning Space's pending writes, not just that one object.

Do not restore pathname-based `/dev` adapters, Plan 9 Chan/fid/qid, or
separate protocol-specific kernel syscall families just to reach userspace.
IPv6 protocol processing and the minimal ESP demonstration are userspace
concerns unless an explicit kernel requirement says otherwise.

## Development boundary

The basic proof of concept needs a functioning kernel-to-MicroPython path.
Do not treat old user-journey experiments, one-off networking demonstrations,
or old T-stage documents as current architecture mandates.

Implementation references: `runtime/abi/sys.h`, `kernel/core/`,
`kernel/drivers/`, `ge/storage/`, and `micropython/`. Verify exact
constants and structs against headers before changing code.
