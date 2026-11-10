# Plan 2001-kernel: target architecture

This is the authoritative **target** architecture, not a claim about working
APIs. Plan 2001-kernel is experimental on AMD64 and ARM64. There is no Unix,
POSIX, Plan 9 or historical ABI compatibility obligation and no fixed syscall
count. See [kernel interface](KERNEL-INTERFACE.md) for proposed mechanisms and
current ABI status.

## Identity and authority

Persistent GE/O objects have `(SpaceID, OID)` identity within a volume. A
stored occurrence, volatile memory region, volatile process identity,
process-local Handle and physical device identity are distinct. A known OID,
numeric PID, selector or pointer conveys no authority. Operations and mappings
require checked, attenuated capabilities. The kernel retains privileged memory
protection, CPU scheduling, synchronization, device control and capability
enforcement. Object protocols and libraries belong in userspace. Native
paths, 9P and Chan/fid/qid are not required.

## Independent processes and resource graph (target)

A process is an independently schedulable execution context. Launching one
does not permanently establish privileged ancestry. Relationships arise only
from explicit process-control and observation grants, memory mappings,
namespace roles, devices and other shared resources. These grants form a
graph; no mandatory kernel graph database is needed. Creation, control,
completion observation and resource lifetime are separate concerns. A
launcher does not automatically own, control or exclusively wait for its
launched process. A process can outlive its launcher. Multiple authorized
observers can see completion; a holder of delegated control authority can
manage a process regardless of who launched it. Initial/boot process status
is explicit, never inferred from `parentpid == 0`.

Namespace views can bind authorized roles independently of ancestry.
Binding, lookup and delegation retain capability checks and attenuation.
Removing a role does not revoke an already held capability. View lifetime
follows explicit references. A stored GE/O namespace definition is data for
a possible userspace loader, not a live namespace or authority source; its
format and loader are not implemented. Persistent object sets and memberships
are separate data/query concepts. Membership never grants authority. Cross-
volume identity comparisons include the volume. There is no required kernel
hashtag/tag language or native hierarchy.

## Memory-publication IPC (target)

A publishing region has one writer and any number of authorized read-only
observers. The same physical pages may be mapped RW by the writer and RO by
independent readers at different virtual addresses. Region identity is a
volatile capability, separate from GE/O identity. Each mapping is checked
against its process's authority. Define reference lifetime, revocation and
reader survival after writer death: death must not silently invalidate a live
authorized reader's immutable snapshot. Reclaim after references end or an
explicit revocation policy applies.

The writer constructs data and publishes one atomic version or region-relative
reference with release ordering. Readers acquire it and see complete immutable
snapshots. A raw virtual address is not a valid shared reference. Small delta
records need not copy large state; large immutable records can be referenced
without copying their contents. FIFO, mailbox, RPC, event streams and
broadcast are userspace protocols.

Waiting blocks rather than polls. Proposed `WAIT(region, offset, expected,
timeout)` atomically checks the publication word against `expected` while
registering sleep, even for an RO observer. `WAKE(region, offset, count)`
wakes eligible waiters without transferring data. Readers recheck after any
return; spurious wakeups, timeout and interrupt are ordinary outcomes.
Internal kernel queues/locks are allowed. These signatures are design
targets, not finalized syscall ABI. Region creation/mapping uses kernel
memory and capability mechanisms, potentially existing object/VM families,
not a new kernel IPC provider. `ObjectFIFO` is rejected as a generic kernel
IPC primitive.

## Devices and console (target)

Preserve low-level driver, scheduler and early-boot diagnostic mechanisms.
An authorized userspace console service can own keyboard input, translate
scan codes and route events to selected sessions. Clients get separately
delegated input, output and control rights; focus is a service decision, not
ancestry. Ethernet reception and structured events can publish immutable
records in capability-mapped regions with WAIT/WAKE notification. Framing,
buffering, backpressure and byte streams are userspace protocol choices.
Do not mandate permanent `ObjectFIFO` or Pipe endpoints. Define service
failure recovery and reader lifetime. Protected boot/panic console output
remains available. See [device ABI](DEVICE-ABI.md) for current device facts.

### Console/session contract (TARGET)

One authorized console service owns the keyboard device. Keep raw hardware
access separate from scan-code decoding, keyboard layout and terminal policy.
Only the active session receives physical input; independent REPLs must never
race for raw keystrokes. The service routes input, output and diagnostics
through explicit namespace/capability views independently of launch ancestry.
Sessions retain their own references and may outlive individual processes.

Each session has separate input, output, diagnostics and control authority.
Input is a service-owned publication region mapped RO by its client. Output
and diagnostics use client-owned writer regions observed RO by the service;
multiple writers require separate regions. Control requests require a separate
grant. Publish immutable records with release/acquire and use WAIT/WAKE for
blocked observers. Endpoint grants convey no raw keyboard ownership, global
framebuffer access or kernel debug-output authority. Role removal and focus
switching do not implicitly revoke previously delegated capabilities.

The service/library boundary handles scan codes, modifiers and UTF-8 conversion.
Specify one owner for echo, line editing, raw/cooked mode and control-key
interpretation. Deliver Ctrl-C interrupt and Ctrl-D EOF exactly once to the
active session, including cancellation/wakeup of blocked readers. Readers
recheck state after every wake. Define bounded buffering and backpressure for
inactive output and slow clients. Focus changes atomically select the input
recipient; queued input must not leak into the next session. On client exit,
release its references and select a surviving session by explicit policy.
On service death, unblock observers with a detectable termination condition,
release device claims and allow an authorized recovery owner to reacquire the
keyboard and rebuild sessions. The protected kernel-owned early-boot/panic
framebuffer/serial diagnostics path remains independent and cannot be blocked
indefinitely by a failed userspace service.

Phase 6 must demonstrate an end-to-end handoff: boot to MicroPython, start the
console owner, attach two independent REPLs through publication-region grants,
switch focus, type UTF-8 and control keys, observe separate output/diagnostics,
and interrupt or terminate a blocked reader. Exit the active client and recover
a surviving session; kill/restart the service and reacquire the keyboard
without leaked claims. Check denied raw-device/control grants and absence of
input theft; kernel diagnostics must still work. Run the AMD64 guest checks
and record hardware checks separately. This is a future acceptance contract,
not evidence that this data path exists today.

## CURRENT IMPLEMENTATION

AMD64 has a functional full-system path with GE/O, MicroPython and device
providers. `SPAWN` and `RFORKEX` return a process-local `VkindProcess` Handle
carrying `Vobserve|Vcontrol|Vmeta|Vdelegate`; a process Handle, not a PID, is
the authority. Observation and control are separate `OBJINVOKE` operations,
completion is an immutable multi-reader record, and no parent/child wait
state or ancestral control path exists. `Pgrp/OContext` sharing/copying/
sealing and namespace views remain independent resource mechanisms.
`ObjectFIFO` carries framed IPC and Ethernet receive data; Pipe and current
console paths also remain. The `-I` REPL directly claims PS/2 input;
`micropython/p9console.c` decodes the scan codes and supplies the HAL.
`ObjOpenConsole` and `stdoutpipe` route selected output;
`kernel/core/fbconsole.c` is the kernel-owned framebuffer text sink. These are current
facts, not target requirements.

AMD64 also has volatile capability regions: `ObjKindRegion` creation, an
attenuable `Vread`/`Vwrite` Handle, and `ObjOpRegionMap`/`ObjOpRegionUnmap`
over the existing object ABI. A region owns eagerly allocated, zeroed Page
backing. Exactly one writable mapping may exist at a time; authorized reader
Handles map the same pages read-only at process-local addresses, with PTERONLY
enforced per `SG_REGION` Segment. A mapping survives Handle close and is
released by unmap or process exit; the region and its pages are reclaimed at
the last mapping/Handle reference. Writer transfer is rejected in this phase.
An authorized `Vcontrol` revoke blocks all future mappings through existing
Handles without invalidating installed mappings; readers cannot revoke.
`REGIONWAIT(147)` and `REGIONWAKE(148)` implement region-scoped publication
waiting over an installed mapping. The publication word is a naturally
aligned 64-bit little-endian value compared only for equality; userspace uses
release/acquire atoms, and the kernel never copies payload. Waiters are keyed
by the live region plus region-relative offset in a per-region queue guarded
by a spin lock; writer presence, word comparison and registration are
linearizable with writer-slot acquisition and release. Read-only mappings may
wait, the mapping survives Handle close, and only the writable mapping may
wake. A changed word returns immediately even with no writer; an unchanged
word with no active writer returns writer-released immediately, including for
a late waiter after unmap or writer-process exit. Timeout, interruption,
writer-slot release and process exit remove the waiter; return values
distinguish changed, woken, timeout and writer-release so callers always
recheck. A later writer may map and publish, so status 3 is temporal rather
than destruction. The retired public
`RENDEZVOUS`, `SEMACQUIRE`, `SEMRELEASE` and `TSEMACQUIRE` ABI, its user
wrappers, `RF*REND` resource and internal `Rgrp`/`Sema` waiting lists were
deleted. Free-process grants remain Phase 5. The former named `shared`
segment attach, `SG_SHARED` type and `_globalsegattach` hook were removed;
`SEGATTACH` remains only for the anonymous `memory` and physical device
segments.

Phase 4 demonstrates three independent MicroPython processes over two
capability regions: A is the only writer of the task region, B the only
writer of the answer region, and C holds read-only mappings to both. Scalar,
delta and immutable 8 KiB records cross only through shared pages;
`REGIONWAIT`/`REGIONWAKE` carry notification only. The processes receive
attenuated region Handles through explicit `SPAWN` vhandles, so this data
path uses no `ObjectFIFO`, Pipe or payload-copy syscall.

Phase 5 adds one reference-counted `Procstat` completion resource per user
process, owned by the live `Proc` and then by every process Handle. A single
completion transition publishes state, cause (voluntary exit, authorized
termination or unhandled fault), bounded status and runtime accounting, then
wakes every enrolled observer without consuming the record. The address
space and its writable region mappings are released before completion, so a
faulted or terminated writer cannot retain the only RW slot; read-only
snapshots survive through their own mappings. `SPAWN` and `RFORKEX` install
the initial Handle before publication, and `PROCWAIT`, `OTERMINATE`,
`Proc.parent/parentpid/nchild/nwait/waitq`, the Broken-retention path and
`RFNOWAIT` are deleted. `SPAWN` still creates a fresh `Pgrp`; no ancestry
grants authority. The boot process has an explicit `Proc.boot` role, not a
`parentpid == 0` test. `OEXECUTE` keeps the same Process identity and its
observers; it replaces the image in place without granting new rights.

ARM64 has an early UEFI/QEMU boot foundation with BootInfo handoff, mappings,
console, timer interrupts and physical-page allocation, not full-system
scheduler, GE/O, syscalls, userspace or device parity. MicroPython is the
proof-of-concept userspace. Source and tests govern exact current behavior.

## Six-stage roadmap

Every phase PR records **Added, Deleted, Tests actually run, NOT RUN, and
Remaining obsolete code**, including exact removed paths. A working new path
does not excuse leaving replaced code in place. No shims, aliases, dormant
providers/fixtures, feature toggles or retained syscall numbers exist solely
for backward compatibility.

1. **Architecture reset:** replace superseded normative instructions across
   all tracked docs; delete contradicted claims. No code changes.
2. **Capability memory regions:** implement independent RW-writer/RO-reader
   mappings, identity, lifetime and revocation. Delete replaced segment user
   ABI when superseded.
3. **Atomic publish and WAIT/WAKE:** implement release/acquire publication,
   RO waits and lost-wakeup prevention. Delete replaced rendezvous/semaphore
   public ABI when no longer needed.
4. **MicroPython evidence:** A publishes a task; B reads it and publishes an
   answer in B's region; A observes; C observes concurrently. Test scalar,
   delta, immutable record, writer death, interrupt and multiple cores.
   Delete `ObjectFIFO` from this data path.
5. **Free processes:** explicit process-control/observation grants,
   multi-observer completion and launcher-first exit. Delete ancestry-based
   lifecycle code and obsolete `RFORKEX/PROCWAIT/OTERMINATE` semantics.
   Retain or reshape namespace views only where justified.
6. **Delete old kernel IPC:** migrate required device/console transport;
   physically delete `ObjectFIFO` implementation, provider types, ABI,
   MicroPython/libc adapters, obsolete tests/docs, plus replaced Pipe/other
   IPC. Boot and the console/REPL handoff acceptance contract above must pass.
