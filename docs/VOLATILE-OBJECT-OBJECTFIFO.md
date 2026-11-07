# Volatile objects, ObjectFIFO and TCP6 event delivery

**Status: capability-fix candidate, 2026-11-05 (task
`9b85e5c5208a4a3b87ec2f22e12443f7`).**
The slice retains the canonical `CREATE/OPEN/CLOSE/BIND/INVOKE` kernel
boundary (syscalls 136-140) and libc/MicroPython adapters over it. This round
hardens the same core: ObjectFIFO delivery rights are bounded by the sender's
capability, subscriptions cannot cross OPEN/BIND/GRANT/OSPAWN, derived GE/O
capabilities share revocation lineage, CreatePersist allocates no durable OID
when the Handle table is full, and native/GE bindings share one pgrp/OContext
entry. It is still a reviewable candidate, not a declaration that the tree is
the owner's new HEAD.

## Capability-fix candidate (2026-11-05)

Implemented and demonstrated in this round:

- A published message stores the sender's immutable rights mask as an upper
  bound. The received Handle gets `Vread|Vmeta` plus only the optional rights
  the subscription requested and the sender held (`Vwrite`, `Vaccept`,
  `Vconnect`, `Vdelegate`, `Vcreate`, `Vcommit`). A sender cannot receive
  rights it did not hold, and a subscription cannot add rights beyond its
  requested policy.
- Single-receiver subscriptions cannot be duplicated through OPEN, BIND,
  GRANT or OSPAWN. OSPAWN also requires `Vdelegate` on each delegated Handle.
- The receive path reserves the result Handle and its table slot before it
  removes a message, so Handle-table exhaustion cannot reinsert into a queue
  that may have been revoked or overflowed. No allocation or fallible
  publication happens after dequeue.
- FIFO destruction clears each subscription's `fifo` pointer under the FIFO
  lock; the previous code could leave a dangling `Vfifo*` when a subscription
  Handle outlived the FIFO attachment.
- Queue mutation remains bounded: `Vfifo.lk` is a QLock for allocations and
  blocking-free setup, the FIFO -> subscription lock order is unchanged, and
  wakeups happen after the subscription lock is dropped.

Deterministic tests covered TEE, envelope, drop-oldest, dead subscriber,
derive, revoke, a 32-slot Handle cap (the 33rd live Handle fails without a
durable allocation), namespace COPY/SHARE, and a two-process object/TCP
handoff through one ObjectFIFO. See [STATUS](STATUS.md) and
[TEST-RESULTS.json](TEST-RESULTS.json). Long fuzz, external IPv6 beyond the
local UDP-encapsulated peer, dedicated allocation fault injection beyond the
Handle cap, and physical hardware remain **NOT RUN**.
**Source baseline:** the working tree derived from read-only snapshot
`eed6f23e502737aa1589b844b25c028fe32d032429cfd711ecbb8b4d0c710ad5`
(808 files, 2026-11-02 10:58:59Z); the snapshot and the tree had no modified
file differences before this change. This is a reviewable candidate, not a
declaration that the tree is the owner's new HEAD.

This document distinguishes **implemented** (visible in the current source),
**demonstrated** (a named test was executed) and **proposed/blocked**. It does
not change GE/O format v1, the existing persistent object bytes, or the
existing TCP6/UDP6 byte-stream semantics.

## 1. Universal object concept

An object has an identity and may have zero or more bytes of content and/or
behavior. Identity does not imply persistence, residence, ownership or a
class. A byte file, a FIFO endpoint, a TCP connection and a persistent GE/O
record are clients of the same model; they are not mandatory classes and the
kernel does not add an object-oriented type hierarchy.

The kernel now distinguishes:

| Concept | Meaning | Status |
| --- | --- | --- |
| persistent GE/O object | versioned bytes identified by `(VolumeUUID, SpaceID, OID)` in v1 storage | EXISTING, unchanged |
| volatile object | RAM-only state with a boot-scoped identity | IMPLEMENTED |
| ObjectFIFO | volatile object with message-framed reference transport | IMPLEMENTED |
| subscription | independent receiver attachment to one ObjectFIFO | IMPLEMENTED |
| Handle | process-local, generation-tagged, rights-bearing reference | IMPLEMENTED |
| binding | named visibility of one authorized Handle in a process namespace | IMPLEMENTED (one pgrp/OContext entry; optional GE `Ohand` plus `Vhand`) |

**Identity is independent of backing and visibility.** A Space-qualified OID
does not by itself imply RAM residency, GE/O persistence, a device or TCP
resource, or namespace visibility. GE/O is one *persistence provider* for an
object; it is not the definition of object existence. The old GE/O v1 disk
format, its persistent OIDs and its bytes are unchanged.

These are deliberately distinct concepts and must not be collapsed in code or
documentation:

```text
identity        (incarnation, space, oid) or persistent (VolumeUUID, SpaceID, OID)
Handle          process-local generation-tagged reference with rights
capability      an authorized reference that may cross a process boundary
provider backing RAM bytes | GE/O bytes | TCP endpoint | ObjectFIFO state
binding         a name for an authorized Handle in a process namespace
```

The volatile incarnation prevents a stale reference from aliasing a later
boot's object. A GE/O identity keeps its persistent Volume/Space/OID across
boots because it is a different provider.

### 1.1 Scope-qualified identity

A volatile identity is:

```text
(incarnation, space, oid)
```

- `incarnation` is captured once per boot from the real-time clock. It is not a
  GE/O identifier and is never reused by a later boot.
- `space` is a caller-supplied 64-bit scope tag. `0` is the runtime/default
  scope. It may carry a persistent GE/O SpaceID when the caller already has
  one, but the volatile path does not read GE/O to interpret it.
- `oid` starts at 1, increases monotonically for the boot, and is never reused
  while the incarnation is alive.

The implementation does **not** expose a raw "resolve any OID" syscall. A
volatile object is reached through a Handle; `VOBJINFO` reports its identity.
Knowing an identity therefore conveys no authority. This satisfies the rule
that OID lookup must not bypass capability.

Across the two-boot test the allocator produced `oid=1` in both boots while
the incarnation changed from `1793744450319218491` to
`1793744476301549055`. The complete identities differ, so a stale reference
from boot one cannot resolve to boot two's object. No Handle or identity is
transported across a reboot, so this is a non-aliasing guarantee rather than a
cross-boot persistence feature.

## 2. Memory model and lifetime

- Creation copies the caller's bytes into a kernel-owned immutable snapshot.
  Zero-length objects are valid. No GE/O read, write, commit or cache flush is
  performed by `VOBJCREATE`, `VOBJREAD`, the FIFO operations or revocation.
- Payload is read-only after creation. There is no mutable-shared-payload
  path, so a published reference cannot be used to mutate another holder's
  view. A future versioning operation would create a new object.
- Every Handle, queued message and FIFO/subscription attachment holds a
  reference. The object is freed after the last reference is released.
- A subscription is detached when its Handle is closed or the FIFO is
  revoked; queued messages are released and blocked receivers are woken.
- `VOBJREVOKE` closes the FIFO for all subscribers and wakes every blocked
  receiver. It does not invalidate independent byte-object Handles.
- Exec and process exit drop all Handles (`vobjprocexec`/`vobjprocexit`).
  Handles are not inherited across `OSPAWN`; authority is default-denied.
- Stale Handle generations fail closed with `Ebadarg`. A close removes only
  the exact table slot and generation that was validated under the table lock.

### 2.1 Hard bounds

These are implementation limits, not an ABI guarantee:

| Limit | Value |
| --- | --- |
| Handles per process | 96 (`Vhandmax`, observed in T11) |
| live volatile objects | 4096 |
| volatile payload bytes | 32 MiB |
| payload bytes per object | 1 MiB |
| FIFOs | 256 |
| subscriptions per FIFO | 64 |
| queued messages globally | 65536 |
| per-subscription queue | 1..1024 (default 64) |
| opaque envelope per message | 0..64 bytes |

Allocation failure fails the operation with `Enomem`; a send reserves all
message nodes before publishing, so a fanout is not partially delivered when
memory is exhausted. When a node cannot be allocated the FIFO is unchanged.

## 3. Userspace ABI

Fixed-width definitions are in `runtime/abi/vobj.h`; syscall numbers are in
`runtime/abi/sys.h` and take effect through the generated kernel table and
MicroPython stubs.

| Number | Operation | Arguments |
| --- | --- | --- |
| 121 | `VOBJCREATE` | `(uvlong space, void *bytes, long len, uvlong flags, VobjId *out)` -> Handle |
| 122 | `VOBJREAD` | `(int handle, void *buf, long len)` -> bytes copied, or size when `len==0` |
| 123 | `VOBJINFO` | `(int handle, VobjInfo *out)` -> 0 |
| 124 | `VOBJCLOSE` | `(int handle)` -> 0 |
| 125 | `VOBJFIFOCREATE` | `(long capacity, uvlong flags)` -> Handle |
| 126 | `VOBJFIFOSUB` | `(int fifo, uvlong rights, long capacity)` -> subscription Handle |
| 127 | `VOBJFIFOSEND` | `(int fifo, int object, void *envelope, long len)` -> 0 |
| 128 | `VOBJFIFORECV` | `(int subscription, void *env, long envlen, int timeout_ms, long *got)` -> object Handle |
| 129 | `VOBJFIFOSTAT` | `(int handle, VobjFifostat *out)` -> 0 |
| 130 | `VOBJGRANT` | `(int handle, uvlong rights, uvlong flags)` -> attenuated Handle |
| 131 | `VOBJREVOKE` | `(int fifo)` -> 0 |
| 132 | `VOBJIO` | `(int handle, uvlong op, void *buf, long len, long timeout)` -> result; `op` is `VioRead=1` or `VioWrite=2` |
| 133 | `VOBJLIST` | `(VobjListRec *buf, long n)` -> total Handles; copies up to `n` records in slot order |
| 134 | `VOBJOPEN` | `(uvlong provider, void *req, long reqlen, uvlong rights, VobjId *out)` -> Handle |
| 135 | `VOBJACCEPT` | `(int listener, int timeout_ms)` -> connection Handle |

`flags` are zero in this slice. `timeout_ms` is `< 0` for an infinite wait,
`0` for non-blocking, and `> 0` for a bounded wait.

### 3.1 Rights

| Bit | Name | Effect |
| --- | --- | --- |
| `1<<0` | `Vread` | read immutable object bytes |
| `1<<1` | `Vsend` | publish references into a FIFO |
| `1<<2` | `Vrecv` | receive references from a subscription |
| `1<<3` | `Vmeta` | query identity and metrics |
| `1<<4` | `Vclose` | reserved; close is always allowed |
| `1<<5` | `Vsubscribe` | create an independent subscription |
| `1<<6` | `Vcontrol` | revoke the FIFO |
| `1<<7` | `Vdelegate` | create attenuated Handles |
| `1<<8` | `Vwrite` | write stream bytes or GE content where the provider allows |
| `1<<9` | `Vaccept` | accept a connection from a listener object |
| `1<<10` | `Vconnect` | reserved for a connection-provider open |

A byte Handle created by `VOBJCREATE` receives
`Vread|Vmeta|Vsend|Vdelegate`. A FIFO owner Handle receives
`Vsend|Vsubscribe|Vmeta|Vcontrol|Vdelegate`. Subscription creation requires
`Vsubscribe` on the parent. The subscription Handle carries the requested
receive-policy mask (`Vrecv|Vmeta` plus optional `Vread`, `Vwrite`, `Vaccept`,
`Vconnect`, `Vdelegate`, `Vclose`, `Vcreate`, `Vcommit`); `Vsend` is accepted
only when the parent FIFO Handle already carries it. `Vcontrol` and
`Vsubscribe` are not requestable, so a subscription Handle cannot become a
revocation or subscription-creation authority. The optional class bits only
mask rights on received object Handles; they never add a right, because the
grant is intersected with the sender's object-Handle rights (T11 observed this
behavior). Granting requires `Vdelegate`, and the requested rights must be a
subset of the grantor's rights. Subscription Handles cannot be duplicated by
`VOBJGRANT`; a new independent subscription is the only way to add a reader.

### 3.2 Error model

| Condition | Error |
| --- | --- |
| invalid/stale Handle, wrong kind, bad size | `Ebadarg` |
| missing Handle right | `Eperm` |
| allocation/quota failure | `Enomem` |
| receive with `timeout==0` and empty queue | `vobj: receive would block` |
| receive timeout | `vobj: receive timed out` |
| receive/send on closed or revoked FIFO/subscription | `Ehungup` / `vobj: FIFO is closed` |
| publish a FIFO reference | `vobj: FIFO references are not transferable` |
| publish the FIFO to itself | `Ebadarg` |
| duplicate close / stale generation | `Ebadarg` |
| subscription limit | `vobj: FIFO subscription limit` |
| handle table full | `vobj: too many handles` |

No kernel pointer is returned to userspace. Handles are integers encoding a
slot and a nonzero generation.

### 3.3 Unified provider dispatch and cross-process delegation

The four object kinds use one Handle/rights/close/info/delegate family:

| Kind | Created by | `VOBJIO` | Other |
| --- | --- | --- | --- |
| `VkindBytes` | `VOBJCREATE` | read immutable bytes; write returns unsupported | `VOBJREAD` remains a byte convenience |
| `VkindFifo` | `VOBJFIFOCREATE` | unsupported; use FIFO send/receive | subscriptions/TEE/revoke |
| `VkindTcp` | `VOBJOPEN(VopenTcpListen/Connect)` | stream read/write with timeout | `VOBJACCEPT` on a listener |
| `VkindGe` | `VOBJOPEN(VopenGe)` | read/write persistent object bytes | identity is the persistent OID |

Provider dispatch is deliberately explicit. `VOBJIO` returns unsupported for
an operation a kind cannot provide; it does not invent a universal payload
schema. `VOBJINFO` carries `provider`, `abi_version`, and the 16-byte volume
UUID for a GE capability. `VkindTcp` covers both listeners and connections;
a listener accepts, a connection reads/writes, and the wrong operation fails.

**TCP6 is now a provider, not a separate user ABI.** `VOBJOPEN` creates a
listener or connection object; `VOBJACCEPT` returns a connection object; and
`VOBJIO` transfers stream bytes. The legacy `TCP6LISTEN/CONNECT/ACCEPT/READ/
WRITE/CLOSE` (108-113) remain compatibility shims that call the same
`vtcp_open`/`vtcp_accept`/`vtcp_read`/`vtcp_write` core and return the same
kind of Handle. They are retained until the compatibility condition in
section 8.1 is met; no new protocol-specific syscall was added.

**Cross-process delegation is explicit.** `OSPAWN` flags bit 2 appends a
fixed `struct Oinheritv` naming up to eight parent Handles and optional
rights. With `rights==0` the child receives the parent's rights; otherwise
the requested rights must be a subset. The kernel installs new child Handles
in the listed order and the child can discover them with `VOBJLIST`. There is
no implicit inheritance by PID, handle number, `rfork`, or exec. A child that
predicts a handle number still has no handle: generations and the parent's
table are not shared. `VOBJGRANT` is the parent-side attenuation primitive.

**A TCP connection object survives its accepting process.** A message stores a
real Vobj reference, not a Proc-owned token. The Tendpoint is owned by the
Vobj refcount. The accepting process may exit after enqueue; the receiving
process's message/Handle keeps the connection and its `Conv` alive. Closing
the last Handle releases the endpoint. This was demonstrated with worker A
exiting and process B serving the HTTP response.

**GE identity is preserved without a clone.** `VOBJOPEN(VopenGe)` validates
the caller's existing O Space Handle and makes an independent capability copy
of that Space reference (`oscopycap`). The copy keeps the same SpaceID/OID,
carries a private Ospace handle, and is marked capability-authorized: it still
checks `revoked` and the requested `Oread`/`Owrite` rights, but does not
repeat the source process's view-membership check. `VOBJIO` then calls the
GE provider directly. The bytes are the persistent object's bytes, not a new
volatile copy. A capability already delegated to another process is not
retroactively revoked by later namespace edits; it is revoked by closing its
Handles. This limitation is explicit.

### 3.4 Universal five-operation object boundary

The canonical **object** boundary is the five conceptual operations below. They
are not the entire syscall ABI. Fixed-width types are in `runtime/abi/obj.h`;
numbers 136-140 are in `runtime/abi/sys.h`. T7 restored direct calls for simple
process-local operations and the current observed table is 135 named / 62
active / 73 reserved. “Exactly five” describes the universal OBJ family, not a
total syscall quota; see [ROADMAP-T8-T16](ROADMAP-T8-T16.md). The older `VOBJ*`
(121-135) family is a compatibility shim over the same core.

| Number | Operation | Arguments | Meaning |
| --- | --- | --- | --- |
| 136 | `OBJCREATE` | `(uvlong space, uvlong flags, void *bytes, long len, ObjId *out)` -> Handle | create an independent empty or byte object in a Space; no implicit GE/O commit |
| 137 | `OBJOPEN` | `(uvlong provider, void *req, long reqlen, uvlong rights, ObjId *out)` -> Handle | open an existing authorized selection; never grant from an OID alone |
| 138 | `OBJCLOSE` | `(int handle)` -> 0 | release this process's reference, not necessarily the object |
| 139 | `OBJBIND` | `(int handle, char *role, long rolelen, uvlong rights, uvlong mode)` -> 0 | establish named visibility; not storage identity and not a FIFO subscription |
| 140 | `OBJINVOKE` | `(int handle, ObjCall *call, long calllen)` -> provider result | one typed request router over READ/WRITE/RECV/SEND/ACCEPT/CONTROL/QUERY |

`OBJOPEN` providers are `ObjOpenTcpListen=1`, `ObjOpenTcpConnect=2`,
`ObjOpenGe=3`, `ObjOpenHandle=4` and `ObjOpenBinding=5`. Provider 4 duplicates
one process-local Handle with an equal or attenuated rights subset. Provider 5
resolves a named binding in the caller's namespace; it performs **no path
walking** and no cross-Space copy.

`OBJBIND` modes are `ObjBindCopy=0`, `ObjBindExclusive=1`, `ObjBindShare=2`
and `ObjBindRemove=0x100`. A role is a bounded ASCII label, never a path or a
directory component. `OBJBIND` and the native O view share **one**
pgrp/OContext entry. For a GE-backed object that entry contains the attenuated
`Ohand` and the `Vhand`; for a volatile object it contains the `Vhand` alone.
`OLOOKUP`, view copy/share and revocation therefore observe the same binding.

`OBJBIND` is not a transaction that can half-succeed: COPY and SHARE remove an
existing same-name entry and install the new one at one spinlocked
linearization point; EXCLUSIVE rejects an existing role; Remove clears the
entry. There is no separate mirror table and no partial-mirror rollback state.
For every object kind, Share currently has the same role-replacement effect as
Copy; true union/composition semantics remain a roadmap item. An external
`OUNBIND` removes the same unified entry, so it also removes a native
object binding of that name. Subscription Handles cannot be duplicated, bound,
granted or passed through OSPAWN, preserving the one-receiver-per-subscription
rule.

**The actual minimal ABI is not POSIX.** `OBJOPEN` selects a capability or a
namespace binding but does not walk a pathname. `OBJINVOKE` carries a typed,
versioned `ObjCall`; it is not a universal `read(fd, void*, n)` byte stream.
A READ of a byte/stream provider returns a byte count. A RECEIVE from an
ObjectFIFO returns **one authorized object Handle** plus an optional small
opaque envelope; the two return types are intentionally different.

The `ObjCall` operations are `Info`, `Read`, `Write`, `Stat`, `Send`, `Recv`,
`Subscribe`, `Accept`, `Grant`, `Revoke` and `List`. Authority is always
derived from the Handle and its rights; the caller's `ObjCall.rights` field is
reserved and must be zero. Unknown operations fail with
`obj: operation not supported`, and a version mismatch fails with
`obj: unsupported ABI version`.

**Public ergonomics are libc/libo adapters over INVOKE.** The running C API in
`runtime/libc/port/oobject.c` is:

```c
handle = obj_create(space, ObjKindBytes, data, len, nil);
handle = obj_open(ObjOpenTcpListen, &req, sizeof req, rights, nil);
n      = obj_read(handle, buf, len, timeout_ms);      /* bytes */
handle = obj_accept(listener, timeout_ms);
obj_bind(handle, "role", strlen("role"), rights, ObjBindCopy);
obj_close(handle);
```

The MicroPython bindings `plan9.obj_create`, `obj_open_*`, `obj_bind`,
`obj_read`, `obj_write`, `obj_publish`, `obj_subscribe`, `obj_receive`,
`obj_accept`, `obj_grant`, `obj_revoke`, `obj_list` and `obj_close` call these
libc wrappers, not `VOBJ*` or `TCP6*`.

**Compatibility and removal.** `NET6*` 116-120 is RESERVED as of phase 4:
`nil` dispatch, `sysreserved=1` and no handler symbols. The `VkindUdp6`
provider and process `Vhand` table are used only through
`OBJOPEN(ObjOpenUdp6)`, `OBJINVOKE(ObjOpNet6Addr=24/SendTo/RecvFrom)` and
`OBJCLOSE`. `VOBJ*` 121-135 and `TCP6*` 108-113 are reserved too. No legacy
number is deleted or renumbered.

**Limits are unchanged and bounded.** 96 process Handle slots, at most 4096
live volatile objects, 32 MiB volatile payload, 256 FIFOs, 64 subscriptions
per FIFO, 65536 queued messages globally, and a 1 MiB single-call payload cap.
These are implementation limits, not ABI promises.

## 4. ObjectFIFO semantics

1. One published message is one ordered, complete authorized **object
   reference** plus an optional small opaque envelope. It is not a byte
   stream and not an IP/NetFrame-specialized channel. A reference can name
   RAM bytes, a GE/O capability, a TCP listener/connection or a future
   USB/network object; the core does not require a type enumeration to carry
   it and does not parse the envelope or any network protocol.
2. Publication is per-producer ordered by enqueue. Every active subscription
   receives an independent appended reference. The two queues are not a single
   shared cursor.
3. Each subscription is bounded and uses drop-oldest overflow with a
   `dropped` counter when full. A slow subscription therefore cannot exert
   backpressure on the sender or on other subscriptions. There is no
   sender-side blocking.
4. Receive blocks on that subscription's rendezvous with an optional timeout.
   `VOBJREVOKE`, `VOBJCLOSE` of the subscription, FIFO teardown and process
   exit wake the receiver. No busy-poll is used. At most one receiver owns a
   subscription's receive QLock at a time; separate consumers use separate
   subscriptions, so a publish cannot be left asleep for a second receiver.
5. A received message creates a new receiver-local Handle for the referenced
   object with `Vread|Vmeta`, plus `Vwrite`, `Vaccept`, `Vconnect` or
   `Vdelegate` where delivered by the subscription policy. The message
   attachment is released when the Handle is closed. Closing the original
   producer Handle does not invalidate the message or other authorized
   Handles. The referenced object's identity is unchanged by the handoff.
6. FIFO references are not transferable. A receiver cannot acquire the
   referenced FIFO's control behavior through a message. Publishing the FIFO
   to itself is rejected.
7. Revocation and lifetime are per FIFO and per subscription. `VOBJREVOKE`
   closes the FIFO for all subscriptions, drains queued references and wakes
   receivers; it does not invalidate unrelated object Handles.

### 4.1 Locking and concurrency audit

| State | Lock |
| --- | --- |
| process Handle table | `Vhtab.lk` |
| FIFO subscription list, FIFO closed, counters | `Vfifo.lk` |
| subscription queue, `len`, `closed`, `dropped` | `Vsub.lk` |
| one receiver per subscription | `Vsub.recvlk` (QLock) |
| global object/byte/FIFO/message quotas | `Vstats.lk` |
| identity counters | `vidlock`, `vgenlock` |

Lock order is FIFO `lk` -> subscription `lk`; the receiver path never takes
the FIFO lock while holding the subscription lock. `wakeup()` is called while
holding the FIFO lock only after the subscription lock is released; the
condition function reads only the subscription's `len`/`closed` and takes no
locks, matching the existing `Rendez` contract. Handle lookup takes only the
table lock and increments the Handle reference before releasing it. No
sleep/blocking allocation is performed while holding `Vfifo.lk`; send nodes
use non-waiting `mallocz` and are pre-reserved.

The concurrency argument is an audit plus the executable tests below; a
stress campaign with simultaneous multi-CPU send/close/recv is **NOT RUN**.

## 5. Capability boundary

- Possessing a Handle is the only access path. There is no ambient OID lookup.
- Rights are checked on create, subscribe, send, receive, stat, grant and
  revoke.
- A sender must hold `Vsend` on the FIFO and a valid Handle for the published
  object. A receiver obtains the object only through a subscription that the
  FIFO owner authorized.
- A subscription Handle carries only the requested receive-policy mask.
  `Vsend` requires the parent FIFO Handle to carry `Vsend`; `Vcontrol` and
  `Vsubscribe` are not requestable, and class bits never add rights beyond
  the sender's object Handle. `VOBJGRANT` only attenuates.
- Exec/exit drop all Handles; `OSPAWN` does not inherit them. There is no
  implicit delegation from possessing an OID or a message.
- Revocation and stale generations fail closed.
- Rights and subscriptions expose no unauthorized producer identity or data.

## 6. GE/O coexistence and the blocked same-identity promotion

The existing GE/O path is untouched: `OVOLOPEN`, `OSPACE*`, `OEXECUTE`, the
v1 disk format and the persistent object bytes remain as they were. The
volatile store is a separate RAM provider.

**Implemented interoperation (clone/snapshot).** A process with an ordinary
GE/O read authorization can read the persistent bytes through its existing O
Space Handle and pass those bytes to `VOBJCREATE`; the new volatile object has
a new scope-qualified identity and provenance. The QEMU test writes native
OID 42 with `space.oput(42, b"persist")`, reads it with `space.oget(42)`,
publishes the snapshot through an ObjectFIFO, and verifies the received
bytes and envelope. This is explicitly a **clone**, not persistence of the
original volatile identity.

**Blocked:** binding a live volatile identity to a GE/O backing so the same
logical object keeps that identity across reboot. This needs an atomic
identity-to-backing registry and a recovery rule for interrupted promotion.
No such registry is implemented. The system reports an honest unsupported
operation rather than remapping one OID onto another or calling a clone
"persist".

## 7. TCP6 integration

TCP6 stream semantics are unchanged. The implemented integration is the
allowed userspace-service bridge, not a TCP-specific FIFO data structure and
not a second TCP/IP stack:

1. A user process owns a real TCP6 listener/connection through the existing
   `TCP6LISTEN/ACCEPT/READ/WRITE/CLOSE` ABI.
2. After a connection is accepted, the process publishes a volatile event
   object (for example the bytes `accept` plus the envelope `conn`) into a
   general ObjectFIFO.
3. A subscriber receives the event through blocking `VOBJFIFORECV`, then the
   same process reads the genuine TCP6 request and writes an HTTP/1.1
   response. TCP remains a byte stream; message boundaries exist only in the
   ObjectFIFO envelope.

The continuation test is a true two-process handoff. Process A is an
OSPAWN child that receives a listener Handle and an attenuated FIFO sender,
accepts a genuine IPv6 TCP connection, publishes the GE capability and then
the connection object through the ObjectFIFO, and exits. Process B is the
parent: it receives the same GE object (and reads `b"persist"` through the
persistent OID) and the TCP connection object, then reads
`b"GET / HTTP/1.1..."` and writes `HTTP/1.1 200 OK` with `Content-Length: 2`.
`space.wait()` observed A's successful exit before B served, so the connection
survived the accepting process. The host peer confirmed the full response.

Evidence: `build/qemu-o/u2-final4/serial-boot.log` (WORKER 17, GE b'persist',
WAIT (17, ''), REQ, SENT 40, INTERPROC TEST PASS) and
`build/vobj-test/peer-final4.log` (TCP PEER PASS). The pre-continuation
single-process TCP test remains green at `build/qemu-o/u2-tcp-old3/`; the
legacy `TCP6*` shims were exercised there.

**Honest limitation:** direct native TCP readiness subscription and
cross-process transfer of a TCP endpoint Handle are **not implemented**. The
bridge process owns the TCP Handle; a separate consumer receives the event
object, not the TCP capability. This is documented rather than faking a
transfer. No kernel pointers or raw TCP tokens are copied to another process.

## 7.1 Partial and blocked items

These are implemented only to the stated extent; they are **not** claimed
complete:

- **Native namespace binding:** `VOBJ*` Handles are delegated/attenuated with
  `VOBJGRANT` and OSPAWN, but a volatile/FIFO/TCP object cannot yet be
  `OBIND`-bound into an O view or looked up by the NS role machinery. The
  existing NS/O views are unchanged.
- **Cross-process Handle transfer:** implemented only through explicit
  `OSPAWN` flags=2 (`Oinheritv`); there is no standalone post-spawn handoff
  API and no `rfork` inheritance. Receiver policy is the child's explicit
  spawn list plus per-Handle rights; no PID+handle-number shortcut exists.
- **Unified backing/provider API:** RAM/FIFO/TCP/UDP6/GE share the OBJ* five
  calls and the same rights-bearing `Vhand` family. UDP6 `NET6*` is reserved;
  the object provider, including `ObjOpNet6Addr=24`, is the only path.
  Same-identity volatile-to-GE promotion remains blocked.
- **Direct TCP readiness subscription:** not implemented; the demonstrated
  path is a worker that accepts and transfers the real connection object.
- **Same-identity volatile-to-GE promotion:** blocked (section 6).
- **GE capability revocation:** a delegated GE capability is closed by its
  Handles; later namespace/OREVOKE edits do not retroactively revoke it.
- **Performance:** no statistically meaningful latency/allocation measurement
  was run. The QEMU checks are correctness checks only.

## 8. Explicit exclusions

- No GE/O v1 format change, no reaming, no persistent-identity rewrite.
- No path, `/proc`, `/tmp`, `/net`, 9P walk, `Chan`/`Qid`/`Dev` resurrection
  or directory-tree namespace.
- No Python class system or object metadata schema in the native model.
- No dedicated IP/TCP/USB/HTTP FIFO type; no TCP byte-stream reframing.
- No mutable shared payload, no zero-copy claim, no unbounded queue.
- No swap or new paging behavior.
- No global machine-wide OID allocator, remote OID protocol or OID migration.
- No implementation of same-identity volatile-to-GE promotion.

### 8.1 Compatibility shims and removal condition

The following legacy syscalls remain and delegate to the same generic core:

| Slot | Shims | Delegates to |
| --- | --- | --- |
| 108-113 | `TCP6LISTEN/CONNECT/ACCEPT/READ/WRITE/CLOSE` | `vtcp_open`, `vtcp_accept`, `vtcp_read`, `vtcp_write`, `vobjclosehandle` |
| 116-120 | `NET6ADDR/OPEN/RECVFROM/SENDTO/CLOSE` | RESERVED (phase 4); use `ObjOpenUdp6`, `ObjOpNet6Addr` and `SendTo/RecvFrom`/`CLOSE` |
| 121-135 | `VOBJCREATE/READ/INFO/CLOSE`, `VOBJFIFOCREATE/SUB/SEND/RECV/STAT`, `VOBJGRANT/REVOKE/IO/LIST/OPEN/ACCEPT` | the same `vobj*` core now also called by 136-140 |

The TCP and VOBJ shims can be removed only after (a) the generic
`OBJCREATE/OPEN/CLOSE/BIND/INVOKE` path is the only tested client path, (b) a
version-negotiated compatibility transition is accepted, and (c) no retained
userland or test depends on the numeric legacy handles. This candidate does
not remove or renumber them.
UDP6 is migrated: `ObjOpenUdp6`, `ObjOpNet6Addr` and
`INVOKE(SendTo/RecvFrom)` are the only object path; `NET6*` 116-120 is
reserved and has no dispatch entry. The boot/permit gate remains on open and
address lookup. No new UDP-only syscall was added.

## 9. Verification

Environment: Linux host, GCC 14.2.0, Clang 19/LLD loader, QEMU 10.x with KVM,
amd64, disposable O images. All images are regular files under `build/`.

| Gate | Command | Result | Evidence |
| --- | --- | --- | --- |
| kernel build | `tools/build-gcc.sh --kernel` | PASS 132/132 selected, 134/134 closure, link PASS | `build/gcc/report.txt`, `build/gcc/full-report.txt`, `build/gcc/abi-report.txt` |
| kernel artifact | - | `build/gcc/9pc64.elf` SHA-256 `2b1c77150b22ec05fb092a80e8ad2490bb6d0d3aa29354708a88a6ba3f4686a9` | - |
| MicroPython build | `tools/build-mpy-gcc.sh -j 8` | PASS 246 units, 0 FAIL; ABI checks PASS | `build/gcc/micropython.elf` SHA-256 `221c4b02a35be862a7039e55947e32390ed11c59896e3acc4b4bfb6b7d908849`, size 517904 |
| new ABI self-test | `--repl-input build/vobj-test/obj_selftest.txt --expect "OBJ TEST PASS"` | PASS 35/35: create/open/bind/close, rights, TEE, GE, NS binding, negatives | `build/qemu-o/final-obj-selftest/serial-boot.log` |
| new single-process TCP6 HTTP | generic `obj_open_tcp`/`obj_accept`/`obj_read`/`obj_write`; real IPv6 peer | PASS HTTP/1.1 200 OK; `OBJ TCP SENT 40` | `build/qemu-o/final-obj-tcp/serial-boot.log`, `build/vobj-test/peer-final-obj-tcp.log` |
| new two-process TCP6 HTTP | generic family; A accepts and publishes GE+TCP, B serves | PASS; parent pid 3, worker pid 17, `WAIT (17, '')`, `OBJ GE b'persist'`, 200 OK | `build/qemu-o/final-obj-ipc/serial-boot.log`, `build/vobj-test/peer-final-obj-ipc.log` |
| legacy volatile/FIFO shims | `--repl-input build/vobj-test/repl.txt` | PASS 20/20 | `build/qemu-o/final-legacy-fifo/serial-boot.log` |
| legacy TCP6 shims | peer + `plan9.tcp6_*` single-process test | PASS HTTP/1.1 200 OK | `build/qemu-o/final-legacy-tcp/serial-boot.log`, `build/vobj-test/peer-final-legacytcp.log` |
| two-boot persistence | `QOB_INI_BOOTEXEC=... tools/qemu-o-persist.sh --tcg --repl --boot1-input ... --boot2-input ...` | PASS; `OBJ PERSIST READ b'newabi'`, new `obj_open_ge` read `b'newabi'` after reboot | `build/qemu-o/final-obj-persist/report.txt` |

Covered by the new 35-check self-test: CREATE of bytes/empty/scoped objects;
OPEN of a Handle with attenuation and amplification denial; BIND/OPEN by role,
exclusive/remove visibility and missing-binding errors; GE capability read;
GE binding visible through `space.olookup` (NS role, OID 42) and removed;
FIFO TEE, envelope, identity stability, grant attenuation; RAM and GE
references through the same FIFO with unchanged identity; denied subscribe,
send, read, accept and write; subscription Handle duplication and binding
denied; oversized read; unsupported operation and ABI
version; non-blocking receive; stale and double close; independent fast/slow
capacities; drop-oldest overflow; dead-subscriber publish; revocation wake and
denial; message lifetime after double close. The two-process run additionally
covers explicit OSPAWN Handle delegation, cross-process GE read through a
capability, real TCP connection-object transfer, accepting-process exit, and
HTTP service by a different PID. The two-boot run covers persistent byte
restore and generic `OBJOPEN(ObjOpenGe)` over the restored OID.

**Not run, reported honestly:** true multi-CPU concurrent send/close/recv
stress; concurrent bind/unbind/open/close race campaign; allocation-exhaustion
injection; repeated 1-1000 connection cycles; PID-reuse and guessed-Handle
attacks; exec/exit generic binding cleanup and child non-inheritance; a
read-only TEE subscriber for TCP; an optional concurrent third-process
(Proc C) TEE subscriber; the TCP6 per-process 8/global 64 endpoint cap,
listener collision and close-race regression suite; unprivileged spawn denial
of UDP6 open, OSPAWN UDP6 handle delegation, observed exec/exit UDP6 release,
the raw receive-buffer-too-small path (a 1232-byte send/receive peer round trip
and rights/close denials were run); external IPv6 connectivity beyond the
local UDP-encapsulated peer; physical hardware and disk writes. These are not
claimed as PASS.

## 10. Source map

- `runtime/abi/sys.h` — syscall numbers 121-140 and `struct Oinheritv`.
- `runtime/abi/vobj.h` — fixed-width identity, info/stat/list/TCP/GE request
  types and rights.
- `runtime/abi/obj.h` — the universal five-operation ABI: `ObjId`, `ObjCall`,
  provider/kind/operation/mode enums and libc wrapper prototypes.
- `kernel/core/sysobj.c` — `OBJCREATE/OPEN/CLOSE/BIND/INVOKE` handlers, the
  version/length/rights validation and the single INVOKE request router.
- `kernel/core/sysvobj.c` — volatile store, ObjectFIFO/TEE, shared core
  (`vobjcreat`, `vobjopenstream`, `vobjfifosend`, `vobjio`, ...), rights,
  revocation, quotas, generic bindings, spawn inheritance and cleanup.
- `kernel/core/vobjint.h` — the small internal Vobj/provider C contract and
  shared bounds.
- `kernel/core/systcp6.c` — TCP6 provider (`vtcp_*`).
- `kernel/core/sysnet6.c` — UDP6 `VkindUdp6` provider; the former `NET6*` shims are reserved.
- `runtime/libc/port/oobject.c` — libc/libo wrappers over the five OBJ syscalls
  and the read/write/receive/publish/subscribe/accept/UDP6 adapters.
- `kernel/core/portdat.h`, `kernel/core/portfns.h`, `kernel/core/proc.c`,
  `kernel/core/sysproc.c` — per-process Handle slot, OSPAWN flags=2
  delegation, exec/exit cleanup.
- `ge/storage/sysgefs.c`, `ge/storage/ocontext.h` — GE capability copy,
  capability authorization mode, generic read/write helpers, and the
  `octxbindmode`/`octxunbind` targets used by `OBJBIND`.
- `tools/host/build-kernel.py` — canonical manifest entry (`sysobj`).
- `tools/host/build-mpy-gcc.py` — generated syscall stubs for 136-140 and the
  libc manifest entry (`oobject`).
- `micropython/modplan9.c`, `micropython/p9space.c` — thin `plan9`/`space`
  bindings used by the disposable QEMU tests; not the only interface and not
  a new file format.
- `docs/OBJECT-MODEL.md`, `docs/KERNEL-API-ABI.md` — conceptual and ABI
  contracts.
