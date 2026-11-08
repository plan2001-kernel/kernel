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

## Persistent namespace and set semantics (architectural target)

The persistent relational namespace is a GE/O object with its own scoped OID.
Explicit membership relations connect it to member OIDs, including other
namespaces. This is distinct from the currently implemented process-group
`OContext` capability/role view described above: persistent membership
describes *which objects belong to a set*; `OContext` determines *which
objects the process can see and use*. Attaching a namespace or learning an
OID must not grant capabilities implicitly.

The minimal set model supports membership testing, union, intersection, and
difference (`A \\ B`, or "in A and not in B"). It does not require an
unbounded universal complement. Dynamic namespace expressions reference
other namespaces and are evaluated against their later memberships; initial
implementations may reject cyclic expression dependencies. A query result
must be restricted to the caller's authorized visibility, without exposing
unauthorized object identities or granting access by set membership alone.

An object's optional name (at most one) is separate from its identity,
persistent membership, and process namespace role. Multiple OIDs may have
the same name; finding by name within a namespace can return zero, one, or
several authorized identities. `OBJBIND` roles are capability-binding labels,
not alternate stored-object names or hashtag tags.

The kernel should expose only minimal OID relation/storage and authorization
mechanisms through the existing object ABI where appropriate. Union,
intersection, difference, and higher-level query parsing do not each need a
new syscall; user programs may evaluate complex expressions. Hashtag/tag
parsing, tag-specific indexes, and application query syntax do not belong in
the kernel. Existing GE R1/`OQUERY` hashtag behavior is legacy to remove,
not a requirement for the new set model. No compatibility migration of that
metadata is needed when the experimental volume is reamed and objects are
reimported. This section is a target contract, not a claim of implementation.

## Device messages and ObjectFIFO

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
