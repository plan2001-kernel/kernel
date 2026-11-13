# Plan 2001-kernel: target architecture

This is the authoritative **target** architecture, not a claim about working
APIs. Plan 2001-kernel is experimental on AMD64 and ARM64. There is no Unix,
POSIX, Plan 9 or historical ABI compatibility obligation and no fixed syscall
count. See [kernel interface](KERNEL-INTERFACE.md) for proposed mechanisms and
current ABI status.

## ObjectSpace and authority (target)

The [ObjectSpace architecture](OBJECTSPACE.md) supersedes the
OwnerID-equality-only permission model from 11 November 2026.
An ObjectSpace is a **first-class kernel object** containing labeled,
typed, rights-bearing bindings to any object, including other ObjectSpaces.
It is an independent directed multigraph, not a directory, storage
partition, Pgrp group or process tree. Multiple labels, aliases and cycles
are valid. A user-space interface may project the graph as a directory,
project, container, module or room. There is no mandatory root/CWD/path/parent.

Object identity is separate from authority. A Handle is a process-local
generation-checked capability; a BindingID identifies one relation; its
rights cannot exceed the source capability. A label or raw OID cannot mint
rights. OwnerID remains immutable provenance/lifecycle responsibility and
a trusted policy input for issuing initial capabilities, NOT a general
bypass for a process with the same name. The legacy equality gates have
been removed from current GE/O, region, device and process providers.
Their authorization now uses capability admission, GE/O Ohand scope,
trusted bootstrap gates and provider-specific safety checks.

The implemented ObjectSpace runtime uses the existing five-call object ABI.
Processes have explicit ObjectSpace roots. GE/O v2 persistence supports one
checksummed graph snapshot per durable OID, and
trusted boot can select one graph with `plan9.ini obootgraph=<OID>` and
restore it automatically into the bootstrap process as an explicit Handle.
Persistent graph garbage collection, full cross-volume references and
physical-hardware verification remain unfinished.
GE/O v2 has one flat OID allocator and publication domain per volume;
there is no SpaceID bucket or implicit storage hierarchy. No prior ABI, on-disk-format or Unix/Plan 9 compatibility
is required. All phases and limitations are defined in
[ObjectSpace](OBJECTSPACE.md).

## Independent processes and object roots

A process is an independently schedulable execution context. It has
explicit capabilities to one or more ObjectSpaces, regions, devices,
services and other resources. Neither its launcher nor a parent directory
automatically confers authority. Trusted program registration may set a
program OwnerID and give its process explicit initial capabilities.
Process-control and completion observation are independently delegated.
Current Pgrp/OContext namespace handling is a legacy implementation
to remove after ObjectSpace-root support replaces it.

## Memory-publication IPC (target)

**Keep the implemented memory-publication IPC. Do not add a send/receive
transport, message queue, FIFO, Pipe, ObjectFIFO or generic kernel IPC
provider.** Existing regions have one RW publisher, RO observers, atomic
release/acquire publication and blocking REGIONWAIT/REGIONWAKE.

The publisher owns the source objects and underlying publication region.
Readers receive only the offered data values. RO mappings are delivery
views, not ownership or direct modification of publisher-owned objects.
Data independently stored by the receiver belongs solely to the receiver.
Page-table RW/RO protection is mandatory for memory safety independently
of ObjectSpace capability admission.

Retain existing region-relative references, release/acquire publication,
immutable visible snapshots, blocking race-free WAIT/WAKE, spurious-wakeup
rechecks, mapping lifetime, writer-death behavior and reclamation rules.
WAIT/WAKE do not transmit payload bytes. Userspace defines any higher-level
format. Current capability handles, grants and revocation are incomplete foundations
for the new ObjectSpace authorization design.

## Devices and console (target)

Keep low-level drivers, scheduler, early-boot/panic diagnostics and device
isolation. A device has one immutable owner but can explicitly delegate attenuated
device capabilities across owners; publication remains another option. Exclusive hardware
claims and memory protections are safety constraints, not Unix-style
object-right classes. No new transport or userspace service is required.

### Console/session contract: kernel-owned minimum and future invariants

The kernel owns the framebuffer, persistent SystemConsole, physical keyboard
focus, and all initial boot sessions. **No user-space init or console-owner
process is required.** A boot process receives three attenuated
capability-region grants: read-only stdin, writable stdout and writable
stderr/diagnostics. The existing publication ring is the sole transport;
there is no new IPC mechanism, path/FD console or userspace framebuffer grant.
The OEXECUTE boundary discards bootstrap Handles and installs each process's
three console grants again after the exec commit.

One framebuffer has one selected view. Ctrl+Alt+F1 selects SystemConsole;
Ctrl+Alt+F2 through F9 select independent manifest slots 1 through 8.
The kernel retains process output off-screen while other sessions continue
running. Process exit cannot destroy the kernel console or another session.
Kernel diagnostics remain in kmesg, separate from REPL output. The SystemConsole
shows diagnostic history and recent lifecycle/focus events; a kernel command
interpreter is deliberately absent. PS/2 may be unavailable on some machines;
in that case the kernel remains running, but interactive switching is not
provided by this minimum implementation.

The following **C1-C10 are target invariants**, not a claim that all recovery
features have shipped:

| ID | Target invariant | Minimum implementation |
|---|---|---|
| C1 | SystemConsole does not depend on user processes. | Kernel framebuffer view exists independently. |
| C2 | Failure of one process does not terminate another console session. | Independent boot roots, isolated regions. |
| C3 | Kernel can reclaim console control independently of user space. | Kernel owns focus; full emergency recovery later. |
| C4 | stdin/stdout/stderr belong to a process session. | Three capability regions, no shared direct framebuffer. |
| C5 | Switching sessions does not change ownership, rights or process lifetime. | Kernel focus/view state only. |
| C6 | Kernel diagnostics survive user REPL failure. | Kernel kmesg remains distinct. |
| C7 | A recovery console needs neither GE/O nor a network stack. | Later: UART/L2 recovery and independent boot recovery. |
| C8 | Remote commands require authorization and authentication. | Later: no remote console implemented. |
| C9 | Panic path cannot allocate memory or depend on userspace. | Later: interactive panic console; basic panic output retained. |
| C10 | Missing or broken physical console does not stop the kernel. | Kernel does not require successful PS/2 claim; fallback transport later. |

This phase does not implement Ethernet L2 console, UART recovery, interactive
panic-mode input, remote console, multi-volume GE/O, or network loading.
Do not claim those future invariants are verified by the QEMU session test.

## CURRENT IMPLEMENTATION

The implemented kernel interface is specified in one place:
[KERNEL-INTERFACE](KERNEL-INTERFACE.md#current-implementation). It defines
the current object ABI, capability-region mapping and WAIT/WAKE semantics,
process completion model, device ring protocols and their documented
limitations. This document keeps the target architecture and the maturity
summary below rather than maintaining a second copy.

AMD64 has a functional full-system path with GE/O, MicroPython and device
providers. It implements immutable process/object `OwnerID`, trusted
boot/installation program registration, capability-controlled object access,
registered-program launch under the program's owner, and explicitly shared data
through the existing region publication IPC. Stored owners survive restart;
the name/program registry is volatile and is reconstructed by trusted boot
for symbolic program identity. Handle rights authorize cross-owner operations
when explicitly delegated, independently of immutable provenance.
ARM64 has an early UEFI/QEMU boot
foundation with BootInfo handoff, mappings, console, timer interrupts and
physical-page allocation, not a full-system scheduler, GE/O, syscalls,
userspace or device parity. MicroPython is the proof-of-concept userspace.
Source and tests govern exact current behavior.

## Completed six-stage implementation roadmap (historical)

The phases below record the historical implementation; the current
ObjectSpace migration roadmap is in OBJECTSPACE.md. Code migration requires a
separate, explicitly scoped implementation task.


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
6. **Delete old kernel IPC (completed):** Ethernet receive and console
   sessions were migrated to capability regions and WAIT/WAKE. The
   `ObjectFIFO` and Pipe implementations, provider types, ABI, MicroPython and
   libc adapters, device adapters, obsolete tests and contradicted docs were
   physically deleted, along with the kernel IPv6/ARP/ESP stack. Boot, raw L2
   Ethernet and the console/REPL handoff contract pass on AMD64.
