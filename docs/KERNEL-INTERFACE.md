# Kernel interface: target and current status

The [architecture](ARCHITECTURE.md) is the single authority for target design.
The mechanisms below are proposals, not a frozen ABI. `runtime/abi/sys.h`
and kernel source define what works now. There is no fixed syscall count or
historical ABI compatibility obligation.

## ObjectSpace graph and capability target

[ObjectSpace](OBJECTSPACE.md) is the current authority for target semantics.
OwnerID is immutable identity/provenance, not a universal per-operation
OwnerID-equality gate. Capability possession, reduced rights, revocation
state and provider-specific safety determine access. No raw OID, process ID,
label, kernel pointer or storage SpaceID can grant authority on its own.

`OBJCREATE(ObjKindObjectSpace)` now creates a volatile independent graph.
`OBJINVOKE` operations `ObjOpSpaceBind`, `ObjOpSpaceUnbind`,
`ObjOpSpaceResolve`, `ObjOpSpaceEnumerate` manipulate unique labeled
BindingIDs. Duplicate names and nested spaces are supported, without
path parsing. Object ABI version 2 is intentionally incompatible with
the earlier value. The ABI records are in `runtime/abi/objectspace.h`.

The capability-lineage layer derives attenuated Handles with at most 128
ancestor grants. `ObjOpCapRevoke` requires Vdelegate on the exact source
Handle and invalidates prior descendants, not the source, unrelated
siblings, provider object or already installed region mappings. A stored
ObjectSpace binding preserves its own derived grant lineage; revoked
bindings report `ObjSpaceFlagRevoked` and cannot mint fresh Handles.
Generic subtree revocation is independent of provider `ObjOpRevoke`
and binding removal. Cross-owner reads, writes, mapping and control are
authorized by explicit grants. The GE/O Ohand enforces object/Space scope
and independent revocation state; owner records are integrity-checked,
not used to authorize requests.

The v1 limit is 64 bindings per space, 47 label bytes plus NUL, and eight
records per enumeration page. An epoch rejects mixed snapshots. Space-to-space
references are weak in this first implementation to avoid retaining cycles;
independently owned target Handles keep them live. Unbind does not revoke an
already resolved Handle. A generic volatile Handle derivation tree and descendant revocation are
now implemented by `ObjOpCapRevoke=57`. GE/O v2 can save and load one
checksummed graph per durable OID through `ObjOpSpaceSave=58` and
`OBJOPEN(ObjOpenSpaceGraph)`. Trusted boot treats `plan9.ini
obootgraph=<OID>` as a selector, not authority: it automatically restores
that graph on the `obootvol` GE/O volume and installs the root
`VkindObjectSpace` Handle in the bootstrap process before user code runs.
No selector means no automatic root; an invalid one fails closed.
Existing Pgrp namespaces remain **legacy current behavior**, not architectural
target invariants. OwnerID access gates have been replaced by capability
admission and provider safety conditions.

The region-publication IPC with one RW writer, RO observers, WAIT/WAKE and
release/acquire publication is retained. ObjectSpace may name regions, but
does not introduce any new kernel message queue, FIFO or network transport.
Processes are independent contexts; launch ancestry never grants ownership.

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

A process accesses an object only through a valid, generation-checked Handle
(or an explicitly authorized provider opening path). `Vread`, `Vwrite`,
`Vcommit`, `Vcontrol`, `Vdelegate` and the other operation-specific rights
are attenuable and revocable. Matching OwnerID does not manufacture a grant;
a different OwnerID does not invalidate an otherwise valid grant. A region's
single writable mapping still requires `Vwrite` and the unique writer slot;
read-only publication requires a read-capable mapping. Cross-owner GE/O
writes retain the first stored Kowner; provenance never follows the writer.
GE/O object/Space rights, Ohand scope and O-store integrity are still checked
before any persistent action. Physical device claims remain exclusive.

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
Unregistered names and non-boot registration are rejected. Cross-owner
termination requires an explicitly delegated process `Vcontrol` Handle;
`Vobserve` alone never authorizes it. The optional
`Ospawn`/`Oinherit`/`Oinheritv` layout is unchanged and describes
explicitly delegated capability rights, CPU placement and budgets.

The named-owner and registered-program tables are volatile RAM. Stored object
OwnerIDs survive restart, but the names and program records do not. Trusted
boot/installation must therefore re-register program identities and names
needed for launch and symbolic metadata on each boot. Authorization to an
already delegated object does not depend on re-registering its OwnerID. Registration of the same trusted name deterministically
resolves to the same 64-bit OwnerID, so stored owners continue to match.
Existing stored OwnerIDs are never reassigned or inferred from a requesting
process. If a stored object's OwnerID has no current name in the registry, the
numeric owner is reported as unresolved (empty name); it is not mapped to
`root`, rewritten, or granted to the requester. A two-boot test restores
the same O volume, reconstructs the trusted registrations, and verifies
immutable provenance with explicitly delegated cross-owner access.

`OWNERIDFAULT(151)` is a boot-only, test-only deterministic fault point for
creation/publication recovery tests. It does not grant ownership or expose a
production configuration interface.

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


## Boot process manifest (v1)

On AMD64, `plan9.ini` can contain `bootmanifest=/processes.conf`. The
UEFI loader reads the separate LF-terminated file from its boot EFI volume,
limits it to 16 KiB, and passes it as a distinct NUL-terminated section of
the shared `BootInfo` v1 blob. A missing named manifest aborts the boot
instead of silently using a different startup configuration. Without
`bootmanifest=`, the original `obootexec` behavior is unchanged.

The file is text with `version=1` followed by one to eight named entries.
Each entry is `[process.NAME]` with required `source=` and `owner=` and
optional `cpu=`, `args=` and `required=true|false` fields. For example:

```ini
version=1
[process.python]
source=geo:PARTUUID=11111111-1111-1111-1111-111111111111:OID=0x4f45584543000003
owner=python
cpu=0
args=-N
required=true
[process.probe]
source=memory:selftest
owner=probe
required=false
```

The kernel starts each entry as a separate kernel-created process root,
not as a child of an init user process. A GE/O program receives a separate
embedded loader, which obtains its own volume Handle and replaces itself
with the executable addressed by OID. `memory:selftest` is a small
embedded executable for CI that exits immediately, **not** a rescue
console. A process failing to load or exiting does not halt unrelated
manifest processes. `required` is currently a diagnostic label, not a
dependency or automatic-restart policy.

Current scope is one activated GE/O volume with a matching PARTUUID
across all `geo:` entries. Additional GE/O volumes and network source
resolvers are **not implemented**. Malformed manifests or unsupported
source types are rejected as boot-configuration errors. This v1 manifest
is **not authenticated**; UEFI Secure Boot, signed manifest verification,
kernel hash checking and executable hash checking remain separate
security work. Neither its source bytes nor the process owner strings
must be interpreted as cryptographic authority.

`tools/gcc-o-install.sh --bootmanifest FILE` installs the sidecar as
`<ESP>/processes.conf`, adds `bootmanifest=/processes.conf` to the canonical
`plan9.ini`, and verifies the copied bytes with `--verify`. It cannot be
combined with `--obootexec`. The installer also backs up every ESP file it
replaces; see [HARDWARE-BOOT.md](HARDWARE-BOOT.md).

## Kernel-owned boot process consoles (AMD64)

With a `bootmanifest=` configuration the kernel owns the permanent
SystemConsole framebuffer view and the exclusive PS/2 keyboard focus.
Each manifest process has its own input, output and diagnostic/stderr
capability-region ring, created by the kernel. The three attenuated
Handle grants are installed after `OEXECUTE` clears the bootstrap Handles.
MicroPython attaches by enumerating its process-local region grants.
No new IPC primitive or syscall is introduced. A process may not write to
the framebuffer or kernel `kmesg` through these console grants.

`Ctrl+Alt+F1` selects the kernel SystemConsole; `Ctrl+Alt+F2` through
`F9` select a live process session. The kernel retains off-screen output
history independently of user process lifetime. A session exit releases
its input and does not terminate other processes. Kernel diagnostics
remain separate from user stdout/stderr; they are not automatically
mirrored to serial. The framebuffer path is only as available as the
hardware framebuffer and kernel; full panic-interactive recovery is not
implemented.

Physical PS/2 set-1 input is translated once by the kernel. `keymap=fi`
(the default) and `keymap=us` select built-in Unicode tables in plan9.ini.
Fi accent keys emit their literal characters immediately. Navigation emits
the escape sequences understood by MicroPython readline; Ctrl+Alt+F1–F9
are consumed by kernel focus handling. USB keyboard input is not implemented.

`ObjOpConsoleInfo=59` requires Vmeta on a console input region and returns
`ObjConsoleInfo` (version 1, columns, rows, reserved=0), all 32-bit fields.
Other objects reject the operation. Columns and rows describe fixed text cells.
The MicroPython editor uses this width for a horizontally scrolling input
line. The kernel draws a steady Plan 9 text tick and retains independent
text/parser state for hidden sessions. No framebuffer authority is delegated.

Slot 0 also has a bounded kernel-resident command prompt. It reads the
currently focused keyboard in kernel context, never delegates to a user
process, and adds no syscall or IPC primitive. The initial commands are
strictly read-only: `help`, `status` (kernel build and boot mode),
`sessions` (slot, name, PID and live/exited state) and `dmesg` (the
kernel diagnostic ring). Input is line-buffered with Backspace and Enter,
unknown commands return an error and a fresh prompt, and the prompt is
redrawn below new diagnostics. The command transcript is separate from
kmesg so that `dmesg` does not recursively contain itself. A missing PS/2
keyboard leaves the kernel and all sessions running but removes interactive
focus switching; USB-only keyboard support is not implemented.

For separate boot REPLs, `micropython -O PARTUUID=... -R` enters a
session-only interactive REPL **without** running `init.run()`, selecting
a GE/O object graph or automatically committing storage. The executable
still comes from its manifest GE/O OID. Unlike `-I`, `-R` requires
a kernel-provided console session, and Ctrl+D exits that process only.
This mode avoids unrelated concurrent GE/O write transactions in the
console acceptance. `-I` and `-N` keep their previous boot semantics.

The future C1–C10 recovery invariants are documented in
[ARCHITECTURE.md](ARCHITECTURE.md#console/session-contract-kernel-owned-minimum-and-future-invariants).
Ethernet L2, UART rescue, remote terminals, interactive panic recovery,
network program loading, and multi-volume GE/O are not implemented.
