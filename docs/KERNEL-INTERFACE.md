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

## Runtime process namespaces and lifecycle

The active process namespace follows Plan 9's `Pgrp` lifecycle model, not a
persisted GE/O namespace object. A `Pgrp` owns a volatile `OContext` of
authorized role/Handle bindings. Processes may share one `Pgrp`, copy its view
with `RFNAMEG` or start a new empty view with `RFCNAMEG`. A newly created
`Pgrp` owns a new `OContext`; its final reference releases that context.
`OSPAWN` starts a fresh group whose delegated views/roles are explicitly
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

Each namespace role holds one authorized object Handle. `OBJBIND` checks
delegation rights and can attenuate them; `OBJOPEN(ObjOpenBinding)` opens that
role through the same path for GE/O, FIFO, and other object kinds. Closing a
Handle releases one reference. `OBJBIND(ObjBindRemove)` removes a name without
invalidating Handles already opened from it. `OBJINVOKE(ObjOpRevoke)` revokes
GE/O authority and its derived copies, or closes a FIFO with `Vcontrol`.
GE/O revocation follows the existing shared origin: sibling copies of the
same origin are affected, while independently opened grants to the same OID
remain valid. Persistent object identity and bytes remain intact.

## Process creation and capability inheritance

Process creation and object-Handle inheritance are one explicit, atomic
operation. `RFORKEX(143)` is ordinary `RFORK(19)` plus a bounded, versioned
list of parent Handles and their rights attenuation; `RFORK(19)` itself never
copies an object Handle. OSPAWN's existing `Oinherit`/`Oinheritv` options are
translated into the same internal validation and commit path, so the two
process-creation entry points cannot diverge.

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
attenuated rights. An ordinary `RFORK` or an unlisted Handle never crosses.

OSPAWN's `stdoutpipe` names one parent Pipe. It must resolve to exactly one
declared plan entry; an absent source or a source listed more than once is
ambiguous and is rejected explicitly rather than silently selecting the first
match.

`OBJINVOKE` is not a configuration side channel for a later process creation:
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

ObjectFIFO is the common capability-aware transport for framed messages and
IPC. Frame bytes and transport metadata should remain separate from object
identity. A FIFO does not grant independent device-control rights.

The current device integration distinguishes provider semantics:

- USB control transfers have synchronous provider completion and a
  session-owned pending response; do not claim that this is universal FIFO
  completion behavior.
- Raw Ethernet transmission reports accepted/enqueued L2 bytes, not physical
  completion. Receive frames are exposed as immutable message objects through
  a FIFO/subscription; device control is separately authorized.

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
