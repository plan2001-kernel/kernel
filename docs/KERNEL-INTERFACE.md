# Kernel interface: target and current status

The [architecture](ARCHITECTURE.md) is the single authority for target design.
The mechanisms below are proposals, not a frozen ABI. `runtime/abi/sys.h`
and kernel source define what works now. There is no fixed syscall count or
historical ABI compatibility obligation.

## Authority and identity

Persistent GE/O `(SpaceID, OID)`, volatile memory-region identity, volatile
process identity, process-local Handle and device identity are distinct.
Knowledge of a value or pointer grants no access. Object operations suit
resources with identity, ownership, rights, delegation and lifetime; direct
scalar operations are allowed. The kernel checks authority at each call and
mapping. Revocation, final close and process exit must have explicit lifetime
effects. GE/O object content and versions remain independent of volatile
process and region lifetimes.

Namespace roles can provide authorized lookup independent of ancestry. A
stored definition is data interpreted by a future userspace loader, not an
authority token. Role removal does not automatically revoke capabilities
already opened from that role. GE/O object-set membership is also data, not
a grant. The current `Pgrp/OContext` implementation is described below,
not required as the target namespace structure.

## Capability regions and publication (target)

Create a volatile region under a capability; grant at most one writer RW
mapping and any number of authorized reader RO mappings, including processes
with no launch relationship. Rights are per process and per mapping. Maps may
have different virtual addresses, so published references are region-relative
offsets or another mapping-independent form. Specify bounds/alignment,
reference ownership, unmap, writer death, revocation and reclamation. A live
reader retains a published immutable snapshot after writer death unless an
explicit revocation rule invalidates it. A known PID, OID or raw pointer
cannot obtain or identify a mapping capability.

Writer stores data, then atomically publishes a version/reference with release
ordering. Reader acquires that word before dereferencing a complete immutable
snapshot. Small deltas and references to large immutable records avoid
compulsory full copies. Ownership of storage and reuse must prevent mutation
of a snapshot while readers may still observe it. FIFO, RPC, mailbox, stream
and broadcast formats belong in userspace.

Proposed `WAIT(region, offset, expected, timeout)` accepts an RO mapping and
atomically checks the word against `expected` while registering the waiter,
so a publication/WAKE cannot be lost between check and sleep. Proposed
`WAKE(region, offset, count)` signals waiters but carries no data. Calls may
return for changed value, spurious wake, timeout or interrupt; readers always
recheck. Validation covers capability, region bounds, word alignment and
mapping rights. Kernel locks/queues can implement the synchronization.
Names, arguments and return codes are not a finalized syscall ABI. Region
creation/mapping may use existing object and VM families; no generic kernel
message provider is required.

## Process lifecycle and grants (target)

Process creation makes an independent schedulable context, with explicit
initial capabilities and namespace view. It does not confer lasting control
or exclusive observation on the launcher. Process-control grants permit
authorized management and may be delegated with attenuation. Separate
observation grants permit multiple holders to observe completion, including
after the launcher exits. Specify completion-state retention, notification,
revocation and cleanup by explicit references. A process may outlive its
launcher. Boot/initial status has an explicit designation, not a special
`parentpid == 0` lifecycle rule. A kernel process-tree database is not
required. Namespace sharing, if offered, is an explicit resource grant rather
than inherited parent/child policy.

## CURRENT IMPLEMENTATION

The AMD64 ABI currently includes object calls 136–140, `SPAWN(146)`,
`RFORKEX(143)`, `PROCWAIT(142)`, `OTERMINATE`, `OEXECUTE`,
`RENDEZVOUS(34)`, `SEMACQUIRE(37)`, `SEMRELEASE(38)` and
`TSEMACQUIRE(52)`. `BRK_`, `SEGBRK`, `SEGATTACH/DETACH/FREE/FLUSH`
provide existing VM operations. Rendezvous/semaphore public calls currently
require a writable segment. The target RO WAIT does not exist.

Volatile capability regions are `ObjKindRegion` objects reached through the
same five-call object ABI. Creation takes a non-zero, page-multiple size up to
16 MiB and returns a Handle with `Vread|Vwrite|Vmeta|Vdelegate`; `OBJOPEN`
attenuates it like any other Handle, and explicit delegation is the only way
another process obtains authority. `ObjOpRegionMap` installs the region at a
process-local address (arg0, 0 chooses) and returns that address.
`ObjOpRegionUnmap` removes the caller's mapping by address and needs no live
Handle. A writable mapping requires `Vwrite` and is unique for the region; it
is rejected while any other writable mapping exists. Read-only mappings require
`Vread` only. A mapping is independent of its Handle: closing the Handle does
not unmap it, and process exit or explicit unmap releases the mapping
reference. Writer exit releases the writer slot but not pages still referenced
by reader Handles or mappings. The region and its eagerly zeroed pages are
freed at the last reference. Phase 2 does not transfer the writer role
automatically.

The former named `shared` segment attach and `SG_SHARED` type are deleted;
`SEGATTACH` now serves only the anonymous `memory` and physical device
segments. `SG_RONLY` remains a per-mapping property of text, physical and
region Segments; for regions the per-process `SG_REGION` Segment and PTE set
carry the protection, not a property shared by all mappings.

`SPAWN` establishes a direct child and fresh `Pgrp`; `RFORKEX` uses a
bounded delegated Handle plan. `PROCWAIT` is child-only. Current
`Proc.parent/parentpid/nchild/waitq`, exit notification and special
`parentpid == 0` behavior are ancestry dependent. `OTERMINATE` has a
direct-child restriction. These are slated for Phase 5 deletion/replacement,
not future interface requirements.

`Pgrp/OContext` currently owns volatile role bindings with share/copy/empty
and sealing behavior. `OBJBIND`, `NamespaceView`, role opening and
delegation exist on AMD64. They do not imply a finished independent namespace
or persistent-definition loader. The implementation must be reviewed when
ancestry dependence is removed. A role, OID or set membership cannot grant
authority by name alone. GE/O revocation affects copies of the same grant
origin, not independently opened grants to the same OID.

`ObjectFIFO` currently supplies framed transport, subscriptions and Ethernet
receive; Pipe and `SPAWN` `stdoutpipe` remain. USB control completion is
synchronous and distinct. These are implementation facts. `ObjectFIFO` is
rejected as the target generic kernel IPC primitive. After the Phase 4
MicroPython proof, Phase 6 removes its provider, ABI, adapters and tests,
alongside replaced device/console transport. Do not replace it with a
universal kernel queue. See [device ABI](DEVICE-ABI.md).

Current `SYSCTL(144)` covers system/own-process inspection and configuration;
`IOCTL(145)` covers authorized device control/status. Device payload uses
object operations. Their exact records and rights remain in current headers.
Low-level device and scheduler mechanisms remain necessary. ARM64 currently
has early boot only; this AMD64 interface is not ARM64 feature parity.
