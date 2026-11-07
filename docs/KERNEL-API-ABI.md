# Plan2001-O kernel API/ABI: terminology and boundary contract

**Status:** normative vocabulary and migration rules for **new Plan2001-O code**; proposed API/ABI design, **not an implemented or frozen binary ABI**.
**Volatile-object implementation addition:** the RAM-only scope-qualified object
identity, rights-bearing handles and ObjectFIFO TEE channel are specified in
[VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md). Persistent GE/O
storage remains the existing provider and its v1 bytes are unchanged.
**N13 implementation addition:** the narrow experimental object-view grants,
per-call O authorization and explicitly inherited Pipe endpoints are specified
in [N13-OBJECT-CONTEXT](N13-OBJECT-CONTEXT.md). That owner-assigned slice
implements these particular interfaces; the general resource-model questions
below remain open.
**2026-11-06 interpretation note — “exactly five syscalls” is superseded:** the
five universal object operations `OBJCREATE(136)`, `OBJOPEN(137)`,
`OBJCLOSE(138)`, `OBJBIND(139)` and `OBJINVOKE(140)` remain the general object
boundary, but they are **not** the entire syscall ABI. T7 restored direct
`BRK_(24)`, `SLEEP(17)`, `NOTIFY(28)`, `NOTED(29)`, `RFORK(19)`,
`OTERMINATE(93)` and `EXITS(8)` for simple process-local operations. The
current table was observed as 135 named / 62 active / 73 reserved. The goal is
a small, coherent, understandable interface, not a numeric count; a simple
operation must not be forced through the object calls merely to preserve a
count. Historical T0–T7 reports are evidence of their rounds, not current
quotas. See [ROADMAP-T8-T16](ROADMAP-T8-T16.md).

**T12 implementation addition (2026-11-07):** the common message-device ABI
and one native USB control-endpoint integration are defined in
[DEVICE-ABI](DEVICE-ABI.md), with current validation in
[T12-DEVICE-RESULT](T12-DEVICE-RESULT.md). The catalog/session objects use the
existing Vhand core and USB endpoint ownership; complete responses are ordinary
immutable bytes objects. There is no new syscall or generic driver callback
table. Earlier USB metadata-only statements describe the pre-T12 slice; the
legacy ObjOpenUsb/UsbEp interface itself remains metadata/open-only.

**Revision:** 0.1, 2026-10-24.
**Source baseline inspected:** `main` at `373497917be36e03e36fd5f6a92d880fcf2a8bf2`. Source references are evidence at that revision, not eternal line numbers.
**Audience:** coding agents implementing or reviewing kernel, low-level C userspace, MicroPython bindings, and source-to-source migration. This is a working contract, not a general OS survey.
**Task boundary:** this document makes **no code changes**, adds **no syscall**, and does not authorize removal of working legacy paths.

## 0. Agent entry: rules to apply before editing code

1. **One concept, one canonical name.** In all new API/ABI names, type declarations, design documents, tests and agent reports, use the exact term defined in section 2 for that concept. Never swap `Queue`, `Stream`, `Pipe`, `Handle`, `Channel`, `File` or `OID` for stylistic variety. Same operation and semantics imply the same term; a different contract requires a different name.
2. **Keep meanings separate.** Hardware state, buffered data structures, transfer protocols, persistent data, resource references, and authorization are distinct layers, even if existing code happens to implement several in one C function.
3. **Reuse existing names when the meaning matches.** The kernel's real `Queue`, `Block`, `Pipe`, `Proc`, `VGAscr`, `Memimage`, `QLock`, `Rendez` and `Ohand` are starting points. Their existence does not mean they are safe or suitable for a public ABI.
4. **Do not make `Dev` a new generic `Device` class.** Prefer concrete C functions plus records with domain-specific names. Shared interfaces require a proven shared mechanism, not a universal `open/read/write/close` function table.
5. **A kernel C API is not a syscall ABI.** Kernel pointers never cross into userspace as usable pointers to kernel records. A handle value, a persistent OID, a C pointer and a permission grant are four different things.
6. **Never claim a proposed facility is implemented.** Use `EXISTING`, `LEGACY`, `PROPOSED` and `OPEN` as defined below, including in tests and reports.
7. **Migration is additive until tested.** Introduce/verify a concrete native operation first; let a legacy `Dev` branch delegate to the same logic; remove legacy routing only when all remaining callers and security/lifetime obligations are accounted for.
8. **Do not silently invent semantics.** When this contract or the inspected source does not settle allocation, EOF, blocking, framing, transfer, rights, memory lifetime, cancellation, error representation or ABI encoding, mark it `OPEN` and obtain a design decision before exposing a public interface.

**N10 note:** the N10 source move removed the inherited `sys/src` userland tree. Current kernel paths are `kernel/`, `ge/storage/`, `boot/efi/`, `runtime/` and `include/`; references below that name a removed legacy component are historical evidence, not files present in the compact tree.

**Terms are normative; sample proposed signatures are NOT declarations to copy verbatim.** The authoritative implemented signature always comes from the corresponding C header/source.

### Status vocabulary

| Status | Precise meaning |
| --- | --- |
| `EXISTING` | Concrete structure/function/behavior inspected in the current repository; still verify when editing another revision. |
| `LEGACY` | Existing inherited interface retained for compatibility, not the intended basis for a new native interface. |
| `PROPOSED` | Intended semantic distinction or migration direction; no implication that a named API/ABI exists. |
| `OPEN` | Contract or implementation choice not yet accepted. Do not guess. |

An existing mechanism can also be LEGACY. A semantic rule may be binding for new code while its implementation remains PROPOSED.

## 1. Four boundaries that must not collapse

`Hardware/controller ↔ kernel implementation ↔ kernel C API ↔ userspace ABI/client API`

- **Hardware/controller**: IRQs, I/O ports, mapped memory, DMA, device-specific operations, concrete device state.
- **Kernel implementation**: `Queue`, `Block`, `Lock`, `QLock`, `Rendez`, scheduling, buffering, format conversion, reference counting. Implementation details may change without becoming ABI.
- **Kernel C API**: directly callable C functions and C records for a defined operation, with documented caller context, ownership, locking, errors and side effects. Passing `T*` to another trusted kernel function is ordinary C, **not a userspace handle**.
- **Userspace ABI**: syscall entry/number, argument widths/layout/alignment, validated user memory, ownership, rights, lifetime, return/error encoding, blocking and compatibility rules. A language-level client API such as a MicroPython module is **not identical to** this ABI.

A fifth, independent concern is **GE/O persistent identity and contents**. It is not a device registry, kernel-memory pointer scheme or implicit live-resource namespace.

**Required proof for a "native/no Chan" claim:** trace actual called functions in the measured execution path and account for `Chan`, `Qid`, `namec`, `walk`, `Dev` dispatch, blocking/locks and authorization. An operation may be Chan-free without the whole driver or kernel becoming Chan-free.

## 2. Canonical glossary — authoritative definitions

The **canonical token** below is what new code and design prose must mean. `Source` names existing evidence, not a proposed new header. Terms shown as EXISTING can remain in inherited function names, even if an eventual public API is refactored.

| ID | Canonical term | Exact meaning and exclusion | Status / source |
| --- | --- | --- | --- |
| T01 | `Queue` | In-kernel **buffering and synchronization structure** for queued `Block` data; supports `qread`, `qwrite`, `qproduce`, `qpass`, `qclose` etc. Neither an end-user stream type nor a resource identifier. `Qmsg` alters framing behavior, so do not assume all queues have byte-stream semantics. | EXISTING: `kernel/core/qio.c`, `portdat.h`, `portfns.h` |
| T02 | `Block` | Kernel-managed contiguous payload buffer with metadata/linkage used by queue and network paths. **Not** a disk block, an OID or a whole logical file. | EXISTING: `qio.c`, `portdat.h` |
| T03 | `Stream` | **Transfer/access semantics** for ordered bytes across an interface, without requiring random-access offsets or persistent storage. May be implemented using `Queue` or another mechanism. It is not the name of a `Queue` C struct and not an OID. | PROPOSED native boundary, already conceptually defined in `docs/OBJECT-MODEL.md`; inherited code also uses the word descriptively |
| T04 | `Pipe` | A **connection** joining two communicating ends; in inherited `devpipe.c`, a `Pipe` contains two `Queue*` and lifetime/reference accounting. Not a synonym for `Queue` or `Stream`. A pipe can provide stream transfer. | EXISTING structure, LEGACY `Dev` exposure: `kernel/core/pipe/devpipe.c` |
| T05 | `Endpoint` | One independently usable **end of a connection** such as a pipe. An endpoint has direction/rights/lifetime under its eventual contract. **Not** automatically a `Chan`, fd, queue, OID or dedicated C type. | PROPOSED term for native connection APIs; ABI representation OPEN |
| T06 | `FIFO` | **Ordering property**, first-in-first-out; not a generic kernel subsystem or a second name for `Queue`/`Pipe`. Use only to state ordering behavior actually provided. | Terminology rule |
| T07 | `Message` | A bounded **unit of transferred data whose boundary matters**, rather than an arbitrary portion of a byte stream. `Qmsg` in `qio.c` is existing message-mode behavior; do not equate it with a specific public message ABI. | Existing internal queue mode; public contract OPEN |
| T08 | `Event` | A **reported occurrence**, with an event-specific contract (type, payload, ordering/time if relevant). Can be buffered in `Queue` or transferred over a `Stream`, but is not itself either. Raw keyboard scancodes do not become normalized `KeyEvent` records merely by renaming them. | PROPOSED general semantic term |
| T09 | `Handle` | A **lifetime-limited process-visible reference** resolved by kernel-maintained state. A handle is not a memory pointer, `fd`, `Chan`, `OID` or grant by itself. Which object it references and its attached rights must be specified per ABI. | PROPOSED general term; concrete narrower O Space implementation exists as `Ohand` |
| T10 | `Capability` | A **validatable authorization** for specified operations on a resource, with an explicit holder, scope and transfer/revocation rules. A numeric handle is not automatically a capability. Avoid this term for an unchecked integer or a mere global mask. | PROPOSED security contract; general implementation OPEN |
| T11 | `OID` | Persistent identifier of a stored occurrence, scoped to its defined GE/O `Space` (currently 64-bit). Not a byte address, `Handle`, `Chan`, path, global UUID or resource permission. | EXISTING scoped identity: `docs/O-FORMAT.md`, `docs/OBJECT-MODEL.md` |
| T11a | `Volatile object` | Object whose state and identity exist only in RAM for one boot incarnation. Creation performs no GE/O read or write. It may have empty or arbitrary content and may expose behavior (ObjectFIFO) without a class hierarchy. | IMPLEMENTED: `kernel/core/sysvobj.c`; [VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md) |
| T11b | `Scope-qualified identity` | `(incarnation, space, oid)` for a volatile object. The incarnation is per-boot, the space is a caller scope tag (0 runtime/default), and the oid is monotonic and never reused while that incarnation lives. It is not a GE/O disk address and does not change GE/O v1. | IMPLEMENTED: `runtime/abi/vobj.h`, `kernel/core/sysvobj.c` |
| T11c | `ObjectFIFO` | General message-framed channel carrying authorized references to immutable objects, plus an optional opaque envelope. It is itself a volatile object, not a synonym for `Queue`, `Pipe`, `Stream`, TCP, or a NetFrame queue. | IMPLEMENTED: `kernel/core/sysvobj.c` |
| T11d | `Subscription` | A receiver-side ObjectFIFO attachment with its own bounded queue, cursor and overflow counter. Independent subscriptions implement TEE/fanout. It is represented to a process by a rights-bearing subscription Handle. | IMPLEMENTED: `kernel/core/sysvobj.c` |
| T12 | `Space` | GE/O persistence scope containing OID-addressed contents/versions. Not a process address space, device file tree or a `Queue`. | EXISTING O operations, `sysgefs.c`; native graph relations remain a separate development target |
| T13 | `File` | **Finite byte content** of an identifiable stored occurrence in the Plan2001-O object model; excludes mandatory pathname/owner/stat/directory/Unix inode metadata. Not a `Stream` or `Pipe`, although a stream may transfer its bytes. | Normative architecture: `docs/OBJECT-MODEL.md`; storage profile described separately in `docs/O-FORMAT.md` |
| T14 | `Proc` | Kernel process record/current execution context. A process's PID is an integer observation, not an object pointer or `Handle` to an arbitrary process. | EXISTING: `portdat.h` |
| T15 | `Driver` | Code plus state that controls a **concrete device/controller class** (e.g. PS/2 controller, framebuffer). No universal `Dev` callback table required. | PROPOSED architectural use; concrete inherited code EXISTING |
| T16 | `Service` | Kernel-provided operation not necessarily tied to a physical device, e.g. process information, clock, logging or reboot. Do not categorize all `#c` functions as console hardware. | PROPOSED architectural use; concrete existing functions |
| T17 | `Console` | Textual diagnostics/input/output policy, potentially routed to screen or UART. Not the same as graphical `Screen`, raw framebuffer or `Keyboard` hardware. | EXISTING: `devcons.c`, `putstrn/putstrn0` and related routines |
| T18 | `Screen` | Visible **presentation surface** used by drawing/display. The name is a conceptual surface, NOT a declaration that a `Screen` C struct exists. Use actual `VGAscr`, `Memimage` and `Rectangle` where required by existing code. | EXISTING subsystem: `kernel/amd64/screen.c`; proposed native API OPEN |
| T19 | `Framebuffer` | Mapped or otherwise addressable **pixel storage** with format, dimensions, stride and access/flush rules. Not an application drawing protocol or necessarily the whole `Screen` state. | EXISTING implementation: `screen.c`, `bootscreeninit` |
| T20 | `Keyboard` | Physical/logical **input device source** with controller state, input delivery and optional LED control. Do not conflate PS/2-specific operations with a universal USB/HID protocol. | EXISTING PS/2 implementation: `kernel/amd64/devkbd.c`; common API OPEN |
| T21 | `Scancode` | Raw controller/protocol key code delivered by the existing PS/2 path, currently bytes in a queue. No implied Unicode code point or normalized key semantics. | EXISTING `Qscancode`: `devkbd.c` |
| T22 | `KeyEvent` | Interpreted/normalized key event **only if** a translation layer defines encoding, key state, modifiers and ordering. Not interchangeable with `Scancode`. | PROPOSED only; no general native `KeyEvent` ABI approved |
| T23 | `Dev` | Inherited Plan 9 device interface table. N11.8.4 removed the dead callback layout and N11.8.5 removed the empty registry entirely: the canonical build has no `struct Dev`, no `devtab[]`, no `devno()`, and `kernel/core/dev.c` is not compiled. There is no replacement registry or `Dev` adapter. Dormant arm64 sources still mention the legacy types but are outside the amd64 build. | REMOVED from canonical build: `portdat.h`; `dev.c` unmanifested |
| T24 | `Chan` | Historical **Plan 9 channel** type. N11.8.4 removed the full struct; N11.8.5 removed the last compiled SD/LED pointer signatures and the incomplete typedef. Do not use "channel" as a synonym for `Queue`, `Stream` or native endpoint. | REMOVED from canonical build: `portdat.h` |
| T25 | `Qid` / `Walkqid` | `Walkqid` is removed. `Qid` remains only as the GEFS/`ge/storage/dump.c::Qconv()` formatting type defined in `kernel/core/lib.h`; it is not a driver directory identity, native operation identity, graph relation or OID. N11.8.5 removed network `Proto.qid` and the #I Qid-directory encoding; the native conversation bound is the plain numeric `Maxconv = 4096` (identical to legacy `Maskconv + 1`), not an identity. The inherited `Dirtab`/`Devgen` directory helpers were removed by N11.7D.2. | LEGACY FORMATTING ONLY: `kernel/core/lib.h`, GEFS dump; network bound in `kernel/net/ip.h` |
| T26 | `fd` (legacy) | Historical Plan 9 file-descriptor index and `Fgrp` table mapping to `Chan*`. N11.8.2 removed the FD success paths and N11.8.3 removed `Fgrp`; the syscall slots remain validation/failure-only and are not a `Handle` table. | RETIRED/LEGACY ABI: `sysfile.c` slots, `Proc.fdgrp` presence token |
| T27 | `Ohand` / `Ohtab` | Existing **O-Space-specific** reference and per-process slot table used by native `OSPACE*` calls. This is a concrete instance of handle-like behavior, **not** a universal, rights-bearing capability system. | EXISTING: `ge/storage/sysgefs.c` |
| T28 | `Lock` / `QLock` / `Rendez` / `Ref` | Existing distinct kernel synchronization and lifetime primitives; none is a `Queue` merely because of the letter Q in `QLock`. Use actual types and proper interrupt/sleep context. | EXISTING: `portdat.h`, `qio.c` |
| T29 | `SDev` / `SDunit` / `SDifc` | Concrete C storage-controller registry, live unit and controller-operation interface. `SDifc` is a controller interface (`pnp`/`enable`/`verify`/`online`/`bio`/`rio`/...), not a file-like `Dev`. A live `SDunit*` is kernel state, not an OID, pathname, userspace `Handle` or `Capability`. Native block calls use an absolute **unit byte offset**. The canonical build has no compiled `#S` adapter, no `SDfile`/`SDrw` virtual-subfile metadata and no `sdaddfile()`; `sdcore.c` owns registry/unit/block operations. AHCI enclosure LED hardware control (`ledkproc`/`blink`/`blinkesb`) and NVMe controller initialization/identify/`nvmerctl` remain; only their unreachable #S subfile registrations were removed. | EXISTING/IMPLEMENTED: `kernel/core/sd.h`, `sdcore.c`, `kernel/amd64/sdiahci.c`, `kernel/core/sdnvme.c` |

### Disambiguation requirements

- "queue of keyboard scancodes" means a `Queue` whose payload is `Scancode` data; it does **not** mean a `KeyEvent` ABI or a generic `Stream` object type.
- "stream backed by a queue" means `Stream` transfer behavior implemented with `Queue`; neither name may replace the other.
- "pipe endpoint" refers to one end of `Pipe`. Its native representation may eventually be a `Handle`, but no such ABI is yet approved.
- "native O Space handle" means `Ohand/Ohtab`-mediated process-local access. An `OID` denotes persisted identity **within** that space, not the handle.
- "capability" only when a documented access right is actually validated. Today's `Proc.iomask` and inherited permission checks must be described by their **real** semantics; they do not automatically implement the proposed generic capability model.
- `Queue` and `QLock` are unrelated concepts, regardless of naming resemblance.
- A source code variable named `channel` does not establish that its value is a Plan 9 `Chan`; always check its declared type. Conversely, a `Chan*` remains legacy even when no path lookup occurred on a particular call.

## 3. Data movement model and contracts

All terms below are independent axes; do not collapse them into "an I/O object."

| Axis | Allowed distinction | Representative current mechanism | Requires an explicit decision when exposed |
| --- | --- | --- | --- |
| Information persistence | `File`/OID content and published version | GE/O `Space`, OID, get/put/commit | Publication/commit, version, errors |
| Transfer semantics | `Stream` (ordered bytes), `Message` (boundary preserved), discrete `Event` | `Queue` can serve different modes | Ordering, truncation, EOF, cancellation, blocking |
| Buffering | `Queue` and `Block` | `qio.c` | Boundedness, backpressure, allocation |
| Connection | `Pipe` with `Endpoint` roles | `devpipe.c` uses two `Queue*` | Direction, peer close, ownership, sharing |
| Authority/lifetime | `Handle` resolved for a process; `Capability` is a verified right | O `Ohand`, separate `iomask`, legacy `fd` | Ownership, grant, transfer, revoke, exec/exit |
| Device/action | `Driver` concrete operation, or `Service` operation | i8042, screen, console, `Proc` | Initialization, IRQ safety, privileges |

**Examples of semantics, not new APIs:**

```text
PS/2 interrupt -> Scancode bytes -> Queue -> approved reader
                                          ^         ^
                                          |         |
                                     buffering   transfer/API/ABI contract
```

```text
Pipe: Endpoint A <-> two internal Queue buffers <-> Endpoint B
                      (devpipe implementation)
```

```text
Space + OID -> finite File content/version
        |             |
    persistent     transferred in chunks as needed
    identity       (transfer does not change identity)
```

**In particular:** a stream is not guaranteed to persist; a file is not guaranteed to be an in-RAM byte array; a queue is not inherently a public stream; and a pipe does not have to be registered as a persistent OID.

## 4. API vs ABI: requirements and explicitly undecided parts

### 4.1 Kernel C API: direct, concrete, domain-specific

- Name a function for its **effect**, not for an artificial file operation (`present`, `flush`, `setleds`, `getscancode`, `transmit`, `readblocks`, `parentpid` as appropriate).
- Use a pointer to an existing concrete C record when needed for controller/surface/instance state. Passing many related arguments in a C record is allowed; do not invent universal `Device`, `Object` or `IoRequest` wrappers.
- Reuse the source's actual data types and lifetimes; e.g. the framebuffer code uses `VGAscr`, `Memimage` and `Rectangle`, not a verified type named `Display`.
- The kernel C contract for each operation **MUST** specify: required initialization; permissible execution context (IRQ, kproc/process, sleep); ownership and lifetime of records/buffers; input/output validity; locking; return/errors; side effects; reentrancy; and whether it uses any legacy `Chan` path.
- Do not hand-roll a second queue, timer, mutex or memory allocator when an existing kernel primitive with matching semantics suffices.
- Domain-specific operations may legitimately use `read`/`write` **when they truly transfer bytes**. Prohibition concerns turning every device action into a fictitious file with offset/stat/walk.

**Current executable examples, not proposed API:**
- `putstrn(char*, int)`: kernel text-output path; `devcons.c`.
- `flushmemscreen(Rectangle)`: screen-memory flush operation; `pc/screen.c`.
- `qread(Queue*, void*, int)`, `qwrite(Queue*, void*, int)`: queue operations; `qio.c`.
- `i8042intr` produces raw bytes into `kbd.q` through `qproduce`; `devkbd.c`.

**Design intent, not implemented signatures:** direct kernel calls for presenting dirty screen regions, reading raw keyboard scancodes, setting keyboard LEDs, and querying process information. Select their exact C identifiers and prototypes only after reviewing actual callers/types; do not code an imagined `Display*` or `Keyboard*` as though existing.

### 4.2 Userspace ABI: stable crossing, not direct kernel pointers

An eventual public operation MUST define:

| Field | Required contract |
| --- | --- |
| Operation | Stable semantic name and concrete effect; syscall number/dispatch, if a syscall is chosen |
| Identity/reference | Which process-visible handle or other explicit identifier is accepted; how resolved and validated |
| Access | Required grant, who bestows it, what is checked on each call, transfer/revocation policy |
| Memory | Integer widths, signedness, pointer/buffer direction, layout/size/alignment, user address checks |
| Lifetime | Creation/acquisition, duplicate/share (if supported), exec inheritance, process exit and close |
| Execution | Blocking, wakeups, cancellation, concurrency, interrupt safety, ordering and partial transfers |
| Result/error | Return values, errors, EOF/hangup, invalid handle, denied rights, device absent |
| Version | Target architecture (amd64), call convention and compatibility/version negotiation where necessary |

An ordinary userspace `Handle` is not a `T*`; a kernel must resolve it before calling a trusted internal C function. A Python `import`/method name is a *language binding* above the ABI, not a specification of its implementation or persistent graph type.

**No general handle table, generic capability encoding, keyboard/display syscall numbers, stable `KeyEvent` structure or generic dispatch syscall is authorized by this document.** N13 originally added a service-specific UDP6 endpoint family; that endpoint is now a provider behind the five-object ABI, while the legacy numbers remain compatibility entry points. Neither authorizes a generic resource table.

### 4.3 Concrete existing ABI evidence

- The pathname/FS slots `BIND`, `CHDIR`, `EXEC`, `_MOUNT`, `OPEN`, `_STAT`, `CREATE`, `FD2PATH`, `REMOVE`, `_WSTAT`, `_FWSTAT`, `UNMOUNT`, `STAT`, `WSTAT`, `FWSTAT` and `MOUNT` are **RESERVED** in `runtime/abi/sys.h`. Their generated `systab` entries are now `nil` and `sysreserved[]` marks them, so a raw call returns `Ebadarg` without reaching any removed implementation. The old `sysunsupported()` wrapper and its generated special case are gone; the numbers are never renumbered or reused.
- The always-failing legacy FD slots `CLOSE`, `DUP`, `_FSTAT`, `_READ`, `OSEEK`, `_WRITE`, `PIPE`, `SEEK`, `FSTAT`, `PREAD` and `PWRITE` are **RESERVED** the same way: `systab` entries are `nil`, no `sysclose`/`syspread`/... handler exists, and a raw call returns `Ebadarg`. `IOWRITE` remains the separate functional native output grant. `PIPENEW/PIPEREAD/PIPEWRITE/PIPECLOSE` are now **RESERVED** as well (see the phase-3 retirement below). `sysfstat`/`sys_fstat` and path-based `sysstat`/`sys_stat`/`syswstat` are not functional.
- Current O `OVOLOPEN` and `OSPACEOPEN/GET/PUT/CREATE/COMMIT/CLOSE` exist; `sysgefs.c` owns process-local `Ohand` / `Ohtab`, validates user addresses and drops O handles on `oprocexec/oproc exit`. OBJOPEN(ObjOpenSpace) wraps the same authorized Space acquisition, and OBJCREATE(ObjKindGe)/OBJINVOKE(CreatePersist,Commit) expose persistent create/commit through it. This is **not** a general device handle table or permission system.
- Current native `IOWRITE` uses a process `iomask` for constrained output grants (`docs/STATUS.md`, `kernel/core/systab.h`). It does not specify a generic Stream API or a transfer-of-capability protocol.
- `sysiowrite()` in `port/sysfile.c` does not require a UART. Handles 1/2 still require the caller's `iomask` grant; length must be 0–65536 bytes. A zero-length call returns 0 without accessing the buffer. Nonempty calls validate and copy the user buffer, recognize fixed diagnostic milestones, call `putstrn()`, and return the accepted byte count. Neither a UART nor a framebuffer is required: `consputstr()` still applies the existing bounded `kmesg` policy and routes to available sinks. This is acceptance by the console service, not a guarantee of physical display or remote delivery. Syscall numbers and permissions are unchanged.
- USB R3/R4 adds `USBOPEN/USBLIST/USBEPOPEN/USBEPNAME/USBREAD/USBWRITE/USBIOCTL/USBCLOSE` as syscalls 63–70. `kernel/core/sysusb.c` owns a separate, process-local 16-slot `Usbhtab` with `USBSVC`/`USBDATA`/`USBCTL` kinds and generation-tagged integers; these are not fds, `Chan`s, `Ohand`s or capabilities. Data handles also retain their open mode: `USBOREAD` read-only, `USBOWRITE` write-only, `USBOrdwr` both; the check is per handle, not inferred from `Ep.mode`. `USBIOCTL` is explicitly USB-handle-only (typed requests `USBdebug..USBinfo`), and data handles reject it. `kernel/drivers/usb/usbcore.c` owns the single live `eps[]`/`hcis[]` state and exposes kernel C functions without `Chan`/`Qid`/`Dirtab`/`Dir`/`Walkqid`. R4 deletes `devusb.c` and the `#u` Dev; canonical boot calls `usbcore_reset()` directly after `links()`, `usbcore_init()` stays deferred to the first native `USBOPEN` or `OBJOPEN(ObjOpenUsb)`, and `usbcore_shutdown()` is called directly at teardown. R4 had 12 `Dev` entries; G1 removes `#G` leaving 11, and keeps EHCI/xHCI as `link` types. No downstream enumeration, generic device registry or external `usbd` implementation is part of this ABI.
- GE/O G1 relocates the active 15-module engine and four headers to `ge/storage/`, deletes the old, now removed `sys/src/9/gefs/` tree and its retained FS/9P/Fid/Conn/DMOBJ sources, and removes the `#G` Dev. Native `OVOLOPEN`/`OSPACE*`/`OEXECUTE` remain syscalls with the same numbers and process-local `Ohand` lifetime; `sysgefs.c` retains the PARTUUID/SD block provider and an exported direct `sysgefsshutdown()` teardown hook. `sysovolopen` reaches `ovolopenblk` -> `oopenblk` -> `geopenblk` with a bounded block callback and no `namec`, fd table or path fallback. Canonical `pc64` now registers 11 Dev entries; GE/O has no Dev entry.
- N1/N2 originally added pathless native Pipe, keyboard and UART syscalls: `PIPENEW/PIPEREAD/PIPEWRITE/PIPECLOSE` (71–74), `KBDOPEN/KBDREAD/KBDCLOSE/KBDLEDS` (75–78), and `UARTCOUNT/UARTOPEN/UARTREAD/UARTWRITE/UARTIOCTL/UARTSTATUS/UARTCLOSE` (79–85). **Phase 3 (2026-11-05) retires this family and `PIPEINHERIT=100`:** the numbers stay allocated forever with `nil` dispatch entries, `sysreserved=1`, no `sys*` handler symbol and a raw call returning `Ebadarg`. The providers are unchanged internally and are reached only through `OBJCREATE`/`OBJOPEN`/`OBJINVOKE`/`OBJCLOSE`: Pipe (`ObjKindPipe`, `ObjOpPipePeer`, Read/Write), the one-shot `ObjOpPipeInherit=22` for `RFPROC`, UART (`ObjOpUartCount=23`, `ObjOpenUart`, `UartCtl/Status`) and KBD (`ObjOpenKbd`, Read, `ObjOpKbdLeds` with handle 0 as the eve-only global LED control). Pipe handles remain two full-duplex endpoints over the existing queues (`end0` reads q0/writes q1; `end1` reads q1/writes q0). Keyboard Handles share the existing exclusive PS/2 claim and expose raw scan-code bytes; UART Handles use discovery-local numeric ids, mode rights and the structured `Uartcfg` request, reject console/special ports, and preserve `#t` console ownership. Legacy `#|`, `#b` and `#t` Devs remain registered; this is a native user ABI alongside, not a VFS or generic Handle framework.
- Phase-3 retirement (2026-11-05, no new syscall): the 16 numbers 71-85 and 100 are RESERVED. **Phase 4** adds the five former `NET6*` numbers 116-120, so the table is **135 named / 66 active / 69 reserved**, and `OBJ*` 136-140 remain active. `ObjOpPipeInherit=22` is a typed `OBJINVOKE` on a Pipe Handle (`arg0` = legacy bits 1/2, subset-only, one-shot, consumed by the next `RFPROC`; OSPAWN keeps its explicit list). `ObjOpUartCount=23` is the handleless (handle 0) UART discovery call returning `uartnports()`. `ObjOpKbdLeds=19` keeps per-Handle `Vcontrol` and adds handle 0 as the eve-only global LED control without a scan-code claim. `syspipe.c`, `syskbd.c` and `sysuart.c` keep only RFORK/exec/exit and OSPAWN/bootstrap hooks; `vobjpipepair`/`vobjpipeio`/`vobjpipeclose`, `vobjuartopen`/`vobjuartio`/`vobjuartclose` and `vobjkbdread`/`vobjkbdclose` are removed because the generic OBJ paths cover them. MicroPython's historical names are userland adapters over OBJ* and make no retired call.
- N3 adds pathless value operations `RTCGET/RTCSET` (86–87) and `ENVGET/ENVSET/ENVDELETE/ENVLIST` (88–91). `rtcapi.h` defines `Rtcdate` as six `int` fields (year 1970..2069, month 1..12, day 1..31 validated for month/leap year, hour 0..23, minute/second 0..59); `rtcnativeget` reads the existing CMOS/BCD clock under `nvrtlock` and requires stable reads, while `rtcnativeset` validates first and requires `iseve()` (the old `#r/rtc` write authority). The CMOS year is a two-digit field with this repo's 1970–2069 window; raw CMOS/NVRAM remains unavailable. `envapi.h`/`sysn3.c` operate only on the caller's current `up->egrp`: binary-safe get/set/delete and a bounded snapshot list. Values are copied to kernel/user buffers; names are limited to 128 bytes, values to 512 KiB and the environment to 1 MiB. `ENVGET` uses `buf == nil` as a size query and returns `Eshort` when the caller buffer is too small; `ENVLIST` sets `need` (sum of records) and uses records `[4-byte big-endian name length][4-byte big-endian value length][name][value]`. No process handle, `confegrp`/`#ec` path, `devno('e')` gate or raw `Evalue*` crosses the ABI; `RFENVG` copy and `RFCENVG` share remain intact. `rtcdevtab` and `envdevtab` remain registered; 11 Devs are unchanged.
- N13 originally added process-local native UDP6 endpoint calls `NET6ADDR/OPEN/RECVFROM/SENDTO/CLOSE` (116–120). **Phase 4 (2026-11-05) retires that family:** the five numbers stay allocated forever with `nil` dispatch entries, `sysreserved=1`, no `sys*` handler symbol and a raw call returning `Ebadarg`. All UDP6 use is now the five-call OBJ ABI: `OBJOPEN(ObjOpenUdp6)` binds one `VkindUdp6` `Vhand`, `OBJINVOKE(ObjOpSendTo=14)` and `ObjOpRecvFrom=15` transfer one complete 0–1232-byte datagram with peer address/port metadata and the existing millisecond timeout (`0xffffffff` infinite), and `OBJCLOSE` releases the Handle. `OBJINVOKE(ObjOpNet6Addr=24)` is the authorized address lookup: handle 0, `in==NULL`, `outlen==16`, and the same `obootstrap`/`net6permit` gate as opening an endpoint; it reserves no port. The kernel checks each argument's user-memory direction/range and each Handle's operation right. Native UDP6 is default-denied outside the trusted boot process; OSPAWN delegates it only by explicit option. Exec/exit revoke the permit. See [N13-INTEGRATION](N13-INTEGRATION.md) for lifecycle, proof and limits.

- T7 (2026-11-06) restores seven caller-local operations to their direct
  historical numbers: `BRK_` 24, `SLEEP` 17, `NOTIFY` 28, `NOTED` 29,
  `RFORK` 19, `OTERMINATE` 93 and `EXITS` 8.  The former `ObjOpHeapBreak=26`,
  `ObjOpSleep=28`, `ObjOpNotifyRegister=30`, `ObjOpNoted=31`,
  `ObjOpRfork=32`, `ObjOpOterminate=34` and `ObjOpExit=36` variants are
  retired; a raw `OBJINVOKE` with them returns an operation-not-supported
  error.  `kernrfork`, `kernoterminate`, `notecont`, `setnotify`,
  `kernelsleep`, `ibrk(BSEG)` and `pexit` remain the shared cores; the T5b
  note ABI/noteret path is unchanged.  `ProcessSelf` remains for
  `ObjOpTimeNow=27` and `ObjOpWait=35`, and `OSPAWN(92)/_WAIT(36)/AWAIT(47)`
  remain reserved with the typed `ObjOpOspawn=33`/`ObjOpWait=35` operations.
  The current table is **135 named / 62 active / 73 reserved** (the phase-3/4
  counts above are historical).  See
  [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md).

- V1 (this change) adds volatile-object and ObjectFIFO syscalls 121-131: `VOBJCREATE/READ/INFO/CLOSE`, `VOBJFIFOCREATE/SUB/SEND/RECV/STAT`, `VOBJGRANT`, `VOBJREVOKE`. `runtime/abi/vobj.h` defines fixed-width identities, info/stat layouts and the rights bits `Vread=1`, `Vsend=2`, `Vrecv=4`, `Vmeta=8`, `Vclose=16`, `Vsubscribe=32`, `Vcontrol=64`, `Vdelegate=128`. Creation is RAM-only; persistent objects remain the existing GE/O provider. Handles are process-local, generation-tagged references (32 per process) and are dropped on exec/exit; OSPAWN does not inherit them. The kernel accepts at most 4096 live objects, 32 MiB of volatile payload, 256 FIFOs, 64 subscriptions per FIFO and 65536 queued messages globally. These are bounded implementation limits, not an ABI promise. There is no raw OID lookup and no same-identity volatile-to-GE promotion; reading a GE/O object's bytes and publishing them is an explicit clone. See [VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md).

- V2 (continuation) keeps 121-131 and adds `VOBJIO=132`, `VOBJLIST=133`, `VOBJOPEN=134`, `VOBJACCEPT=135`. Rights gain `Vwrite=1<<8`, `Vaccept=1<<9`, `Vconnect=1<<10`. `VOBJOPEN` dispatches `VopenTcpListen`, `VopenTcpConnect` and `VopenGe`; `VOBJIO` dispatches explicit read/write to bytes, TCP streams or GE content; `VOBJACCEPT` creates a connection object. TCP listeners and connections are `VkindTcp`; GE capabilities are `VkindGe` with the persistent SpaceID/OID and a private capability copy of the O Space handle. `OSPAWN` flags bit 2 appends `struct Oinheritv`, an explicit list of up to eight parent Handles and optional attenuated rights; the child gets new process-local Handles and uses `VOBJLIST` to discover them. No implicit PID/handle-number inheritance exists. `TCP6LISTEN/CONNECT/ACCEPT/READ/WRITE/CLOSE` (108-113) are now **RESERVED**: their `systab` entries are `nil`, `sysreserved[]` marks them, and a raw call returns `Ebadarg`; the shared `vtcp_*` provider stays internal. The `VOBJ*` 121-135 numbers are likewise reserved and the `sysvobj*` wrappers are removed. UDP6 (`NET6*`, 116-120) is **RESERVED as of phase 4** and is reached only through the object provider below; the five numbers gain no new syscall. See [VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md) sections 3.3, 7 and 8.1.

  The historical eight-slot `NET6` table is no longer separate. `kernel/core/sysnet6.c` exposes `VkindUdp6` over `nativeudp6.c`, installs the endpoint in the same process `Vhtab` used by all objects, and uses `Vread`/`Vwrite` for receive/send. `OBJOPEN(ObjOpenUdp6)` and `OBJINVOKE(ObjOpNet6Addr=24)` require the boot/permit gate; the endpoint Handle is an explicit capability: rfork/exec do not inherit it, and only OSPAWN `Vdelegate` delegation can pass it to a child, with an equal or attenuated rights subset. Each receive queue remains bounded to 16 × 1400 bytes; the per-process UDP6 endpoint limit remains eight. The one configured native IPv6 interface is not reconfigured by this ABI.

- V3 (universal five-operation ABI, 2026-11-04) adds `OBJCREATE=136`, `OBJOPEN=137`, `OBJCLOSE=138`, `OBJBIND=139`, `OBJINVOKE=140`. `runtime/abi/obj.h` defines the versioned `ObjCall` request/response, `ObjId`, the providers `ObjOpenTcpListen/Connect/Ge/Handle/Binding/Space/GeSpace`, the kinds `ObjKindBytes/Fifo/Ge`, and the operations `Info/Read/Write/Stat/Send/Recv/Subscribe/Accept/Grant/Revoke/List/CreatePersist/Commit`.
  - `OBJCREATE(space, flags, bytes, len, out)` creates an independent byte or FIFO object and returns a Handle. `ObjKindBytes/ObjKindFifo` are volatility-only and never create or clone a GE identity. `ObjKindGe` is explicit persistence: `space` must be an authorized `VkindSpace` Handle; the call uses the existing O Space transaction to allocate, publish the initial bytes and commit, then returns a GE object Handle. For a FIFO, `len` is the requested capacity and `bytes` must be null.
  - `OBJOPEN(provider, req, reqlen, rights, out)` resolves an existing authorized selection: a TCP listener/connection, a UDP6 endpoint bind, a GE capability, another process-local Handle with an equal or attenuated rights subset, a named binding, an authorized O Space by bounded name (`ObjOpenSpace`), or an existing persistent OID through such a Space Handle (`ObjOpenGeSpace`). It never grants rights from knowing an OID and does not walk paths.
  - `OBJCLOSE(handle)` releases this process's reference; it is not an object-destruction guarantee.
  - `OBJBIND(handle, role, rolelen, rights, mode)` installs a bounded ASCII role in the caller's pgrp/OContext namespace (`ObjBindCopy/Exclusive/Share/Remove`). It changes visibility only, never storage identity, FIFO subscription or packet routing. OBJBIND and the native O view share **one** context entry: a GE-backed binding stores both the attenuated `Ohand` and the `Vhand` at one linearization point. COPY/SHARE replace an existing role, EXCLUSIVE rejects it, and REMOVE clears the entry; there is no separate mirror table or rollback path. NS1-NS4 copy/share/revocation therefore observe the same entry.
  - `OBJINVOKE(handle, call, calllen)` is one validated request router. It checks ABI version, operation, direction, length and address, derives authority only from the Handle and its rights (the caller's `rights` field must be zero), and then calls the existing bytes/FIFO/TCP/UDP6/GE provider. On a Space Handle, `CreatePersist` allocates a committed persistent object and returns its OID; `Commit` commits the Handle's pending GE writes and optionally returns the version. On a UDP6 Handle, `SendTo` and `RecvFrom` exchange exactly one complete datagram per call, with the 0–1232-byte payload bound, sender address/port metadata and the millisecond timeout unchanged. Unknown operations fail with `obj: operation not supported`; a bad version fails with `obj: unsupported ABI version`.
  - READ/WRITE and PUBLISH/SUBSCRIBE/RECEIVE/ACCEPT are **userland adapters**, not additional syscalls. `runtime/libc/port/oobject.c` provides `obj_create`, `obj_open`, `obj_close`, `obj_bind`, `obj_invoke`, `obj_read`, `obj_write`, `obj_receive`, `obj_publish`, `obj_subscribe`, `obj_accept`, `obj_grant`, `obj_revoke`, `obj_list`, `obj_listen`, `obj_connect`, `obj_udp6_addr`, `obj_udp6_open`, `obj_udp6_sendto` and `obj_udp6_recvfrom`. A received ObjectFIFO message is one authorized Handle plus an optional envelope; a stream read returns a byte count.
  - The `VOBJ*` 121-135 and `TCP6*` 108-113 families are **retired**: the numbers stay reserved with `nil` dispatch entries and no handler symbols. MicroPython keeps its historical Python method names as userland adapters over OBJ* only; the shared `vobj*`/`vtcp_*`/`vudp6_*` provider core remains internal. UDP6 116-120 is **RESERVED (phase 4)** with `nil` dispatch and no handler symbols; all UDP6 use is `OBJOPEN(ObjOpenUdp6)`, `OBJINVOKE(ObjOpNet6Addr=24/SendTo/RecvFrom)` and `OBJCLOSE`. No legacy number is removed or reused. Exact QEMU evidence is in [OBJECT-ABI-MIGRATION](OBJECT-ABI-MIGRATION.md) and [STATUS](STATUS.md).
  - Capability-fix candidate (2026-11-05, no new syscall): the table is **135 named / 87 active / 48 reserved**, and `OBJ*` 136-140 remain active. Derived GE/O `Ohand` capabilities pin an origin and serialize on its gate; rights subsets are enforced at open, copy and bind; revocation checks the origin lineage. OSPAWN/OPEN reject subscription Handles and OSPAWN requires `Vdelegate`. ObjectFIFO send stores the sender's rights as an upper bound and receive intersects it with the subscription. `CreatePersist` reserves its Handle before the durable allocation and returns only the OID. See [STATUS](STATUS.md) for the executed 4-vCPU QEMU evidence.
  - UDP6 provider candidate (2026-11-05, no new syscall): `ObjOpenUdp6=8` binds an endpoint and returns a `VkindUdp6` `Vhand` whose default rights are `Vread|Vwrite|Vmeta|Vdelegate` (subsets allowed; `Vread` is receive and `Vwrite` is send). `ObjOpSendTo=14` consumes `ObjUdp6SendReq` + payload and `ObjOpRecvFrom=15` fills `ObjUdp6RecvHdr` + payload; `arg0` is the timeout (`0xffffffff` = infinite). `ObjOpNet6Addr=24` is the authorized link-local lookup (handle 0, `outlen=16`) and reserves no port. All of these enforce the existing 0–1232-byte datagram bound and one complete datagram per call. A request for a bit outside the provider default is `Eperm`; open and address lookup require the boot/permit gate, while existing Handles remain explicit capabilities that no rfork/exec path inherits. `NET6*` 116-120 are RESERVED as of phase 4; no number was created or reused.
  - USB service candidate (2026-11-05, no new syscall): `ObjOpenUsb=12` opens a process-local `VkindUsb` `Vhand` whose default rights are `Vmeta|Vdelegate`; a caller may request a subset and any bit outside the available ceiling is `Eperm`. The explicit `Vcreate` bit is available only to the trusted `eve`/bootstrap user; `rights==0` stays metadata-only even for that user, so public metadata open, a guessed Handle or an attenuated copy never opens an endpoint. `Vmeta` is the listing authority. `ObjOpUsbList=20` snapshots the existing `usbcore_list()` endpoint table into `ObjUsbEntry` records (byte-compatible with `Usbentry` from `include/usbctl.h`) and copies at most `OBJ_USB_LISTMAX=256` records per call. `out==NULL` or `outlen==0` is the legacy `USBLIST` total-count query; otherwise `outlen` must be a multiple of the record size, a capacity above 256 records is accepted and clamped exactly like the `obj_usb_list()` adapter, and the result is the copied record count clamped to the available total. `ObjOpUsbEpOpen=21` opens one endpoint (`devno`, `epno`, DATA or CTL, and OREAD/OWRITE/ORDWR) through a service Handle carrying `Vcreate`. The request carries no caller rights mask; the new `VkindUsbEp` Handle's rights are derived from kind and mode (`Vmeta|Vdelegate` plus the mode's `Vread`/`Vwrite`; CTL adds `Vcontrol`). DATA uses the existing `usbcore_dataopen()` path, CTL only references the endpoint, and `OBJCLOSE`/exec/exit drops the endpoint exactly once. Data I/O, `USBIOCTL` and control transfers remain **not implemented**. Legacy `USBOPEN/USBLIST/USBEPOPEN/USBEPNAME/USBREAD/USBWRITE/USBIOCTL/USBCLOSE` 63-70 keep their numbers and the separate 16-slot `Usbhtab` for endpoint handles; there is no second object Handle table. Process exit releases `VkindUsb` and `VkindUsbEp` Handles through `vobjprocexit`.
- E4 has three reviewed native `readnum` reads in total: `native_rtc_read(...)` dispatches `Qrtc`, while `native_cons_read(selector, ...)` dispatches `Qpid` and `Qppid`. It is a **kernel-internal transitional dispatcher**, **not** a proposed universal driver or userspace ABI. Its numeric Qid-derived selectors are not persistent identifiers.
- E5 adds kernel-only service operations in `port/cons.c` (`consputstr`, `kmesgreadat`/`kmesgwrite`, `kprintacquire`/`kprintread`/`kprintrelease`, `consinit`), `port/procobs.c` (`procpid`, `procppid`, `procusername`, `proccputimes`) and `port/sysinfo.c` (`sysnameget`/`sysnameset`, `hostownerget`, `hostdomainget`, `configget`, `osversionget`, `sysstatget`/`sysstatreset`, `timecontrolallowed`, `nulldiscard`, `zerofill`). They take no `Chan*`, `Qid`, `Dirtab` or path. Existing `todget`/`todset`/`todsetfreq`, `randominit`/`randomread`, `panic`, `rdb`, `userwrite`, `hostownerwrite` and `hostdomainwrite` are reused directly. No userspace ABI, syscall number or generic handle was added by E5.
- No generic rights-bearing `Handle`/`Capability` ABI for display and keyboard is implemented.

### 4.4 Security and identity separation

```text
Kernel pointer:  VGAscr*, Queue*, Proc*, Ohand*   (trusted kernel memory)
Handle:          process-visible index/token     (lifetime-limited; lookup needed)
Capability:      validated right(s)              (authorization, not merely a number)
OID:             persistent Space-scoped identity (does not grant device access)
Legacy fd:       retired FD slot; no Fgrp/Chan table   (not the new Handle ABI)
```

For a new public operation, do **not** infer rights from an accessible integer, a matching OID, an open legacy file, or an existing kernel pointer. E4's extracted `consread_qppid_read` relies on kernel-trusted callers and `up != nil`; the legacy `consdir` 0444 mode is not enforcement for arbitrary direct callers.

## 5. Real device boundaries: what migrates, what does not

### 5.1 `#c` is a legacy service aggregator, not a keyboard/display driver

`kernel/core/devcons.c` currently registers `consdevtab` (`Dev`), implements `consattach`/`conswalk`/`consstat`/`consopen`/`consread`/`conswrite` and multiplexes unrelated responsibilities:

| Legacy `#c` path/branch | Native conceptual owner | Important preservation |
| --- | --- | --- |
| console text, `Qcons` | `Console` service | log ring (`kmesg`), routing to `screenputs`/UART, panic/print behavior, queue/locking |
| `Qpid`, `Qppid` | process service (`Proc`) | identity of current process, process-context requirement, formatting only in legacy wrapper |
| `Qtime`, `Qbintime` | time service | clock semantics, privilege for writes |
| `Qrandom` | random service | entropy/initialization and read semantics |
| `Qkmesg`, `Qkprint` | log service | one-reader/queue ownership, close/hangup semantics |
| `Qreboot` | system-control service | authorization and safe reboot behavior |
| `Qsysname`, `Qhostowner` etc. | system configuration | policy, lifetime, read/write validation |

**No single future `consdevtab` replacement should own all these services.** Separate by responsibility, not by a directory tree or artificial Python class.

Current E2/E4 shared helpers `consqpidread` and `consread_qppid_read` are a successful **transitional** extraction. A natural final process C API would produce typed process data rather than 12-byte formatted `readnum` records; its exact signature remains to be specified and checked. Retain compatibility wrappers until userspace migrations are proven.

### 5.2 Keyboard `#b` is separate from `#c`

`kernel/amd64/devkbd.c` currently exposes `Qscancode` and `Qleds` via `kbddevtab`:
- `i8042intr` reads the controller and calls `qproduce(kbd.q, &c, 1)`. `Queue` is the internal scancode buffer.
- `kbdpoll` can poll when the buffer is empty; `kbdscancoderead` performs `qread(kbd.q, ...)`, and `kbdscancodebread` performs `qbread(kbd.q, ...)`. These are *raw-scancode byte/Block transfers*, not normalized `KeyEvent`s.
- `kbdwrite` controls LEDs; `kbdsetleds` is controller-side behavior.
- `kbdopen` enforces `iseve()` and exclusive acquisition through `kbd.ref` for scancode reading; `kbdclose` releases it. These rules cannot disappear merely because `Chan` does.

The first kernel-internal extraction now provides `kbdinit()`, `kbdshutdown()`,
`kbdscancodeacquire()`, `kbdscancoderelease()`,
`kbdscancoderead(void*, long)`, `kbdscancodebread(long)` and `kbdsetleds(int)`
in `kernel/amd64/devkbd.c`. The `#b` adapter delegates its scancode
byte/block ownership/read path and LED write to those same operations.
`kbdinit()` is called directly by `pc64/main.c` after `chandevreset()` and is
idempotent; the legacy `kbdreset` callback delegates to it. `kbdshutdown()`
is called directly at teardown; it is idempotent. The reader claims the existing exclusive `kbd.ref`; reads
retain the existing Queue, PS/2 IRQ/polling behavior, and raw scancode bytes.
Kernel callers are trusted and must acquire before reading and release once;
legacy callers still pass the `iseve()` check. These functions are kernel C operations, not a
userspace syscall or ABI, and they do not produce normalized `KeyEvent`s.

The controller, Queue and `Chan` adapter still share one source file, and the
legacy `#b` entry remains registered in all current pc64 profiles. A later
change may separate source ownership or remove that adapter only after its
users and boot/input behavior have an independently tested replacement. If a
`KeyEvent` layer is added, document raw-to-normalized conversion and support
for other keyboard protocols separately. Never rename raw scancode bytes to
`KeyEvent`.

### 5.3 Screen and display are not `#c`

- `kernel/amd64/screen.c` uses `VGAscr`, `Memimage`, framebuffer mappings and `Rectangle`; `bootscreeninit` initializes bootloader/firmware framebuffer state, while `flushmemscreen` flushes a changed area.
- `kernel/core/draw/devdraw.c` retains the legacy `#i` draw client/namespace path and substantial image/client lifetime logic.
- `kernel/amd64/devvga.c` is legacy VGA control (`#v`).
- `devcons.c` may route text to `screenputs`, or serial `uartputs`; neither creates a native screen ABI.
- `kernel/drivers/uart/devuart.c` exposes the UART driver and existing `PhysUart` callbacks.

Start with a concrete **screen presentation operation** that invokes established screen code, with explicit pixel-format, bounds, ownership, memory visibility and flush conditions. Do **not** mistake a proof that `flushmemscreen` works for a removal of `devdraw`, an implemented generic graphics ABI, or a keyboard driver conversion.

### 5.4 `Pipe`/`Stream`/`Queue` migration

In `kernel/core/pipe/devpipe.c`, `Pipe` has two queue pointers (`q[2]`) and reference accounting (`qref[2]`); reads from one queue and writes into the peer's queue. Closing one end can hang up the peer. Any Chan-free pipe must preserve **both directions, ownership, peer hangup, buffering, backpressure, read/write blocking, and error delivery**.

This is a stronger test of lifecycle semantics than the three scalar E4 reads; it cannot be certified by copying an underlying `qread` call alone. Do not expose raw `Queue*` as a userspace "stream handle".

## 6. Naming rules for new code and generated code

The **semantic glossary IDs T01–T28** are the source of truth. Follow these rules even when porting legacy functionality:

1. **Function name = domain + action**, where it clarifies the operation. Prefer already-existing precise C names (`flushmemscreen`, `qread`) for existing behavior; do not mechanically rename them for consistency.
2. **A type name must say what its value represents.** `Queue*` is a buffering object. `Proc*` is process state. `OID` is durable identity. `Handle` is an unresolved process-local reference. None may stand in for the others.
3. **Do not append `stream`, `queue`, `pipe` or `event` to an API unless its contract meets T01–T08**. In particular do not name a method `read_event` when it still returns raw PS/2 scancode bytes.
4. **Existing kernel symbols are evidence, not universal prescriptions.** Historical names such as `qopen` and `pipeattach` may remain during migration. `qopen` allocates a queue; `pipeattach` creates a legacy channel. They do not establish an API-wide `open` operation.
5. **Never use `channel` as a generic euphemism.** `Chan` is a precise legacy structure; an endpoint, queue, byte stream and IPC connection each have their own term.
6. **No generic `Dev` replacement record/vtable without proof.** E.g. don't introduce `NativeDev.read/write/open/close` just to preserve the old callback slots.
7. **No fake OID mapping.** Do not make device IDs, syscall selector integers, queue pointers or kernel handles into stored olio OIDs without an explicit independently reviewed persistence design.
8. **No new public API before a normative ABI specification.** Function names for experimental internal calls can be selected locally; never assume exported ABI compatibility from naming similarity alone.
9. **Case is meaningful in C.** Canonical glossary names use capitalized concept words (`Queue`, `Pipe`, `Handle`); their existing symbols and proposed identifiers use the correct actual C casing (`Queue*`, `qread`, `Ohand`). Do not conflate a glossary term with a declaration.
10. **No speculative synonym tables.** Historical aliases are recorded for migration but are not permitted alternatives in new definitions. If source code says `fd`, call it a legacy file descriptor; do not relabel it `Handle` in technical claims.

### Acceptance examples

| Statement | Verdict | Why |
| --- | --- | --- |
| "`kbd.q` is a `Queue` of raw `Scancode` bytes." | ACCEPT | Actual kernel representation and payload semantics |
| "Expose `Queue*` to userspace as a `Stream` pointer." | REJECT | Implementation pointer crosses ABI; wrong abstraction |
| "`Pipe` uses two internal `Queue` buffers." | ACCEPT | Verified `devpipe.c` |
| "`FIFO` is the new generic keyboard/pipe type." | REJECT | Ordering property is not a resource kind |
| "The `OSPACEOPEN` integer is an O-Space-specific handle." | ACCEPT | Narrow existing ABI, explicit scope |
| "An OID is a capability or device handle." | REJECT | Persistent identity is not authorization or live resource |
| "`Qscancode` already emits portable `KeyEvent` objects." | REJECT | Current implementation reads raw bytes |
| "`Qppid` is now a Chan-free internal operation." | ACCEPT with scope | E4 internal path only; rest of `#c` remains legacy |
| "Replacing `Dev` with a generic object implementing file methods removes file semantics." | REJECT | Changes names, not semantics |
| "A normal device byte transfer can be named read/write." | ACCEPT | Actual byte transfers are permitted without file metadata |

## 7. Migration procedure for coding agents

For **each** selected operation, produce a minimal audit and prove each gate. An agent MUST NOT generalize from scalar E4 reads to an entire device or unrelated operations.

**M0 — Classify.** State device/service owner, current paths, all involved `Dev` branches, internal C functions, concrete structs, `Queue`/`Block`/`Pipe`/`Stream`/`Event` semantics, current callers and user-visible behavior. Use source inspection and any historical generated inventory as evidence, not semantic proof.

**M1 — Name and contract.** Reuse section 2. Specify data representation, kernel C signature, initialization, resource ownership, permissions, blocking/IRQ constraints, lifetime and failures. Clearly label proposals. Stop if one source identifier ambiguously denotes two glossary concepts.

**M2 — Split mechanically, fail closed.** Extract/reuse the concrete operation while leaving a narrow legacy `Dev` wrapper. Never discard a pre-dispatch side effect, guard, shared local or lifetime action. For generator-produced C, review exact source span, SHA, resulting patch and dependent call chain. `DIRECT-CANDIDATE` is **not** approval.

**M3 — Review access and ownership.** Legacy file permission checks do not transfer magically to a direct C call or new ABI. Preserve exclusive scancode ownership, pipe endpoint close rules, console locks/queues, and any process-specific authorization. No kernel pointer may be passed to userspace.

**M4 — Test the smallest meaningful slice.** Compile the actual sources, link where appropriate, test the native path with one bounded real QEMU probe and preserve one pertinent legacy behavior test. Include one negative for denied access or invalid resource and one for end-of-stream/hangup if relevant. No massive unrelated battery without cause.

**M5 — Document the result.** Record commit, exact source/helper, glossary IDs, ABI status, changed files, tests actually executed (PASS/FAIL/NOT RUN), evidence/log location, and remaining `Chan` edges. Update this document if a new approved semantic category truly arose, not to retroactively rationalize an implementation.

**M6 — Remove old code only with proof.** Account for all former `attach/walk/stat/open/read/write/close` users, init/IRQ/shutdown, mixed-mode and recovery paths. No source deletion merely because a new API appears to work.

### Small required change report (machine-readable prose keys)

```text
OPERATION:
SOURCE_REV:
SOURCE_C_PATHS:
CANONICAL_TERMS: Txx, Tyy, ...
CURRENT_LEGACY_ENTRY:
NATIVE_KERNEL_CALL:
USER_ABI: EXISTING / PROPOSED / NONE
BUFFERING_AND_TRANSFER: Queue / Stream / Message / Event / Pipe / N/A
OWNERSHIP_AND_LIFETIME:
AUTHORIZATION:
IRQ_BLOCKING_AND_LOCKS:
PERSISTENT_IDENTITY: OID / NONE
NATIVE_PATH_CHAN_DEPENDENCIES:
TESTS_ACTUALLY_RUN:
UNDECIDED_CONTRACTS:
VERDICT: APPROVE / HOLD / REJECT
```

A report with `NONE` or `N/A` is acceptable when true. Invented universality is not.

## 8. Unresolved decisions — explicit design boundaries

These are **not** left to individual coding agents to decide implicitly:

- Generic `Handle` table format, generation numbers, types, sharing/duplication and stale-reference guarantees.
- Whether authorization should be represented by an explicit `Capability` type, rights on a handle, process-local grants, or a combination; transfer/revocation and exec inheritance.
- Public keyboard ABI: raw scancodes versus normalized `KeyEvent`s; layout, versioning, exclusivity/fanout, USB/HID translation, wait and cancellation.
- Public screen ABI: pixel buffers or higher-level commands, ownership of framebuffer mappings, flush timing, format/mode changes, multiple clients and security.
- General `Stream` and `Pipe` ABI, byte vs message modes, half-close/hangup, endpoint ownership, partial I/O and error model.
- Public syscall numbering, stable data type widths, return/error encoding, ABI versioning and language bindings.
- Exactly which `#c` services merit userspace exposure versus only trusted kernel C functions.
- How (if ever) a live service/resource is named in the future GE/O graph; no mandatory OID for a device or process is implied.

Until separately approved, preserve the existing narrow interfaces rather than exposing an invented public ABI.

## 9. Source and evidence index

Use these as entry points. **Inspect the specific current version before modifying it**; the source is the authority on IMPLEMENTED behavior, this document on new terminology.

| Component | Evidence path | What to inspect |
| --- | --- | --- |
| Core types, Queue declarations | `kernel/core/portdat.h` | no `Dev`/`Chan`/`Walkqid`; `Proc` (`Proc.fdgrp` presence token), incomplete `Queue`, `Qmsg` flags; `Qid` stays in `lib.h` for GEFS formatting |
| Queue implementation and API | `kernel/core/qio.c`, `portfns.h` | `Queue` internals, `qread`, `qwrite`, `qproduce`, `qhangup`, blocking/framing |
| Pipe | `kernel/core/pipe/devpipe.c` | `Pipe.q[2]`, `qref[2]`, data0/data1 paths, open/close/hangup |
| Console | `kernel/core/devcons.c` | `consdevtab`, `consread` and writes, `putstrn0`, E4 Qpid/Qppid |
| PS/2 keyboard | `kernel/amd64/devkbd.c` | `i8042intr`, `kbd.q`, `kbdpoll`, `kbdopen`/exclusive reader, LED writes |
| Screen/FB | `kernel/amd64/screen.c` | `VGAscr`, `Memimage`, `bootscreeninit`, `flushmemscreen` |
| Legacy drawing and VGA | `kernel/core/draw/devdraw.c`, `kernel/amd64/devvga.c` | drawing client lifetime, Dev dispatch, VGA control |
| UART | `kernel/drivers/uart/devuart.c` | queueing, UART callbacks, console routing |
| Legacy fd ABI | `kernel/core/sysfile.c`, `systab.h` | validation/failure-only FD slots; no `Fgrp`/Chan resolution |
| Native O Space handles | `ge/storage/sysgefs.c` | `Ohand`, `Ohtab`, `ohandles`, `OSPACE*`, exec/exit release |
| Native-operation history | `docs/STATUS.md`; archived verification notes | bounded past QEMU claims, not re-run by cleanup gates |
| File, Stream, OID, semantic object | `docs/OBJECT-MODEL.md`, `docs/O-FORMAT.md` | architectural distinction versus implemented O storage |
| Kernel map | historical source-analysis output and build evidence | machine-auditable graph, unresolved edges; not a tracked contract and not a runtime proof |

### Document update rule

This file is the **single canonical glossary for the native kernel API/ABI**. Changes to terminology require (a) evidence of collision or a previously undefined concept, (b) a stable revised glossary entry/ID, (c) a list of affected call sites/docs/tests, (d) explicit status and compatibility consequences, and (e) an approval separate from merely passing a build.

Do not create a competing glossary in `AGENTS.md` or an agent prompt. Agents may link to this document and quote the relevant stable IDs. If existing code contradicts a proposed API, document the difference; **do not claim the implementation already conforms**.

**Acceptance principle:** a reader must be able to determine whether a particular name denotes a data structure, a transferred byte sequence, a connection, an event, an ephemeral process reference, an authorization grant, or a persistent identity — without inspecting what the writer "probably meant."
