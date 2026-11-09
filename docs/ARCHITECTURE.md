# Plan 2001-kernel: architecture

Plan 2001-kernel is an experimental operating-system kernel, not a production
compatibility platform. The architecture is deliberately open to multiple
instruction-set architectures.

## Architectural invariants

- Persistent data is stored in GE/O Spaces, identified by SpaceID and local
  object identifier (OID). A persistent occurrence is not a file path.
- A runtime object, a process-local Handle/capability, a persistent OID, and a
  physical device identity are distinct concepts.
- Process namespaces are volatile, reference-counted `Pgrp`/`OContext` views,
  not persistent GE/O objects. Stored namespace definitions and object sets
  are data, distinct from the live process namespace.
- Hierarchical filesystem paths, Plan 9 Chan/fid/qid, and 9P are **not** the
  native kernel object interface. A hierarchy may be a userspace view, not
  the persistent identity model.
- The system-call boundary is intentionally small and coherent, **not** an
  exercise in reaching a predetermined syscall count. Use object operations
  where identity, capabilities, ownership, or lifetime have object semantics.
  Keep direct scalar calls where an object would only add ceremony.
- ObjectFIFO is the generic device-frame/IPC channel. It preserves message
  boundaries and capability checks; it is not a filesystem pipe.
- The current proof of concept uses MicroPython as its primary userspace
  program. Additional userspace facilities are not prerequisites for basic
  kernel progress.


## Runtime process namespaces (Plan 9 lifecycle model)

A process namespace is an in-memory, reference-counted `Pgrp`/`OContext`
structure of authorized object-role bindings, **not** a persistent GE/O object
and not an OID. Following Plan 9's process-group lifecycle semantics, a child
may share its parent's `Pgrp` by default, get a copy with `RFNAMEG`, or start
with an empty view with `RFCNAMEG`. A newly created `Pgrp` has its own
`OContext`. `SPAWN` creates a fresh group and can populate it from explicitly
selected delegated roles/views. Namespace ownership is not necessarily
one-to-one with processes: shared groups remain alive while referenced, and
the last owning reference releases their runtime view. `RFNOMNT` seals a live
`Pgrp` against further namespace extension; because it is group-scoped, shared
owners observe the seal, while `RFNAMEG` copies it and `RFCNAMEG` preserves it
with an empty view.

A process may alter its namespace during its lifetime through authorized
role binding/removal and `NamespaceView` attachment/detachment. Handles already
opened from a role do not become invalid merely because that role was removed;
revocation is a separate authority operation. Namespace roles bind capabilities,
not paths, persistent membership edges or stored-object names. The existing
`Pgrp`, `OContext`, `OBJBIND` and `NamespaceView` mechanisms implement this
runtime foundation on the full-system AMD64 path. This does not mean that a
persistent-definition loader or the future set/query model already exists.

## Namespace definition objects (target, not yet implemented)

A persistent GE/O object may contain a *definition* used to initialize or
modify a process's volatile namespace. This follows the separation in Plan 9:
a stored namespace description is not the live namespace it constructs.
A userspace interpreter/loader may offer operations analogous to `newns`
(rebuild a view) and `addns` (apply changes), using existing authorized object
operations. The definition's syntax, resolution rules and loading workflow
are not yet specified or implemented; there is no requirement that each
process creation automatically reads such an object.

Loading or possessing a definition, knowing an OID, or naming a device must
not mint authority. Every resulting role must be resolved from an authorized
source capability with explicit rights attenuation. Editing a definition does
not retroactively alter already constructed process views; reapplication must
be explicit unless a later design specifies otherwise. No Plan 9 mount tree,
pathnames, `Chan`/fid/qid, or 9P namespace is reinstated.

## Persistent GE/O object sets (separate data model; target, not yet implemented)

GE/O objects may describe persistent sets and explicit membership relations
between fully scoped object identities, including references to other sets.
This is *stored data*, not a persistent process namespace. The same object
may belong to multiple sets without changing identity or bytes. Its optional
stored name (at most one, non-unique) remains independent of identity, set
membership and runtime role labels. A hierarchical view is optional userspace
presentation, never the native kernel namespace.

Potential set/query semantics include membership testing, union (`A ∪ B`),
intersection (`A ∩ B`), difference (`A \ B`), and dynamic expressions that
reflect subsequent changes in referenced sets. An initial implementation may
reject cycles; an unbounded universal complement is not required. Identity
comparisons must include the volume when crossing volumes. Membership never
grants capabilities and query results must be filtered by the caller's
authorized visibility. The kernel should supply only necessary identity,
storage and authorization mechanisms; richer expressions belong in userspace.

The legacy GE R1 hashtag/tag model is not the target and requires no
migration: experimental volumes may be reamed and objects reimported. No
persistent set-relation engine or automatic namespace-definition loader is
claimed as implemented.

## Console sessions and namespace-bound terminal I/O (design target; not implemented)

### Purpose and separation of concerns

Console and terminal I/O are foundational device services, not a special feature
of a shell, a GE/O filesystem, or the MicroPython runtime. The target is the
useful Plan 9 principle that a process receives *its own authorized view of
services*: a keyboard/terminal service may be bound to roles in one process's
namespace, and a different service may fill the same roles in another. This
does not require Plan 9's pathname, mount, Chan/fid/qid or 9P ABI.

Keep the following identities and lifetimes separate:

- **Physical device:** PS/2 keyboard, UART, display/framebuffer, or a future
  USB keyboard. The driver and its exclusive acquisition remain kernel/device
  concerns; a device selector alone grants no authority.
- **Console service:** a privileged-enough userspace owner of selected device
  Handles. It translates and routes input/output without granting raw device
  control to ordinary applications. Initially it may be a small MicroPython
  service; a new userspace subsystem is not a prerequisite.
- **Console session:** a volatile set of input/output endpoints and optional
  control state, managed by the console service. A session is neither an OID,
  a stored GE/O namespace nor necessarily one process or one `Pgrp`.
- **Process namespace:** the existing `Pgrp/OContext` binding view, which
  names the *authorized session endpoints* a process may use. Sharing/copying
  a `Pgrp` and delegating process-local Handles are distinct operations.
- **MicroPython REPL or other client:** consumes and produces bytes/events
  through the assigned endpoints. It must not need to acquire the physical
  keyboard, know which output device is active, or have an implicit global
  terminal grant.

A namespace is an authority and lookup view, **not** a transport or an
automatic stdin-to-stdout connection. The bound endpoint providers carry
actual data. Opening a role yields a process-local Handle with explicit rights.

### Verified AMD64 implementation baseline and limitations

The current native boot path is *not* the console-service design:

- **Input:** `kernel/amd64/kbdcore.c` buffers raw PS/2 scan codes.
  `OBJOPEN(ObjOpenDevice)` for `ObjDeviceKbd` requires the
  `OBJ_DEVICE_KBD_EXCLUSIVE` claim; `VkindKbd` reads scan-code bytes.
  `micropython/p9console.c` opens this device directly from the `-I`
  REPL, retains a private `kbdh`, decodes scan codes (currently including
  Finnish keyboard mapping), and feeds `mp_hal_stdin_rx_chr()` through
  `micropython/p9hal.c`. Another independent open cannot take the active
  exclusive claim. Inherited access to a Handle, if explicitly authorized,
  must not be confused with a second acquisition or with input arbitration.
- **Output:** `mp_hal_stdout_tx_strn()` writes through
  `p9_console_write()` and the `VkindConsole` object provider. That
  provider ultimately calls `iowritek()` / `putstrn()`, which use
  kernel-console sinks such as the UEFI framebuffer and serial console.
  `ObjOpenConsole` creates a write-only Handle, authorized by the caller's
  `Proc.iomask` or an explicitly configured `stdoutpipe`; the actual
  write also checks that process-local route. This is not a general duplex
  session endpoint. The current console Handle has `Vwrite`, not
  `Vdelegate`, and cannot simply be `OBJBIND`-delegated as-is.
- **Child process:** `SPAWN` creates a fresh `Pgrp`, empty Handle
  table and no ambient output grant; selected roles, Pipe endpoints or other
  delegable Handles can be passed using explicit creation options.
  Its existing `stdoutpipe` route covers selected output, not an
  automatically supplied child stdin, keyboard focus or full terminal.
  `RFORKEX` likewise only transfers explicitly planned Handles.
- **Namespaces:** `OBJBIND` and `OBJOPEN(ObjOpenBinding)` already support
  delegable generic object kinds, not only GE/O objects. However,
  `vobjnamespacecreate()` currently enforces GE/O role validation for
  sealed views and `vobjnamespacevisible()` reports GE/O identities, not a
  generic named inventory of device/console roles. Selected role delegation
  in `SPAWN` is a separate route; do not claim arbitrary console roles
  already work through every NamespaceView operation.
- **Display:** `kernel/core/fbconsole.c` is a kernel-owned text sink,
  not yet a user-delegable terminal display surface. Existing physical
  framebuffer and serial logging must remain available during early boot
  and service failure. Current PS/2 support does not imply a working USB
  HID keyboard-to-console input service.

This target must be treated as **unimplemented** until the full input,
output, delegation and lifecycle chain is verified in the AMD64 guest and,
when applicable, on hardware. It does not assert ARM64 parity.

### Target endpoint contract

Use ordinary capability objects with bounded, direction-specific rights
before inventing a new syscall or a new persistent kind. Proposed namespace
role labels (contract targets, **not currently standard bindings**) are:

| Role | Client operation | Intended rights and owner |
| --- | --- | --- |
| `console_in` | Read terminal text input | Client `Vread`; console service owns the writer. |
| `console_out` | Write terminal output | Client `Vwrite`; console service owns the reader/sink. |
| `console_err` | Write application errors | Client `Vwrite`; may route to the same display but must be separately selectable. |
| `console_ctl` | Session/focus administration | Only an authorized session manager has `Vcontrol`; not inherited by default. |
| `console_events` (optional) | Receive structured key/resize events | Separate bounded event channel with `Vrecv` where needed; not mandatory for the initial REPL. |

The producer/consumer endpoints can initially use the existing blocking
**Pipe** byte streams. ObjectFIFO is appropriate for *framed* key events,
notifications and IPC, where record boundaries and subscriber semantics
matter; do not change terminal text into fake FIFO frames merely to use
ObjectFIFO everywhere. A future explicit `ConsoleSession` runtime object is
justified only if it has actual identity, rights and lifecycle operations
beyond these endpoint capabilities. Its introduction must preserve the
small object ABI instead of inventing a console-specific syscall family.

Terminal input, output and diagnostics are **separate authorities**. A client
can have read-only input, output-only access, both, or neither. `console_ctl`,
raw keyboard and physical-display ownership must never be inferred from
`console_in` or `console_out`. Role names, object numbers, a known PID
or a namespace definition never grant access without the underlying
legitimately delegated capability. Rights may only be attenuated.

All session endpoints belong to a managed session, but session lifetime
does not have to equal one process lifetime. The console service maintains
its own references and closes/revokes them according to its policy. A
process exiting releases its Handles; a `Pgrp` goes away at its final
reference; a GE/O object's persistent identity is independent of both.
Removing an `OContext` role does not implicitly revoke previously opened
Handles. Switching keyboard focus is a routing operation, not an unbind
or a GE/O transaction.

### Intended data paths and focus

```text
Physical PS/2 (future: USB HID)                 UEFI framebuffer / UART
        |                                              ^
        v                                              |
Kernel device capability                        kernel boot/diagnostic path
        |                                              |
        v                                              |
Console service (sole normal keyboard owner) ---------+
        |            |
        |            +-- decode/layout, input policy, output routing
        |
        +-- Session A: in/out/err/ctl endpoints -- Pgrp A -- MicroPython init
        |
        +-- Session B: in/out/err/ctl endpoints -- Pgrp B -- MicroPython child
```

Only the **active** session receives physical keyboard events. The console
service, not competing client readers of the raw PS/2 queue, arbitrates
focus and forwards bytes/events to the chosen input endpoint. Do not share a
single undifferentiated keyboard read queue between REPLs: that would
split keystrokes unpredictably. Output remains independently routable from
each session, with explicit policy for which output is shown, buffered or
suppressed. The first implementation may offer **one display, one active
session**, with no windows, pseudo-terminals or compositor.

Keep raw scan codes, keyboard layout translation and terminal input policy
separate. Move the current `p9console.c` key decoding into a reusable
service/library boundary, including UTF-8 conversion and modifier state.
Specify where Ctrl-C, Ctrl-D, echo, line editing, escape sequences and
raw/cooked modes are handled; do not accidentally deliver a control key twice
or let inactive sessions consume it. Preserve the MicroPython REPL's
interrupt and EOF behavior, including wakeup/cancellation of a blocked read.

Ordinary program output must not be conflated with the global kernel
log. `console_out` and `console_err` are session streams; early-boot,
kernel panic and diagnostics have a protected fallback console/logging route
independent of any userspace session. No unprivileged session should gain
unrestricted framebuffer memory, UART control, raw scan codes or boot
diagnostic authority merely through terminal output. A blocked/failed
session must not indefinitely block kernel logging.

### Bootstrap and process lifecycle (target)

1. **Early kernel boot:** retain the existing framebuffer/serial diagnostic
   sinks before userspace. The current `-I` REPL direct keyboard route may
   remain temporarily as an explicit compatibility/bootstrap fallback;
   it is not the final client ABI.
2. **Console service initialization:** an authorized bootstrap owner opens
   the device catalog and exclusively acquires the physical keyboard.
   It establishes a display output route and a session registry, then
   creates the initial `console_in`, `console_out` and `console_err`
   endpoints. Exactly one device owner is responsible for keyboard input.
3. **REPL attachment:** the initial MicroPython process receives only the
   selected session endpoint capabilities via its `Pgrp/OContext` roles
   and/or an explicit Handle delegation plan. Its HAL resolves those
   endpoints and performs text I/O; it does **not** try a second exclusive
   keyboard open. If endpoints are missing or invalid, failure/fallback is
   explicit, not a silent attachment to global hardware.
4. **Child creation:** `SPAWN` (or explicit `RFORKEX` as appropriate)
   establishes the child's namespace with a defined COPY/EMPTY/SHARE policy,
   delegates exactly the intended session Handles/roles with attenuated
   rights, and starts MicroPython. Namespace sharing is not an implicit
   Handle-table inheritance rule. The parent must not block on child input
   while still consuming that input itself.
5. **Focus transfer:** the session manager selects the child session as
   active. Only then are physical keystrokes routed there. The parent's
   session remains alive but inactive; the parent may wait on its child,
   without competing for the keyboard device. Output destinations are
   independently chosen.
6. **Exit, failure and recovery:** child exit/EOF/crash releases only the
   child's references; input focus returns to the surviving session in a
   defined order. The console service retains the raw keyboard claim when
   merely switching sessions. If the service itself dies, device claims must
   be released and an authorized boot/recovery path must be able to regain
   console access without relying on the failed service or its namespace.
   Ensure the kernel console still emits diagnostics.

The future `import unix` experiment is a **consumer** of this mechanism:
its child MicroPython process may receive a distinct namespace and a
selected console session, then present an optional Unix-like path view over
GE/O object sets. Building that interpreter must not precede the minimal
reliable console/REPL handoff, and it must not reintroduce native pathname
syscalls.

### Implementation order and required checks

Implement in small, independently testable increments; preserve the current
boot-to-REPL path after every step.

1. **Baseline tests and boundaries:** add reproducible tests for PS/2
   exclusive acquire/release, scan-code decoding, `ObjOpenConsole` write
   authorization, `iomask`/`stdoutpipe` behavior, and explicit child
   Handle/role delegation. Record that an ordinary spawned `-c` child
   currently lacks a general interactive stdin. Test missing and revoked
   grants, ownership and resource cleanup.
2. **Endpoint-driven MicroPython HAL:** allow stdin, stdout and stderr to
   resolve independent authorized endpoint Handles/roles. Keep direct
   console fallback limited to bootstrap, with a single clear selection
   rule. Avoid runtime use of hardwired `kbdh`, `outh` or `diagh` when
   a session is present. Do not make ordinary console output depend on
   `Proc.iomask`; the capability endpoint must be sufficient.
3. **Minimal console owner and switch:** create one service owning the
   exclusive PS/2 Handle, keyboard decoding and independent session stream
   routes. Prove one active input session, then switch focus between two
   MicroPython processes without closing/reopening the physical keyboard.
   Preserve flow control and define behavior if an input reader exits while
   blocked, or a session produces output while inactive.
4. **Namespace consistency:** make generic console/device role delegation
   work where intended. Review GE/O-only validation/visibility in
   `NamespaceView` and provide authorized, bounded role introspection
   for non-GE types *if needed*, without leaking inaccessible identities
   or bypassing `Vdelegate`. Do not couple namespace lookup to the
   concrete PS/2 or framebuffer provider.
5. **Recovery and regression:** boot with no session and with a failed
   console service, launch child REPL, switch active session, type UTF-8 and
   control keys, write stdout/stderr independently, exit/crash the child,
   return to the original REPL, and repeat without leaked Holds or blocked
   keyboard claims. Verify privilege attenuation, unauthorized raw device
   access rejection and absence of input theft across sessions. Run the
   AMD64 QEMU integration tests and relevant hardware checks; do not
   claim ARM64 parity from AMD64 results.

No mandatory new syscall, POSIX file descriptors, `/dev/cons` path or
userspace file hierarchy is implied by this plan. Any ABI extension must
be justified by a missing capability/lifecycle operation, rather than by
convenience of a particular shell. Treat `docs/DEVICE-ABI.md` as authoritative
for physical device providers and `docs/KERNEL-INTERFACE.md` for namespace
and process delegation contracts. Implementation starting points:
`micropython/p9console.c`, `micropython/p9hal.c`,
`kernel/amd64/kbdcore.c`, `kernel/core/sysvobj.c`,
`kernel/core/sysfile.c`, `kernel/core/cons.c`,
`kernel/core/fbconsole.c`, `kernel/core/sysproc.c`,
`kernel/core/pgrp.c` and `ge/storage/ocontext.c`.

## Architecture status

**AMD64:** The current full-system implementation includes UEFI boot,
scheduler, virtual memory, GE/O, object/system-call interfaces, device support,
and MicroPython startup. Its existing build and test paths remain applicable.

**ARM64/AArch64:** Active early-boot port. The repository contains an ARM64
ELF kernel, UEFI loader, shared BootInfo v1 validation, exception/EL1 entry,
early mappings, PL011 console, GICv2/GICv3 timer interrupts, and an early
physical-page allocator. The `tools/qemu-arm64-boot.sh` check exercises early
boot with QEMU `virt` and GICv2 or GICv3; it does not establish full-system
capability or physical-hardware compatibility. ARM64 does **not** yet implement
the full AMD64 scheduler, virtual memory, GE/O, syscalls, userspace, or
device/networking stack.

This describes implementation maturity, **not** an AMD64-only policy. Shared
boot changes must consider both architectures; architecture-specific changes
must not invent cross-architecture feature parity.

## Implementation sources

- `kernel/core/`: core kernel and shared early boot.
- `kernel/amd64/`, `kernel/arm64/gcc/`: architecture-specific code.
- `boot/efi/`: UEFI loader sources.
- `ge/storage/`: GE/O storage engine.
- `runtime/abi/sys.h`: declared kernel syscall ABI and status.
- `micropython/`: current userspace interpreter.
- `kernel/drivers/`: device integrations.

**The current source is authoritative** where these notes and actual code
disagree. Record measured behavior separately from desired design.
