# Plan 2001-kernel object and file model — architectural definition

**Status: normative architecture; the volatile-object, ObjectFIFO, TCP-provider, GE-capability and universal five-operation ABI slice is implemented.** This document defines the intended semantics for developers and coding agents. The implemented slice is the RAM-only object identity, ObjectFIFO/TEE, explicit cross-process Handle delegation, generic TCP6/GE provider dispatch and the `CREATE/OPEN/CLOSE/BIND/INVOKE` kernel boundary with libc wrappers described in sections 3.2-3.3 and [VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md); the rest remains proposed. It does not change GE/O on-disk format v1, historical test results, or existing MicroPython persistence. Read [O-FORMAT.md](O-FORMAT.md) for persistent storage and [STATUS.md](STATUS.md) for evidence.

## 1. Identity, object, file, stream

An **object** (Finnish *olio*, English *object*) is an individually identifiable entity. An object may be persistent, volatile, or both during different parts of its life. An **OID** identifies an object within an explicit scope; it is not itself the object, a memory address, a block address, or a pathname. Two objects may contain identical bytes and still have different identities. A persistent object may retain its identity when its content changes through committed versions. A volatile object has RAM-only state and a boot-scoped identity; neither its identity nor its OID implies GE/O storage.

A **file** is the byte-content abstraction of an O object: a finite ordered sequence of bytes, including the empty sequence. A file is **not** a Unix/Plan 9 directory entry, nor the bundle of data plus mandatory operating-system file metadata. It does not require a root directory, pathname, owner, mode bits, timestamps, or an inode-like record.

```text
OID 42
    content = "hello\n"      # six bytes; a file
```

The word *file* may describe an object's content or a descriptive type/category (`file`). It **does not mandate a `File` class, superclass, inheritance tree, exclusive tag, or even a stored `type=file` field**. Byte-bearing objects may have additional interpretations, such as a compiled executable or an image; those interpretations are not mutually exclusive subclasses. The exact representation of type descriptions remains open.

**Properties and relations are distinct from the file's byte content.** Creation provenance, an original creation timestamp (if actually known), executable interpretation, target ABI or optional imported legacy metadata may be attached to an object or expressed as relations. Do not silently copy Unix `stat` fields into every native object or equate import/copy time with original creation time.

A **stream** is a means of accessing or transferring bytes, possibly through one or several ordered channels. It is not a file's identity or a required container. Random-access reads by offset and bounded-length byte transfers remain useful without directory traversal. A logical file need not occupy one contiguous RAM region or one physical disk block.

## 2. Relationships, not a type or directory hierarchy

Native Plan 2001-kernel relationships are intended to form a **graph**. The graph can have many-to-many references, shared nodes, and cycles. No distinguished root directory, parent relation, canonical path, or traversal from `/` defines object identity.

```text
OID 43
    content = <6c binary>       # byte content; usable as a file

OID 100
    implements → OID 43
    purpose    → C compiler
    target     → amd64  # example; the architecture is not fixed by the object model
```

Here `OID 43` is the stored 6c executable bytes. `OID 100` is a distinct semantic object describing a compiler and referring to that implementation. **`implements`, `purpose` and `target` are illustrative relation/property labels, not an implemented edge schema or a Python object hierarchy.** An implementation can be selected or replaced independently of the descriptive object; a loader eventually operates on the selected executable bytes.

Other examples of future relations: origin, generated-by, depends-on, implements-for-execution-environment, tag. Some objects may have no relations. Indexes and bindings are open engineering questions and **must not require reconstructing directory trees**.

An object can be read or addressed directly by identity; a graph lookup is for relationships and discovery, not a mandatory path-walk to reach the bytes.

## 3. Python is a client, never the GE/O ontology

MicroPython is an execution environment and a consumer of persistent objects. It **does not define the native object model**. Specifically:

- Python `class`, subclassing, inheritance, MRO, descriptors, metaclasses, duck typing, module imports and runtime heap objects are Python-language mechanisms; none is a mandatory GE/O classification or storage layer.
- Do not model OIDs as Python class instances in the kernel or copy Python's class/type hierarchy into GE/O. A client-side Python wrapper may exist for convenience but must not determine disk layout, native identity, or graph semantics.
- A graph of native GE/O relationships is not the same as a serialized Python object graph.
- Code and binary content can be addressed as O objects irrespective of the language used to produce them (Plan 9 C, Go, Ada, Python, etc.).

**Current implementation boundary:** GE/O v1 stores versioned opaque byte values identified by `(SpaceID, OID)` within a volume. `OID` is currently 64 bits; a single globally allocated OID across all machines is **not implemented**. Persistent Python objects in today's PoC are serialized together into the `OID_GRAPH` object, not independently stored as native GE/O graph edges. A generic `file` type field, relationship index, and the example above are **proposed**, not working APIs.

### 3.1 Implemented direct byte-object API (PoC)

The first implemented slice of this model is a small binding in the existing built-in `space` module. It stores and fetches one native GE/O published byte value by explicit numeric OID, independently of the Python graph:

```python
import space
space.oput(42, b"hello\n")       # publish and sync six raw bytes
assert space.oget(42) == b"hello\n"
```

- `space.oget(oid)` returns an exact `bytes` value, including embedded NULs; empty content is `b""`. A missing OID raises `OSError`; it is not fabricated as empty.
- `space.oput(oid, data)` accepts bytes-like data, writes it with the native O write and calls the native commit/sync before returning. It does not wait for Python-graph dirty tracking.
- Both require an open native `-O` volume and `-S` Space. Without one they raise `OSError`; there is no pathname or file API fallback.
- Writes reject the implementation-reserved OIDs (the Python graph `OID_GRAPH`, `OID_ENV`, the code/metadata region, and the native-exec test/boot OIDs). Reads of those OIDs are not treated as user API.
- The whole-object read/write limit is 1 MiB, a conservative PoC bound below the kernel's 64 MiB transaction limit.
- `space.oid(x)` still returns a *Python logical graph OID*. Its number may coincide with a native OID, but the two identity spaces are independent: one names an object inside the serialized `OID_GRAPH`; the other names a separate native GE/O `Kobj` record.

OIDs 42, 44-49 in the earlier demonstration are **explicit PoC identifiers**, not global identities. A separate local registration call now allocates a fresh OID in the active native O volume and Space:

```python
import space
a = space.oregister()            # empty payload, fresh local OID
b = space.oregister(b"hello\n")  # different OID even for equal bytes
assert a != b
assert space.oget(a) == b""
```

The allocator's high-water mark lives in the native Space header transaction, not in the Python graph or `space.oid()`. It is **local**: another volume or machine may use the same number for an unrelated occurrence. Remote observation does not register a local OID, and copying bytes elsewhere would be a separate local registration. There is no machine-wide allocator, global UUID, remote protocol or OID migration. See [O-FORMAT.md](O-FORMAT.md) section 5.4 for the concrete encoding and reserved range.

The `implements`/`purpose`/`target` relations of OID 43/100 remain conceptual and are not implemented by this API. No `type=file` field, Python `File`/`object` class, directory, or on-disk v1 change is involved.

Reference: `micropython/p9space.c`.

### 3.2 Implemented volatile-object and ObjectFIFO slice

The kernel now has a RAM-only object path with the same logical operations as
the persistent provider but no GE/O read or write on creation or access.
Identity is **scope-qualified**: `(incarnation, space, oid)`.

- `incarnation` is a per-boot value captured once from the real-time clock;
  it is never reused by a later boot.
- `space` is a caller-supplied scope tag. `0` is the runtime/default scope.
  It can carry a persistent GE/O SpaceID when the caller has one, but the
  volatile path does not consult GE/O to interpret it.
- `oid` is allocated monotonically within that boot and scope, starts at 1,
  and is never reused while the incarnation is alive.

A process-local, generation-tagged **Handle** is the only access path. A
Handle carries independently enforceable rights (`read`, `send`, `receive`,
`meta`, `subscribe`, `control`, `delegate`). A Handle is not an OID, a pointer
or ambient authority. `VOBJINFO` reports the identity; there is deliberately
no raw "look up any OID" call, so knowing a scope-qualified OID grants
nothing.

**ObjectFIFO** is itself a volatile object with message-framed behavior. One
published message is one authorized reference to an immutable byte object
plus an optional opaque envelope (0-64 bytes). The kernel does not parse TCP,
IP, USB, HTTP or application payloads. Every subscription has an independent
bounded queue and cursor (TEE/fanout); overflow drops the oldest queued
reference for that subscription and increments a counter. A slow subscriber
therefore cannot block a fast subscriber or the sender. A subscription has at
most one receiver at a time; independent consumers use independent
subscriptions.

The ABI is syscalls 121-131 in `runtime/abi/sys.h`, with fixed-width types in
`runtime/abi/vobj.h`. Payloads are immutable snapshots. Publishing a FIFO
reference to another FIFO is rejected, so a transferred reference cannot
create an ambient-authority chain. On exec/exit every Handle is dropped; no
Handle is inherited by OSPAWN, so authority is default-denied. `VOBJREVOKE`
closes a FIFO for all subscribers, wakes blocked receivers and drains queued
references; it does not invalidate unrelated object Handles.

Persistent-provider interoperation in this slice is an explicit **clone**:
bytes read from a GE/O object under its existing O authorization can be
published as a new volatile object with new provenance. Same-identity
promotion/binding of a live volatile object to GE/O is **not implemented**
and is reported as unsupported rather than silently remapping an OID. GE/O v1
disk bytes are unchanged.

See [VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md) for the full
contract, limits, error paths and the QEMU evidence.
**Continuation: one capability family for RAM, GE/O, FIFO, TCP6 and UDP6.**
`VOBJOPEN` creates TCP listeners/connections and GE capabilities under the
same Handle and rights model as volatile bytes and ObjectFIFO. `VOBJIO`
dispatches read/write to the suitable provider, `VOBJACCEPT` produces a
connection object, and `VOBJLIST` enumerates a process's Handles. `OSPAWN`
flags=2 explicitly delegates selected Handles to a child (`struct Oinheritv`);
there is no PID/handle-number ambient authority and no implicit inheritance.
A real TCP connection object can therefore be sent through an ObjectFIFO,
outlive the accepting process and be served by another process. A GE
capability preserves the persistent SpaceID/OID and reads through the
existing GE provider; it is not a new volatile copy. `OBJOPEN
(ObjOpenUdp6)` binds a `VkindUdp6` endpoint whose `Vread`/`Vwrite` rights cover
receive/send, and `OBJINVOKE(SendTo/RecvFrom)` transfers one bounded datagram.
The legacy `NET6*` syscalls (116-120) are RESERVED and dispatch to nothing;
the same UDP6 provider and Handle table are reached only through the OBJ*
calls, with `ObjOpNet6Addr=24` providing the authorized address lookup and
the non-transferable boot/permit gate unchanged. Same-identity volatile-to-GE promotion remains blocked. See
[VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md) sections 3.3, 7 and
8.1.

### 3.3 Implemented universal five-operation object ABI

The canonical **object** boundary is the five conceptual operations below, with
fixed-width definitions in `runtime/abi/obj.h` and numbers 136-140 in
`runtime/abi/sys.h`. **These five operations are not the entire syscall ABI.**
T7 restored direct calls for simple process-local operations (`BRK_(24)`,
`SLEEP(17)`, `NOTIFY(28)`, `NOTED(29)`, `RFORK(19)`, `OTERMINATE(93)`,
`EXITS(8)`), and the current observed table is 135 named / 62 active / 73
reserved. “Exactly five” describes the universal object family, not a total
syscall quota.

| Operation | Effect |
| --- | --- |
| `CREATE` | allocate an independent object in a Space and return a Handle; empty or volatile storage is allowed and no GE/O commit is implied |
| `OPEN` | resolve an existing authorized selection (TCP, GE, another Handle, or a named binding) with equal or attenuated rights; never grant from an OID alone and never walk a path |
| `CLOSE` | release this process's reference; the object may survive through other Handles, messages or bindings |
| `BIND` | establish named visibility for an authorized Handle; it changes visibility, not storage identity, FIFO subscription or packet routing |
| `INVOKE` | validate and route one typed request to the provider: read/write, publish/subscribe/receive, accept, grant, revoke, query |

**Keep the layers distinct.** Object identity is
`(incarnation, space, oid)` for a volatile object or
`(VolumeUUID, SpaceID, OID)` for a persistent GE/O object. A Handle is a
process-local generation-tagged reference with rights. A capability is an
authorized reference that may cross a process boundary. The provider backing
is RAM bytes, GE/O bytes, a TCP endpoint, a UDP6 endpoint, a USB metadata
service or ObjectFIFO state. A binding is a
name in a process namespace. None of these concepts substitutes for another,
and object existence does not depend on RAM, GE/O persistence, a device or
namespace visibility. GE/O is a persistence provider; the GE/O v1 disk format
and its OIDs are untouched.

**Public ergonomics are a libc/libo layer, not extra syscalls.** The running
wrappers in `runtime/libc/port/oobject.c` are `obj_create`, `obj_open`,
`obj_close`, `obj_bind`, `obj_invoke`, plus `obj_read`, `obj_write`,
`obj_receive`, `obj_publish`, `obj_subscribe`, `obj_accept`, `obj_grant`,
`obj_revoke`, `obj_list`, `obj_listen`, `obj_connect`, `obj_udp6_open`,
`obj_udp6_sendto` and `obj_udp6_recvfrom` as INVOKE adapters.
`OPEN` is not POSIX pathname `open`; `READ` is not universally a byte-stream
`read`. A READ of bytes/stream returns a byte count; a RECEIVE from an
ObjectFIFO returns one authorized receiver-local Handle plus an optional small
opaque envelope.

**ObjectFIFO is an OID object transporting ordered, complete authorized
object references.** It can carry RAM, GE/O, TCP and future USB/network
objects; it is not a byte stream and not an IP/NetFrame-specialized channel.
Each subscription has an independently bounded TEE queue, attenuated rights,
revocation and its own lifetime. The core does not require a type enumeration
to carry a reference and does not inspect the payload.

**Creation, open, binding and persistence are distinct.** `CREATE` allocates
an object; `OPEN` selects an existing authorized one; `BIND` publishes a name
for one; none of them silently commits to GE/O or copies across Spaces. A GE
object is opened as a capability that preserves its persistent identity. A
same-identity volatile-to-GE promotion remains a documented blocker and is not
faked by cloning or rewriting an OID.

The legacy `VOBJ*` (121-135), `TCP6*` (108-113) and `NET6*` (116-120)
families are reserved and dispatch to nothing; UDP6 goes only through the OBJ
provider, including `ObjOpNet6Addr=24`. No family is renumbered. The new tests use the new family;
the old paths remain verified.

## 4. Legacy files are imported as bytes, not as namespaces

The Plan 9/9front tree and Unix trees are **inputs to migration**, not templates for Plan 2001-kernel's native information model.

- Import concrete ordinary files and their bytes as O objects; a binary, source file or text file remains useful as a file abstraction.
- Plan 9 `bind`, `mount`, per-process directory views, `walk` and the synthetic root namespace are **not migrated** into GE/O.
- Resolve ambiguous tool names using explicit source paths during import (for example, the amd64 build of `6c`). An original path may be retained as import provenance or in a temporary mapping, but it is not a native OID.
- Build tooling such as a prospective `mk.py` may use numeric OID lists and resolved byte locations/cached handles during migration. A physical disk address is not a durable OID; relocation must not silently change identity.
- Special dynamic Plan 9 devices, `/proc` and other generated views are not automatically imported as immutable byte files. Their behavior requires separate analysis.
- Native Plan 2001-kernel namespaces and discovery will be designed afresh, **after** the core object and relation semantics are understood. Do not predesign them to reproduce Unix/POSIX/9P directories.

These are migration **principles**, not a claim that `mk.py`, a legacy importer, or a new namespace has already been implemented.

## 5. Non-goals and agent guardrails

1. Do not attempt to abolish files, byte blocks, byte streams, random-access reads or executable binaries.
2. Do not define an implicit hierarchical directory root, pathname-based identity, or an operating-system `stat` record as part of every object.
3. Do not insert Python class inheritance, `File` base classes or other object-oriented type hierarchies into the native storage contract.
4. Do not add an external/string OID layer or change the current 64-bit `OID` or GE/O v1 disk format for this conceptual clarification.
5. Do not invent a finished native graph, universal metadata schema, remote protocol, loader, importer or build pipeline; clearly label unimplemented ideas.
6. Do not rewrite historical PoC results when the implementation changes. Preserve test evidence separately from the target model.

**Core distinction:** a file is bytes belonging to an identifiable object; a filesystem hierarchy is a separate, unnecessary organizational abstraction.
