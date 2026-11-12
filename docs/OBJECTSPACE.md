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

The earlier `process.owner == object.owner` general gate is superseded
as a target, though it still exists across the current GE/O, device,
region and process providers. Those gates must be migrated explicitly,
not removed blindly. OwnerID stays immutable for trusted program identity,
boot policy and lifecycle. Only an explicit delegator may attenuate and
transfer capabilities; no OID, label or persisted metadata mints power.

`cap_derive` reduces rights without creating new access. `cap_revoke`
must have explicit lineage semantics separate from removing a binding;
a full generic revocation mechanism is **NOT IMPLEMENTED**.

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
2. **Capability security:** define generic derivation/revocation lineage,
   replace OwnerID-equality gates across GE/O, region, device and process
   providers; retain OwnerID as provenance. Negative cross-owner tests
   must prove only delegated rights work.
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
