# ObjectSpace: native capability graph

**Target architecture**, November 2026. The first implementation is a
**volatile proof of concept**, not the completed kernel migration.
No historical object ABI, disk format, Unix or Plan 9 compatibility is required.

## Normative concepts

- `ObjectID`: identity only. Volatile ID = `(incarnation, scope, oid)`.
  Durable GE/O ID = `(VolumeUUID, private-storage-SpaceID, oid)`.
- `OwnerID`: immutable provenance and object lifecycle responsibility.
  Trusted boot may use it to issue initial capabilities; knowing the owner
  is not itself an all-access token.
- `Capability`: unforgeable, revocable, typed object reference with reduced
  rights; represented by a generation-checked process-local Handle.
- `ObjectSpace`: independent kernel object containing a multigraph of
  labeled, typed bindings. It can refer to data, processes, devices,
  regions, services and other ObjectSpaces.
- `Binding`: stable BindingID, local label, target identity/reference,
  rights ceiling, relation type, flags and generation.
- `Context`: explicitly delegated roots/capabilities used by one execution
  context. No kernel current working directory, root directory, pathname,
  single parent or mandatory process parent/child tree exists.

Graph semantics:

```
ObjectSpace -(BindingID, label, rights, relation)-> arbitrary kernel object
```

The same object may have many bindings in one or more spaces. The same
label may name several objects: BindingID selects a particular edge.
Labels are opaque local strings, never a pathname. Space membership does
not confer ownership. Unbind removes the relation, not the object's
identity or another already opened capability. `contains`, `names`,
`uses` and `imports` are metadata: arbitrary semantics belong to
user space. A directory, project, room, container or module is a view of
reachable graph nodes, not a distinct kernel object type.

## Authorization

```
allowed = valid_capability && required_rights_subset &&
          !revoked && provider_safety_conditions
```

OwnerID-equality checks have now been replaced by explicit Handle
admission for data, GE/O, region writer mapping, devices and process control.
OwnerID is immutable provenance, not a bypass or a veto for authorized access.
Creation assigns it atomically; later cross-owner writes retain the original
stored owner. Trusted boot/installer authority remains separate: untrusted
code cannot open a GE/O Space merely by guessing `init`, register privileged
program identities, or forge a process owner. No OID, label or persisted
metadata grants power without an authorized capability.

Handle copies now create explicit, bounded derivation lineages. A source Handle
with Vdelegate can call `cap_revoke(source)` to revoke **all previously
derived descendants** of that exact capability while preserving the source
and its siblings. The caller can derive new descendants afterwards.
Revocation differs from unbind, object deletion and the existing provider-
specific `obj_revoke` (e.g. permanently disabling new region mappings).
A child checks all ancestor generations on each Handle admission, so closing
the source Handle does not erase its revocation authority while it has
descendants; pinned lineage nodes outlive the original Handle.

Implemented as `ObjOpCapRevoke=57` and MicroPython `plan9.cap_revoke(h)`.
The derivation chain has a bounded depth of 128. This is an **initial generic
Handle-lineage revocation mechanism**; provider-specific OS authorizers still
need migration. In-flight calls admitted before revocation may complete;
future Handle admissions are rejected. Existing MMU mappings survive until
explicit unmap/exit and are not silently torn down.

ObjectSpace bindings derive their own capability node from the bound source.
Resolving the binding derives from that node, not directly from an object
pointer. Revoking the bound source therefore makes the binding and every
resolved Handle unusable, while unrelated source grants remain valid.
Enumeration sets the `ObjSpaceFlagRevoked` bit when a binding's lineage has
been revoked. Unbinding releases the graph edge but does **not** revoke
already resolved Handles. ObjectSpace-to-ObjectSpace edges remain weak for
object lifetime, but their *capability lineage* is pinned.

## Implemented runtime v1

Native kind: `VkindObjectSpace=19`. Create with
`OBJCREATE(ObjKindObjectSpace=5)`. Existing object syscall family is
used rather than adding per-provider syscalls. The universal object ABI
is intentionally bumped to v2.

| Operation | Required capability | Result |
|---|---|---|
| `ObjOpSpaceBind=53` | space Vcontrol, target Vdelegate | BindingID |
| `ObjOpSpaceUnbind=54` | space Vcontrol | remove one BindingID |
| `ObjOpSpaceResolve=55` | space Vread | attenuated target Handle |
| `ObjOpSpaceEnumerate=56` | space Vmeta | paged edge metadata + epoch |

C layouts: `runtime/abi/objectspace.h`. Limits: 64 edges per space,
47 label bytes + NUL, eight entries per enumeration page. Each binding
increments a generation counter. Enumeration with a previous epoch
rejects concurrent modifications, avoiding mixed snapshots.

Cycles between volatile ObjectSpaces are supported. **Caveat:** v1
space-to-space edges are weak to avoid strong-reference cycles. A target
space's independent owner must keep a Handle open. A dead target is
reported as STALE; resolve rejects it. Nonspace edges pin their target
resources until unbound. This is a deliberately incomplete lifetime
policy, not a claim of general graph garbage collection.

The graph is process-independent and can be explicitly delegated at
launch through the existing Handle inheritance mechanism. The kernel
has not yet installed a default ObjectSpace root in ExecutionContext.
Legacy Pgrp/OContext namespace roles coexist only until their migration.

MicroPython API:

```python
import plan9 as p
root = p.space_new()
other = p.space_new()
data = p.obj_create(b'payload')
a = p.space_bind(root, 'src', data, p.VREAD|p.VMETA)
b = p.space_bind(root, 'project', other, p.VREAD|p.VMETA)
rows, next_cursor, epoch = p.space_list(root)
reader = p.space_resolve(root, a, p.VREAD)
p.space_unbind(root, a)
# reader retains its capability: unlink is not revoke.
```

## Persistent GE/O integration (target, NOT IMPLEMENTED)

**Do not use GE SpaceID as an ObjectSpace parent.** SpaceID presently
selects an internal storage allocation and commit scope; it may later be
removed or repurposed without migration. An ObjectSpace must itself have
a durable OID and store versioned binding records, not live Handle
numbers or kernel pointers. Each binding stores BindingID, label,
target durable identity, relation, rights *ceiling*, generation and
optional metadata object ID.

Loading stored metadata from disk must never create authority simply
because it contains an OID/rights pair. A trusted bootstrap policy
validates targets and reifies only explicitly authorized capabilities.
Graph operations and referenced-object creation must be transactionally
published in GE/O as one recoverable version, with torn-write and
reboot negative tests. Objects need not belong to any persistent graph,
and removing a graph/binding never implies deletion of the target.

Use fresh test images. NEVER silently reformat a hardware O volume. No
legacy tag/hashtag schema is restored.

## Execution, IPC and user-space projection (target)

ExecutionContext will reference explicit ObjectSpace roots plus CPU,
address space, scheduler and IPC/device capabilities. Independent process
launch does not imply lifecycle ownership. Chroot-like isolation and
containers consist of restricted capability roots, not filtered paths.
A userspace PVD may display nested unions and multiple parents; cycle
detection, display names, filtering, sorting and navigation belong there.
No generic FIFO, message queue, Plan 9 9P, path, mount, CWD or TCP stack
is introduced. Existing one-writer/RO-observer region IPC remains.

## Required migration stages

1. **Volatile graph PoC:** kernel ObjectSpace ops, MicroPython tests,
   aliases, multiple labels, nested spaces, cycles, rights, stale handling,
   concurrent enumeration and AMD64 QEMU boot. This branch implements a
   foundation; full test execution is still required.
2. **Capability security:** generic Handle derivation, process delegation,
   selective subtree revocation and explicit OwnerID-independent authorization
   are implemented for the current providers. GE/O Ohand scope/rights and
   Kowner record integrity remain enforced. Cross-owner GE/O reads and writes,
   read-only attenuation, process control and region-writer publication are
   covered by the native QEMU/OVMF fixture `owner-authority.py`. Hardware
   permissions, concurrent revocation fault injection and durable grants are
   not yet proven.
3. **Execution roots:** replace Pgrp/OContext/NamespaceView role lookups
   and old process namespace lifecycle with explicit ObjectSpace roots;
   remove obsolete providers, public ABI, wrappers and tests. No shims.
4. **Persistence:** define and implement GE/O ObjectSpace records and
   atomic graph transactions, persisted identities and trusted reboot
   reification, then remove public storage-SpaceID namespace confusion.
5. **User view and hardening:** PVD, directory/project/container projections,
   multi-parent navigation, quotas, graph GC or specified lifetime semantics,
   recovery/race/fuzz tests, AMD64 physical hardware tests.

A phase is not PASS merely because code was committed. Report actual
build, QEMU, recovery and hardware evidence; mark anything not executed
**NOT RUN**. No stale phase can declare the complete ObjectSpace target.

## Capability lineage phase: current status

The new `Vcap` records form a tree of *grant derivations*, NOT a hierarchy
of objects or ObjectSpaces. One object can have many independent cap roots.
Every installed derived Handle retains its ancestors' lineage nodes; none of
those nodes grants access merely by being an object identifier. Revocation
increments the source node's epoch, invalidating prior descendants without
scanning other processes or graph spaces. Descendants can be closed after
revocation. Later derivation from the same source is allowed.

Full-system acceptance is in `tools/host/capability/` with its own
GitHub Actions workflow. The earlier ObjectSpace-v1 acceptance remains
required. OwnerID-equality checks have been removed after auditing the current
GE/O, device and process Handle entry points. Cross-owner authority is now
tested, including that OwnerID alone is insufficient. The trusted initial
device catalog and boot-volume setup still deliberately require their
existing bootstrap/administration gates. The volatile Handle graph does not
serialize authority on disk or automatically cancel in-flight provider work.
Independent testing of timing races, hardware device control and revocation
during active I/O is still necessary.

## Current OwnerID-independent authorization proof

`tools/host/capability/run-owner-authority.sh` boots a real AMD64 guest
with two registered nonroot OwnerIDs. The test checks positive and negative
capability admission, object-level GE/O read/write and commit, immutable
Kowner preservation, cross-owner process termination and region writer
mapping, and absence of ambient Space or program-registration authority.
The prior OwnerID fixtures retain their corruption/reboot coverage but
now test that delegated Space authority overrides owner equality.
`ObjOpOwnerInfo` and `owner_info` report provenance only.
