# Kernel interface: target and current status

The [architecture](ARCHITECTURE.md) is the single authority for target design.
The mechanisms below are proposals, not a frozen ABI. `runtime/abi/sys.h`
and kernel source define what works now. There is no fixed syscall count or
historical ABI compatibility obligation.

## Authority and identity

The [target architecture](ARCHITECTURE.md) defines a single, named,
immutable `OwnerID` per object. A process may directly operate only on
objects with the same owner. There are no Unix UID/GID, rwx, ACL, general
capability-rights, delegated ownership or ownership transfer in the target.
An OID, name or namespace relation gives identity, never ownership.

Trusted registration binds a program name, owner, executable object and
startup policy. The kernel loads registered executables at boot or by
a launch request and starts an independent process as the **program's**
owner, never as the requester. Start does not grant access to the image,
other program objects or the process lifecycle. Voluntary stop and object
modification are owner-handled requests over the existing IPC.

The sender keeps ownership of its original data. Received data values
belong entirely to the receiver, and stored received objects are owned
by the receiver. No owner is allowed to directly change another's objects.

## Capability regions and publication (target)

The **existing** region-publication mechanism is the IPC transport: one
writable publisher, read-only observers, release/acquire publication and
blocking REGIONWAIT/REGIONWAKE. Do **not** add a message/send/receive
transport, FIFO, Pipe, ObjectFIFO or generic kernel IPC provider.

Observer mappings show offered data, not direct object-management authority
over the publisher's underlying region. Any independently stored received
data belongs to the receiver. Keep RO/RW page protection, bounds/alignment
checks, region-relative offsets, unique writer mapping, lifetime cleanup,
atomic WAIT enrollment and WAKE notifications. WAKE carries no payload.
These memory-safety mechanisms are distinct from general rwx/capability
object permissions.

The capability handles, mapping rights and revocation in CURRENT
IMPLEMENTATION remain accurate descriptions of the presently implemented
ABI, **not** target permission requirements. The implemented owner gate makes
those rights unable to authorize cross-owner management.

## Process lifecycle and registered launch (target)

Program code, configuration and stored data belong to the program's named
owner. Trusted boot/installation registration supplies the image OID and
startup policy. Kernel boot starts autostart entries, and a launch request
resolves a registered entry and starts an independent process under the
program's owner. There is no parent/child lifecycle, launcher-owned process
or implicit launcher control.

Process completion, voluntary exit, forced termination and cleanup remain
separate kernel lifecycle operations. The process owner and explicit kernel
lifecycle policy govern forced termination; userspace may offer a shutdown
request that the program can choose to act upon. No new syscall family,
permission-grant model or IPC transport is required by this design.

## CURRENT IMPLEMENTATION

Immutable single-owner identity is implemented on AMD64. Every process has a
trusted `OwnerID`; children created by fork/spawn inherit the caller's owner,
while a registered-program launch uses the program's owner. `OwnerID` 0 is
reserved for kernel-internal execution; user-visible owners, including
`root`, are ordinary named identities (a stable identity derived from the
trusted name). Structs `Proc`, `Procstat` and `Vobj` carry the owner, and
`ObjOpOwnerInfo` (52) reports owner identity for any object or process
Handle; `SysctlOwnerInfo` reports the caller's own owner. Owner identity is
never selectable by a process, an OID, a Handle, a namespace role or a right
mask.

Direct management of bytes, GE/O, process control and device objects requires
`process.owner == object.owner`. Existing Handle rights remain an attenuation
and addressing mechanism, but possession of another owner's Handle no longer
authorizes management: GE/O open/read/write, creation, device operations,
region revocation and process termination are denied across owners.
Completion observation, `ObjOpInfo` metadata, owner metadata and read-only
region publication remain separately auditable operations. A read-only region
mapping is a publication view, not ownership of the publisher's region or
source object; the unique writable mapping requires the region owner.

Trusted boot registration (`PROGRAMREGISTER(149)`) records
`{program name, OwnerID, executable GE/O object, startup policy}` in a bounded
kernel registry. It is accepted only from the explicit boot process. For an
explicit registration the kernel installs a private executable copy owned by
the program owner, so a later launch never borrows the requester's authority
or exposes the image. The boot entry itself binds the already-loaded boot
executable.
`PROGRAMLAUNCH(150)` resolves only a registered launchable name and starts an
independent process under that program's owner; the requester receives an
observation-only process Handle and no control or launcher ancestry. The boot
MicroPython entry is recorded as the default autostart entry by the volume-open
path; the loader's in-place `OEXECUTE` remains the autostart action.
Unregistered names, non-boot registration and cross-owner termination are
rejected. The optional `Ospawn`/`Oinherit`/`Oinheritv` layout is unchanged and
is an explicit IPC-reference/CPU/budget declaration, not an ownership grant.

The AMD64 ABI currently includes object calls 136–140, `SPAWN(146)`,
`RFORKEX(143)`, `OEXECUTE`, `SYSCTL(144)`, `IOCTL(145)`, `REGIONWAIT(147)`,
`REGIONWAKE(148)`, `PROGRAMREGISTER(149)` and `PROGRAMLAUNCH(150)`.
`PROCWAIT(142)` and `OTERMINATE(93)` are reserved and have no handler.
`BRK_`, `SEGBRK`, `SEGATTACH/DETACH/FREE/FLUSH` provide existing VM
operations. The retired
public `RENDEZVOUS`, `SEMACQUIRE`, `SEMRELEASE` and `TSEMACQUIRE` calls and
their user wrappers are gone; no old number is kept as a compatibility
entry. Internal scheduler `Rendez`/sleep/wakeup remains.

Volatile capability regions are `ObjKindRegion` objects reached through the
same five-call object ABI. Creation takes a non-zero, page-multiple size up to
16 MiB and returns a Handle with `Vread|Vwrite|Vmeta|Vdelegate|Vcontrol`; `OBJOPEN`
attenuates it like any other Handle, and explicit delegation is the only way
another process obtains authority. The eagerly allocated, zeroed backing pages
are committed once to the creating process's memory budget at creation; a
mapping only installs PTEs over that backing and is not charged again, so
creating many regions cannot bypass the budget by leaving them unmapped.
`ObjOpRegionMap` installs the region at a process-local address (arg0, 0
chooses), takes an optional `ObjRegionMap*` flag mask in arg1 (0 preserves the
default), and returns that address. `ObjOpRegionUnmap` removes the caller's
mapping by its exact base address and needs no live Handle. A writable mapping
requires `Vwrite` and is unique for the region; it is rejected while any other
writable mapping exists. Read-only mappings require `Vread` only, and
`ObjRegionMapReadonly` maps a `Vwrite` Handle read-only without consuming the
writer slot. A mapping is independent of its Handle: closing the Handle does
not unmap it, and process exit or explicit unmap releases the mapping
reference. An ordinary fork does not inherit region mappings; an RFMEM child
shares the parent's address space and therefore shares the installed
`SG_REGION` Segments, their PTE sets and the writer slot. Writer exit releases
the writer slot but not pages still referenced by reader Handles or mappings.
The region and its eagerly zeroed pages are freed at the last reference.
Phase 2 does not transfer the writer role automatically.

`ObjOpRevoke` on a region Handle carrying `Vcontrol` is the explicit
revocation operation. It permanently denies **new** mappings through every
previously delegated region Handle, including the creator's, but leaves
already installed mappings readable/writable until unmap or process exit.
Those mappings and the pages then retain the region until the last relevant
reference. A read-only Handle does not carry `Vcontrol` and cannot revoke
other parties. There is no implicit TLB shootdown or forced unmap.
Installed mappings, including their writable slot, remain usable for
`REGIONWAIT`/`REGIONWAKE` after revocation; only new maps are denied.

This is a deliberate limitation: revocation cannot remove authority that has
already been exercised as an installed mapping, and it cannot reclaim or
transfer the writer slot. A broken writer therefore keeps the slot until its
process exits or is terminated, which releases the mapping and notifies
waiters. Recovering a service after a crashed writer relies on that process
death, not on revocation; there is no forced unmap, writer eviction or
implicit slot transfer.

`REGIONWAIT(mapping base, region offset, expected, timeout)` and
`REGIONWAKE(mapping base, region offset, count)` operate on an installed
`SG_REGION` mapping, not a Handle or raw pointer. The publication word is
64-bit, little-endian and 8-byte aligned; offsets are region-relative and
bounds/alignment checked. Authority is the caller's mapping: an RO observer
may wait, the mapping survives Handle close, and a guessed address or another
process's Handle conveys nothing. Only the unique writable mapping may wake;
an RO holder cannot. `count` must be positive and the call returns how many
waiters it woke. `WAIT` applies a fixed order: validate the mapping and word;
compare the word and return changed (0) if it differs even when no writer is
present; if it still matches and no RW mapping is active, use the writer
generation to distinguish the two temporal states. A writer that was present
and released, or a revoked region, returns 3 immediately even for an infinite
timeout. A region that has never had a writer enrolls and blocks until a
writer appears or the timeout/interrupt applies, so the reader does not poll
for writer appearance. Writer-presence, comparison and enrollment are
linearizable under one short spin lock with writer-slot acquisition and
release. Status 3 therefore means "the previous writer mapping was released,
or revocation ended the possibility of a new writer", not permanent
destruction: a new writer may map later unless revocation denies it, and the
caller decides whether to retry or treat the stream as ended. Releasing the
writable mapping notifies enrolled waiters with 3 as a lifecycle event, which
a rapid replacement writer cannot hide. Normal returns also include woken (1)
and timeout/non-blocking (2); interruption or an invalid mapping raises the
ordinary error. Waiters always recheck; a wake carries no data and proves no
particular value. The kernel does not copy payload bytes. A faulting or
NDebug-terminated user process no longer parks in a Broken state that retains
its mappings. Final termination releases the writable mapping slot and
notifies REGIONWAIT readers with status 3, while read-only snapshots remain
readable through their own mapping references.

The Phase 4 three-process MicroPython path grants two regions with explicit
`SPAWN` vhandles: A writes only the task region, B writes only the answer
region, and C maps both read-only. Its scalar, delta and immutable 8 KiB
records, notifications and acknowledgements use those mappings and
`REGIONWAIT`/`REGIONWAKE`; the path has no `ObjectFIFO` or Pipe transport.

The former named `shared` segment attach and `SG_SHARED` type are deleted;
`SEGATTACH` now serves only the anonymous `memory` and physical device
segments. `SG_RONLY` remains a per-mapping property of text, physical and
region Segments; for regions the per-process `SG_REGION` Segment and PTE set
carry the protection, not a property shared by all mappings.

`SPAWN` creates a fresh `Pgrp` and returns a process-local `VkindProcess`
Handle carrying `Vobserve|Vcontrol|Vmeta|Vdelegate`; `RFORKEX` uses the same
delegation plan and also returns a process Handle (the child sees 0).
`OBJOPProcessInfo` (`Vmeta`) reads the current lifecycle record;
`OBJOPProcessWait` (`Vobserve`) blocks until `ObjProcCompleted` or returns a
bounded timeout and never consumes the result; `OBJOPProcessTerminate`
(`Vcontrol`) requests termination. Observation returns PID, generation,
state, cause, status and accounting, so a PID or process-table index grants
no authority. Multiple observers may hold independently attenuated Handles,
including after the launcher exits. The completion record is reference
counted and outlives execution; the scheduler `Proc` and address space are
reclaimed independently. Voluntary exit, authorized termination and unhandled
fault are distinct causes. `OEXECUTE` preserves the same process identity and
existing observer authority. The boot process has an explicit `Proc.boot`
role; no ancestry-dependent process wait or control path remains. The
historical no-argument libc `wait()`/`await()` adapters are deleted: callers
use `obj_process_wait()` with an explicit Process Handle, close it when
finished, and need no global tracking table. A process Handle has no explicit
revoke operation; completion is immutable and control is a separate grant.

`Pgrp/OContext` currently owns volatile role bindings with share/copy/empty
and sealing behavior. `OBJBIND`, `NamespaceView`, role opening and
delegation exist on AMD64. They do not imply a finished independent namespace
or persistent-definition loader. The implementation must be reviewed when
ancestry dependence is removed. A role, OID or set membership cannot grant
authority by name alone. GE/O revocation affects copies of the same grant
origin, not independently opened grants to the same OID.

Ethernet receive and console sessions use the `ObjRingHeader` capability-ring
layout in `runtime/abi/obj.h`: one writer, authorized read-only mappings,
release/acquire publication and `REGIONWAIT`/`REGIONWAKE`. Ethernet transmit
uses the same layout in the opposite direction: the authorized publisher owns
the unique writable mapping and the kernel consumes complete frames and
publishes `ack_seq`. USB control completion is synchronous and distinct.
`ObjectFIFO`, Pipe and the `SPAWN` stdout route are deleted; no generic kernel
message queue replaces them. See [device ABI](DEVICE-ABI.md).

Current `SYSCTL(144)` covers system/own-process inspection and configuration;
`IOCTL(145)` covers authorized device control/status. Device payload uses
object operations. Their exact records and rights remain in current headers.
Low-level device and scheduler mechanisms remain necessary. ARM64 currently
has early boot only; this AMD64 interface is not ARM64 feature parity.
