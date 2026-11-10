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

## CURRENT IMPLEMENTATION

AMD64 has a functional full-system path with GE/O, MicroPython and device
providers. `SPAWN` creates a child with a fresh `Pgrp`; `RFORKEX` transfers
planned Handles; `PROCWAIT` waits for a direct child. `OTERMINATE` and exit
logic depend on parent relationships. `Proc.parent/parentpid/nchild/waitq`,
`Pgrp/OContext` sharing/copying/sealing and namespace views remain in source.
`ObjectFIFO` carries framed IPC and Ethernet receive data; Pipe and current
console paths also remain. The `-I` REPL directly claims PS/2 input;
`ObjOpenConsole` and `stdoutpipe` route selected output. These are current
facts, not target requirements. Independent RW-writer/RO-observer mappings,
atomic publication, RO WAIT and free-process grants are not implemented as
specified above.

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
   IPC. Boot and minimal console/REPL must still work.
