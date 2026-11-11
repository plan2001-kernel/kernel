# Plan 2001-kernel: target architecture

This is the authoritative **target** architecture, not a claim about working
APIs. Plan 2001-kernel is experimental on AMD64 and ARM64. There is no Unix,
POSIX, Plan 9 or historical ABI compatibility obligation and no fixed syscall
count. See [kernel interface](KERNEL-INTERFACE.md) for proposed mechanisms and
current ABI status.

## Identity and authority

Each kernel object has exactly **one** named owner (`OwnerID`), set at creation
and unchanged until destruction. Names such as `root`, `user`, `editor` and
`network` designate identities, not Unix accounts or directories. A trusted
boot/installation mechanism registers stable owner identities. Processes
cannot forge or self-select their owner; multiple processes can run under
the same owner and form one trust domain.

The only general direct-object-access rule is
`process.owner == object.owner`. The owner may perform all operations
supported by that object. The target has **no** UID/GID, owner/group/others,
rwx, ACL, chmod/chown, setuid, capability rights or attenuation, loans,
shared ownership or ownership transfers. `root` is a named owner, not
a universal override. Kernel-internal boot, memory and hardware management
remain privileged implementation mechanisms.

GE/O identity `(SpaceID, OID)`, volatile process identities and runtime
memory/device identities are distinct. A known ID, name, pointer, process
identity or object-set membership does not grant ownership. Names and
relations are data, not a native directory tree or a source of authority.

### Program ownership, registration and launch (target)

A program has its own named owner. Its executable object, configuration
and persistent objects belong to that owner, not the user/process that
starts it. Trusted installation/boot metadata records
`{program name, OwnerID, executable object reference, startup policy}`.
This registry is part of existing kernel boot/process management, **not
a new userspace service**.

At boot the kernel starts registered autostart programs. Later, a process
may request the launch of a registered program by name. The kernel validates
the registry and startup policy, loads the executable as a privileged kernel
operation and starts a new independent process **as the program's owner**.
The requester does not gain access to the executable or program objects,
does not become the process owner or obtain process-control authority,
and does not create a parent/child relationship. The initial launch policy
may allow any registered program to be requested; resource limits and
forced termination are separate kernel lifecycle policy.

Input, output, modification requests and voluntary shutdown requests are
data offered through the **existing IPC**. A program decides whether to
act on requests about its own objects. Forced process termination remains
a kernel lifecycle operation controlled by its owner and defined kernel
policy, never automatically by the launcher.

### Owner-offered data (target)

An owner may offer data to another owner using the existing IPC transport.
The **received data value belongs entirely to the receiver**; data stored
as a new object is receiver-owned. The original object remains owned by
the sender and cannot be directly changed by the receiver. A receiver may
request a change, which only the sender's program may perform. No kernel
borrow, transfer, grant, new send/receive ABI, message pipe or provider
is implied. Read-only publication views are an IPC transport detail,
not access to manage the publisher's original objects.

## Independent processes and resource graph (target)

Processes are independently scheduled contexts identified by a trusted
program owner. No privileged ancestry or process tree exists; a launcher
does not control, own or inherit its launched process. A process may outlive
its launcher. Processes with the same OwnerID can access their own owner's
objects. Other owners interact only through offered data.

Creation, voluntary exit, forced termination and completion observation are
separate process lifecycle concerns. Forced termination is limited to the
process owner or explicit kernel lifecycle policy, not the launcher. Boot
process designation is explicit, not derived from `parentpid == 0`.

Namespace views, if retained, are lookup/data constructs and not authority.
Stored namespace definitions and GE/O object-set membership are data only.
No native hierarchy, graph database or kernel tag language is required.

## Memory-publication IPC (target)

**Keep the implemented memory-publication IPC. Do not add a send/receive
transport, message queue, FIFO, Pipe, ObjectFIFO or generic kernel IPC
provider.** Existing regions have one RW publisher, RO observers, atomic
release/acquire publication and blocking REGIONWAIT/REGIONWAKE.

The publisher owns the source objects and underlying publication region.
Readers receive only the offered data values. RO mappings are delivery
views, not ownership or direct modification of publisher-owned objects.
Data independently stored by the receiver belongs solely to the receiver.
Page-table RW/RO protection and mapping checks remain necessary for memory
safety, not general per-object permissions or persistent capability rights.

Retain existing region-relative references, release/acquire publication,
immutable visible snapshots, blocking race-free WAIT/WAKE, spurious-wakeup
rechecks, mapping lifetime, writer-death behavior and reclamation rules.
WAIT/WAKE do not transmit payload bytes. Userspace defines any higher-level
format. The capability handles, grants and revocation described under
CURRENT IMPLEMENTATION in KERNEL-INTERFACE.md are facts of today's ABI,
not requirements of the single-owner target.

## Devices and console (target)

Keep low-level drivers, scheduler, early-boot/panic diagnostics and device
isolation. A device-facing object has one owner; other owners receive only
data that owner offers over the existing publication IPC. Exclusive hardware
claims and memory protections are safety constraints, not Unix-style
object-right classes. No new transport or userspace service is required.

### Console/session contract (target)

The keyboard owner processes raw device input and offers it to the active
session through the existing region-publication transport. Sessions offer
output and diagnostics back through that same transport. One RW publisher,
RO observers and WAIT/WAKE remain the IPC mechanics. Input routing, focus,
echo, editing and control-key behavior are decisions of the owning code.

Keep input isolated between sessions; deliver Ctrl-C and Ctrl-D correctly,
support bounded buffering and backpressure, and preserve protected kernel
boot/panic diagnostics. Trusted restart of the same registered owner may
recover a failed device-facing process. Phase 6 evidence describes the
current implementation, not a request to add programs or change IPC.

## CURRENT IMPLEMENTATION

The implemented kernel interface is specified in one place:
[KERNEL-INTERFACE](KERNEL-INTERFACE.md#current-implementation). It defines
the current object ABI, capability-region mapping and WAIT/WAKE semantics,
process completion model, device ring protocols and their documented
limitations. This document keeps the target architecture and the maturity
summary below rather than maintaining a second copy.

AMD64 has a functional full-system path with GE/O, MicroPython and device
providers. It implements immutable process/object `OwnerID`, trusted
boot/installation program registration, owner-checked direct object access,
registered-program launch under the program's owner, and owner-offered data
through the existing region publication IPC. Existing Handle rights remain
an addressing/attenuation mechanism in the current ABI; they cannot authorize
management of another owner's object. ARM64 has an early UEFI/QEMU boot
foundation with BootInfo handoff, mappings, console, timer interrupts and
physical-page allocation, not a full-system scheduler, GE/O, syscalls,
userspace or device parity. MicroPython is the proof-of-concept userspace.
Source and tests govern exact current behavior.

## Completed six-stage implementation roadmap (historical)

The phases below document the prior capability-era implementation. They
are **not** requirements to reintroduce capability rights or redesign IPC
for the single-owner ownership target above. Code migration requires a
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
