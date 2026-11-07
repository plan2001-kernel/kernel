# Plan2001-O roadmap: T8–T16 and revised architecture

**Status: T8 implemented and verified (PASS, 2026-11-06); T9 completed as
the documentation/measurement audit (PASS, 2026-11-06); T10 implemented and
verified as the narrow time/WAIT decision round (PASS, 2026-11-06); T11
validated and verified (PASS, 2026-11-06) with one FIFO timeout sentinel fix;
T12–T16 remain PLANNED — documentation only. Nothing in T12–T16 is implemented
by this document.** T7 is the previous partial result. This roadmap records
architectural decisions and staged acceptance gates; T8 was completed under a
separately assigned task and its observed result is recorded below and in
[SYSCALL-T8-MULTIPROCESS](SYSCALL-T8-MULTIPROCESS.md). This document does not
authorize source, ABI, build, test or hardware changes for T9–T16, and does not
create new syscalls.

- **Source baseline inspected:** published snapshot
  `01a7c7a454b420136779595117dce2cd5170e5a1b689ead3dbb77b98d1bafe17`
  (2026-11-06) on host `kuha`; the T9 ledger measured the later published
  snapshot `2b3e6126f2d195fc3c3d8d89f9410fd228ec87ff0adbce7880d5a955e7d62253`
  (2026-11-06T18:27:56Z).
- **Current observed dispatch table at that snapshot:** **135 named / 62 active /
  73 reserved** (`build/gcc/gen/systab-gcc.h`, generated from
  `runtime/abi/sys.h`). This is a baseline to verify at each later stage, never a
  target and never a quota to preserve.
- **Previous result:** T7 restored seven direct syscalls and is recorded as
  PASS for those paths and PARTIAL overall in
  [STATUS](STATUS.md) and [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md).
  T7 did not implement any T8–T16 stage.
- **T8 result:** the first complete multi-process MicroPython PoC is observed
  PASS on `kuha` (2026-11-06). See [SYSCALL-T8-MULTIPROCESS](SYSCALL-T8-MULTIPROCESS.md)
  and `.json`. T9 was completed separately as the audit below and T10 as the
  narrow cleanup; T11–T16 were not started.
- **Evidence vocabulary:** CURRENT IMPLEMENTED means observed in the inspected
  source/tree. PLANNED means a target accepted for this roadmap but not built.
  AGENT-REPORTED means a result stated in an earlier task report and not
  independently re-run for this document.
- **T10 result:** the direct `_NSEC(53)` libc time path is implemented and the
  `WAIT` object path is retained with a recorded rationale; see
  [SYSCALL-T10-CLEANUP](SYSCALL-T10-CLEANUP.md).
- **T11 result:** the existing universal ObjectFIFO was validated across real
  MicroPython processes and one infinite-timeout sentinel defect was fixed;
  a 2026-11-07 follow-up strengthened four weak assertions (70/70 checks PASS,
  exact error/PID/status and quota-count evidence, mutation-discriminated) and
  marked subscription-close-while-blocked NOT RUN as unreachable, with no
  kernel change; see [SYSCALL-T11-OBJECTFIFO](SYSCALL-T11-OBJECTFIFO.md).
  T12–T16 were not started.

## 1. Why this roadmap exists

Earlier migration rounds used the phrase “the canonical kernel boundary is
exactly five syscalls”. That statement is **superseded for the total syscall
ABI**. The five universal object operations are still the general object
boundary, but they are not the whole kernel interface. T7 already restored
direct calls for simple, process-local operations, and later stages must be free
to do the same where that is genuinely simpler.

The governing goal is a **small, coherent, understandable kernel interface**,
not a numeric syscall count. In particular:

- The universal object operations `OBJCREATE(136)`, `OBJOPEN(137)`,
  `OBJCLOSE(138)`, `OBJBIND(139)` and `OBJINVOKE(140)` remain the general
  boundary for independent objects, resources, devices, resource lifetime,
  identity, capability/authorization, subscriptions, and persistent
  OIDs/Spaces.
- Simple, inherently process-local or self-contained operations may use a
  direct syscall. `BRK_(24)`, `SLEEP(17)`, `NOTIFY(28)`, `NOTED(29)`,
  `RFORK(19)`, `OTERMINATE(93)` and `EXITS(8)` are direct calls again after T7.
- Never force a simple operation through `OBJOPEN`/`OBJINVOKE`/`OBJCLOSE`
  merely to make the syscall count smaller.
- “Five universal OBJ operations” and “total kernel syscalls” are different
  statements. This document keeps them separate.

## 2. Normative architectural decisions

### 2.1 Objects, OIDs, handles, spaces and visibility

Plan2001-O is an experimental amd64 object-oriented operating system without a
mandatory hierarchical filesystem path model. Persistent GE/O objects are
OID-addressed within Spaces. Keep these concepts distinct:

| Concept | Meaning in this architecture |
| --- | --- |
| Persistent OID | An identifier of stored content/identity within a GE/O Space. Not a path, address, process-local index or permission. |
| Volatile object identity | A RAM-only identity such as `(incarnation, space, oid)`; it does not imply GE/O persistence. |
| Process-local handle | A lifetime-limited, process-visible reference resolved by kernel state and carrying explicit rights. Not an OID, capability, pointer or path. |
| Capability | A validated authorization for specified operations on a resource, with an owner, scope and revocation rules. A bare integer is not a capability. |
| Space | A GE/O persistence scope containing OID-addressed contents/versions. Not a process address space, directory tree or device registry. |
| Binding/role | A bounded name in a process namespace for authorized visibility. It is not a path walk, storage identity or global namespace hierarchy. |

Project and namespace visibility is **not** a directory hierarchy. Source-tree
paths in this repository are development/export names, not kernel identity.

### 2.2 One universal ObjectFIFO

There is **one** universal `ObjectFIFO` mechanism for ordered messages carrying
authorized immutable object references plus an optional bounded opaque metadata
envelope. It is independent of payload content, device and protocol.

- Do **not** create `TCP-FIFO`, `UDP-FIFO`, `Ethernet-FIFO`, `NetFrame-FIFO`,
  `USB-FIFO` or any other protocol/device-specific FIFO variant.
- Each `TEE`/fanout subscriber has its own bounded queue, rights, lifecycle,
  overflow behavior and revocation. “Tee” does not imply indiscriminate
  delivery.
- Traffic authorization and filtering are separate from FIFO transport.
- `ObjectFIFO` is not a byte stream, a network socket, a packet queue or a
  device driver. Its core must not inspect or depend on payload format.

CURRENT IMPLEMENTED: the volatile-object/ObjectFIFO core is in
`kernel/core/sysvobj.c`; it supports immutable object-reference messages,
subscriptions, TEE/fanout, bounded queues, rights intersection, overflow
counters and revocation. Its correctness and independence are to be validated
in T11, not assumed from this summary.

### 2.3 One general authorized device model

USB, Ethernet, Wi-Fi and similar devices share **one** general authorized device
model:

- discovery and selection;
- authorized handle/open;
- complete-message receive and send;
- authorized device-specific control;
- close, ownership and lifetime;
- interrupts, DMA and transfer completion;
- error and partial-transfer reporting.

Devices naturally have different frames, payloads and metadata. Reuse the
existing `OBJ` and `ObjectFIFO` mechanisms where semantically correct; a direct
syscall is permissible when it is genuinely simpler. Do **not** introduce fake
paths, a pseudo-filesystem, a `Chan`/`fd` bridge or a file-descriptor
compatibility layer.

### 2.4 Raw network device access; protocols primarily in userspace

Network device access is raw and protocol-independent:

- Ethernet access transfers whole Ethernet frames.
- Wi-Fi access uses the frame/mode exposed by the driver, not an invented
  Ethernet-only contract.

Network protocols live **primarily in userspace**: IPv6, ICMPv6, Neighbor
Discovery, UDP, TCP and eventually IPv4 if desired; routing, IKEv2, ESP and
similar protocol logic also belong there. EtherType/IPv6 Next Header filtering
at the kernel device boundary is classification/access control, not a separate
kernel protocol stack.

CURRENT IMPLEMENTED and TRANSITIONAL: the native kernel IPv6 path,
`ObjOpenTcpListen`, `ObjOpenTcpConnect`, `ObjOpenUdp6` and the current kernel
network devices exist and work in the inspected tree. They are transition
mechanisms, **not** templates for adding more protocol-specific object
providers. Do not treat a provider-per-protocol pattern as the target.

### 2.5 Preserve working network and security behavior during migration

Preserve the existing working drivers, native IPv6 and the existing experimental
IKEv2/ESP code until tested userspace replacements can provide the required
service.

- IKEv2 uses UDP port 500.
- ESP uses IPv6 Next Header 50.
- These are **not** the same identification and must not be conflated.
- Never weaken authentication or replay protection.
- Never permit unauthenticated plaintext fallback when a userspace service
  crashes or restarts.

### 2.6 Supported userspace during the syscall-cleanup PoC

During the syscall-cleanup proof of concept, **MicroPython is the only supported
general userspace application**, plus actual boot dependencies. Do not add
speculative compatibility layers for userspace programs that are not included
or supported. Audit real source, linked binaries and tests before changing an
interface.

## 3. Current baseline (observed, not a target)

At the inspected snapshot:

- The five universal object operations are active: `OBJCREATE(136)`,
  `OBJOPEN(137)`, `OBJCLOSE(138)`, `OBJBIND(139)`, `OBJINVOKE(140)`.
- T7 direct calls are active: `BRK_(24)`, `SLEEP(17)`, `NOTIFY(28)`, `NOTED(29)`,
  `RFORK(19)`, `OTERMINATE(93)`, `EXITS(8)`.
- The T7 retired `ObjOp` alternatives remain allocated with no dispatch:
  `ObjOpHeapBreak=26`, `ObjOpSleep=28`, `ObjOpNotifyRegister=30`,
  `ObjOpNoted=31`, `ObjOpRfork=32`, `ObjOpOterminate=34`, `ObjOpExit=36`.
- Kernel cores remain shared rather than duplicated: `ibrk(BSEG)`,
  `kernelsleep()`, `setnotify()`, `notecont()`, `kernrfork()`,
  `kernoterminate()` and `pexit(status,1)`.
- The T5b amd64 SysV note callback ABI in `RDI/RSI` and the `lentry.S`
  `noteret`/`iretq` return path are preserved. `NOTED` keeps its special
  register-return behavior.
- `OSPAWN(92)`, `_WAIT(36)` and `AWAIT(47)` are reserved; the typed
  `ObjOpOspawn=33` and `ObjOpWait=35` operations are used instead.
- `ProcessSelf` remains for `ObjOpTimeNow=27` and `ObjOpWait=35`.
- The generated table is 135 named / 62 active / 73 reserved.

Current transitional network code is visible in `kernel/core/ethercore.c`
(`ethercore_lookup`, `ethercore_byindex`, `ethercore_attach`, `ethercore_tx`,
`ethercore_rxregister`, `etheriq`), the native IPv6 sources, and the UDP6/TCP6
object providers. The experimental IKEv2/ESP material is described in
[MINIMAL-IKEV2](MINIMAL-IKEV2.md).

Do not silently convert any “current” statement above into a T8–T16 PASS claim.

## 4. Staged roadmap

Each stage is a separate work item with its own acceptance gate. This document
does **not** submit or authorize any implementation task. Do not conflate several
stages into one change, and do not use later-stage work to bypass an earlier
gate.

### T8 — First complete multi-process MicroPython PoC

**Depends on:** current T7 tree; no earlier T8–T16 stage.

**Status: PASS (2026-11-06).** Implemented and verified in
[SYSCALL-T8-MULTIPROCESS](SYSCALL-T8-MULTIPROCESS.md). The only source change
was the one-line console-object byte-accounting fix in
`kernel/core/sysvobj.c`; no syscall, object operation or ABI was added or
changed.

**Scope (implemented):**

1. Boot the normal kernel into the parent MicroPython REPL.
2. Keep GE/O persistence working across the normal boot and REPL path.
3. Let the parent store or reference a real MicroPython ELF as a GE/O OID.
4. Start an independent child MicroPython using the existing
   `space.spawn()`/`OBJINVOKE(ObjOpOspawn)` path.
5. Have the child execute a simple Python program (for example `-c`) and exit.
6. Have the parent `space.wait()` return the correct PID/status.
7. Confirm the parent REPL survives the child lifecycle.
8. Check isolation and failed-start cases.

**Acceptance conditions (verifiable):**

- The normal boot reaches the parent REPL without a test-only boot selector.
- GE/O persistence still works over the established two-boot check.
- The child is an independent MicroPython process, not a Python-level
  coroutine, thread by renaming, or fake in-process call.
- The child runs a simple program and produces the expected observable result.
- PID/status from `space.wait()` is correct, including a non-zero/failed child
  case and a missing/bad executable case.
- The parent REPL remains usable after child exit and after a failed start.
- Single-writer Space constraints are respected. No concurrent writes into the
  parent Space occur without explicit authorization.
- The result distinguishes what was actually executed from untested cases.

**NOT in scope:**

- A second REPL window or terminal multiplexing.
- Networking, IKEv2 userland, ESP VPN, a network-stack port or device migration.
- Unrelated userspace daemons.
- Redesign of process/Space APIs, or implicit handle/PID inheritance.
- Marking T8 PASS if only a single trivial child path was run.

**Hard gate:** T8 is a HARD milestone gate. Complete the first real multi-process
PoC before any further syscall redesign (T9 may document and measure; T10 and
later may not use T8 as an excuse to skip the gate).

### T9 — Static and runtime semantic audit of the active syscall ABI

**Depends on:** T8 gate complete.

**Scope (COMPLETED 2026-11-06, documentation/measurement only):**

For **each currently active syscall**, record:

- semantics and side effects;
- argument layout, widths, direction and validation;
- return and error encoding;
- required permissions/authorization;
- actual users: bootstrap loader, MicroPython, libc, kernel-internal or other;
- tested versus untested behavior;
- disposition: direct process-local; object/Space/identity; resource/device;
  bootstrap dependency; temporary compatibility; truly unused.

The deliverable is **one authoritative, source-derived syscall ledger**. A
proposed path such as `docs/SYSCALL-LEDGER.md` is a documentation target only;
the exact filename may follow repository convention.

**Acceptance conditions (verifiable):**

- Every active syscall at the measured revision appears in one ledger.
- Each entry cites the real source and, where relevant, the linked binary or
  test that uses it.
- The ledger states the active/named/reserved count it measured and how it was
  derived, instead of trusting an older report.
- Absence from one trace is not sufficient to call a syscall unused.
- A hypothetical future application is not sufficient to retain a syscall.
- **No source, build script, test or ABI change occurs in T9.**
- Factual discrepancies in older documents are identified explicitly rather
  than silently corrected through code changes.

**T9 result:** PASS on the measured snapshot; the ledger is
[SYSCALL-LEDGER](SYSCALL-LEDGER.md). It records 135 named / 62 active / 73
reserved, every active entry, the historical count discrepancies and the
remaining evidence gaps. No source, build script, test or ABI changed.

**NOT in scope:** removals, renumbering, new syscalls, implementation changes,
or a PASS claim for any T10–T16 stage.

### T10 — Narrow semantic cleanup using T9 findings

**Depends on:** T9 ledger accepted.

**Status: PASS (2026-11-06).** Implemented and verified in
[SYSCALL-T10-CLEANUP](SYSCALL-T10-CLEANUP.md). libc `nsec()` now calls direct
`_NSEC(53)`; `plan9.time_ns()` uses the same call; the unused libc
`obj_process_self_time()` adapter was removed. `ObjOpTimeNow=27` is retained
for raw `OBJINVOKE`. `WAIT` stays on the typed `ObjOpWait` path because the
reserved historical numbers cannot carry the typed record and no new number
was warranted. All other T9 candidates are retained with recorded reasons.
The dispatch table remains **135 named / 62 active / 73 reserved**.

**Scope (implemented, narrow):**

- Examine `_NSEC(53)`. Current libc `nsec()` opens `ProcessSelf`, invokes
  `ObjOpTimeNow`, then closes it, even though a direct `sys_nsec()` reading
  `todget()` exists. Consider making `nsec()` direct and retiring the redundant
  `ObjOpTimeNow` only after dependency and regression checks.
- Examine `WAIT`. Current `obj_wait()` opens `ProcessSelf`, invokes
  `ObjOpWait`, then closes it. Consider a direct wait using the existing
  `pwait()` if that preserves typed return, error and authorization behavior;
  the decision must be grounded in T9, not in aesthetics.
- Apply the same evidence discipline to other candidates the ledger identifies.

**Acceptance conditions (verifiable):**

- Each change is justified by the T9 ledger and has targeted regression
  evidence.
- `_NSEC` and `WAIT` behavior, including typed results, errors, permissions and
  lifetime, is preserved or deliberately re-specified with evidence.
- No parallel kernel cores are created.
- No syscall is renumbered.
- No broad refactor, new feature or speculative compatibility layer is added.
- If a candidate is not changed, the reason is recorded rather than left
  implicit.
- Do not assume every scalar operation must be direct, and do not assume every
  already-migrated operation must stay on the object path.

**NOT in scope:** renumbering, broad ABI redesign, protocol work, device work,
or implementing later roadmap stages.

### T11 — Validate the existing general ObjectFIFO

**Depends on:** T8 multi-process PoC; T9/T10 may proceed in parallel only where
they do not modify the FIFO ABI.

**Status: PASS (2026-11-06), strengthened follow-up PASS (2026-11-07, no
kernel change).** Validated in
[SYSCALL-T11-OBJECTFIFO](SYSCALL-T11-OBJECTFIFO.md) and `.json`. The original
round executed 59 checks across three boots with real independent MicroPython
processes, including multiprocess TEE with independent bounded queues,
drop-oldest, rights attenuation, revoke/close/exit teardown, GE/O capability
transfer, Handle exhaustion and the global message quota. One kernel defect was
reproduced and fixed: the OBJ FIFO receive path rejected the adapter's negative
infinite timeout instead of blocking. No syscall, operation number or rights
rule was added or changed.

The 2026-11-07 follow-up replaced four weak assertions and reran 70/70 checks
PASS (boot A 15, boot B 35, boot C 20): exact `Ehungup` after a bounded
no-result window; a real full-width non-delegated parent Handle passed as data
with an authorized delegated control and exact `Ebadarg`; exact reaped PID and
`micropython <pid>: terminated` status; and exact `Enomem` with all eight of 64
witness queues unchanged at `1024/1024/0`. A mutation run confirms the new
assertions fail for deliberately wrong conditions. Subscription close while a
receive is already blocked is **NOT RUN**: the receive's Handle reference delays
`vsubshutd`, but the path is unreachable because Handles are process-local and
subscriptions cannot cross OSPAWN/RFORK. No kernel change was made. T12 was not
started.

**Scope (implemented, validation):**

Validate the existing ObjectFIFO across multiple MicroPython processes,
including:

- immutable object-reference transfer;
- subscriptions and independent bounded queues;
- TEE/fanout behavior;
- rights intersection and attenuation;
- revocation and close;
- error, overflow, closed-subscription and producer/consumer teardown cases.

Confirm the FIFO itself has no protocol or device dependency. Filtering and
authorization are distinct from delivery.

**Acceptance conditions (verifiable):**

- Multiple real MicroPython processes exercise the same FIFO mechanism.
- Each subscriber's queue is independently bounded; one slow or dead subscriber
  cannot silently starve or corrupt another.
- Rights cannot be amplified through send, subscription, TEE or delegation.
- Revocation, close and teardown have defined, tested outcomes.
- Overflow and error paths are observed, not merely reasoned about.
- The FIFO core contains no protocol/device-specific branch.
- No panic, dangling reference, use-after-free or unbounded allocation is
  observed in the tested paths.
- Results distinguish executed tests from untested limits.

**NOT in scope:** inventing a second FIFO framework, protocol-specific FIFO
variants, network protocol implementation, or device-driver migration.

### T12 — Specify the common device ABI

**Depends on:** T11 validity evidence for the ObjectFIFO reuse.

**Scope (PLANNED):**

Specify and demonstrate one general authorized device model covering:

- device discovery and selection;
- authorized handle/open;
- bounded complete-message send and receive;
- authorized device-specific control;
- close, ownership and lifetime;
- transfer completion and error reporting.

Reuse `OBJ`/`ObjectFIFO` where semantically correct. Do not require identical
payload shapes across Ethernet, USB and Wi-Fi. Do not add a filesystem, `Chan`
or `fd` layer. Implement the definition plus **one minimal representative device
integration** only, not all device families at once.

**Acceptance conditions (verifiable):**

- The ABI definition states handle identity, rights, authorization, validation,
  ownership/lifetime, completion, errors and teardown.
- One representative device demonstrates discovery, open, complete-message
  send/receive, device-specific control and close.
- The device's payload/metadata differences are explicit rather than hidden
  behind a fake universal packet format.
- The implementation reuses existing Handle/capability/ObjectFIFO machinery or
  documents why a direct call is simpler.
- No pseudo-filesystem, `Chan` bridge, `fd` compatibility layer or separate
  per-device FIFO is introduced.
- Tests are reported honestly, including what was not run.

**NOT in scope:** migrating every device, implementing USB and Wi-Fi together,
changing GE/O format, or implementing userspace protocol stacks.

### T13 — Ethernet raw frame send/receive over the device model and ObjectFIFO

**Depends on:** T12 device ABI and T11 FIFO validation.

**Scope (PLANNED):**

Use the current Ethernet core (`ethercore.c`: `ethercore_lookup`,
`ethercore_byindex`, `ethercore_attach`, `ethercore_tx`,
`ethercore_rxregister`, `etheriq`) to transfer **whole Ethernet frames** through
the device model and the same ObjectFIFO mechanism, with independently
authorized subscribers/TEE and optional filters.

**Critical implementation constraint (observed in source):** `etheriq()` may run
in interrupt context, while the FIFO send path (`vobjfifosend()` in
`kernel/core/sysvobj.c`) uses `QLock` and may allocate. Introduce a **bounded
IRQ-safe handoff into the same ObjectFIFO mechanism** and complete the FIFO work
in a safe context. Do not create another general FIFO framework and do not call
a sleeping allocation or blocking lock from the interrupt path.

**Acceptance conditions (verifiable):**

- Complete raw Ethernet frames are sent and received in both directions.
- Interface selection, rights and authorization are explicit and tested.
- Multiple independently authorized subscribers/TEE receive according to their
  own queues and rights; optional filters are separate from delivery.
- Overflow, close, revocation and teardown have defined, tested outcomes.
- The RX interrupt path performs only the bounded handoff; blocking/sleeping
  work runs in a safe context.
- The existing working kernel network service continues to work, or any
  temporary change is documented as transitional and does not silently weaken
  security.
- No per-protocol/network-frame FIFO variant is added.
- The report distinguishes raw-frame transport from any protocol parsing.

**NOT in scope:** userspace IPv6 implementation, TCP/UDP migration, changing
the GE/O format, or deleting the native kernel network stack prematurely.

### T14 — Minimal userspace IPv6 service (only after T13 passes)

**Depends on:** T13 raw-frame send/receive and device model evidence.

**Scope (PLANNED):**

Implement a minimal userspace IPv6 service over raw Ethernet:

- IPv6, ICMPv6, Neighbor Discovery and IPv6 addressing;
- basic IPv6 exchange;
- add UDP, later TCP;
- start with a minimal MicroPython PoC.

This does not need to be a complete router stack.

**Acceptance conditions (verifiable):**

- A minimal userspace exchanging node performs basic IPv6/ICMPv6/ND traffic
  over the T13 raw-frame path.
- Address configuration and neighbor discovery are observable and tested.
- UDP is added only after the IPv6/ICMPv6/ND exchange works; TCP only after that.
- The old kernel stack and the new userspace stack do not answer the same
  traffic/address simultaneously; the handover or isolation mechanism is
  explicit.
- Failure and malformed-packet behavior is reported.
- The report distinguishes a minimal PoC from a production router or full stack.

**NOT in scope:** complete router functionality, IPv4 unless separately
desired and planned, IKEv2 userland, ESP VPN, or device migration beyond what
T13 supplies.

### T15 — Gradual userspace protocol migration

**Depends on:** T14 minimal userspace IPv6 and T13 device delivery.

**Scope (PLANNED):**

Bring up or port UDP, TCP, VPN IKEv2 and ESP as needed over the generic
device-message delivery mechanism.

- Preserve the existing kernel implementations until feature, security,
  failure, authorization and compatibility tests pass.
- Never add new kernel TCP/UDP/ESP object providers.
- Remove a redundant legacy protocol implementation only after independent
  validation.
- Explicitly prevent unauthenticated plaintext fallback.

**Acceptance conditions (verifiable):**

- Each protocol has its own feature, security, failure and compatibility gate.
- IKEv2 (UDP/500) and ESP (IPv6 Next Header 50) remain distinct and correctly
  identified.
- Authentication and replay protection are not weakened.
- A userspace service crash/restart cannot silently fall back to plaintext or
  an unauthenticated path.
- The kernel implementation is removed only after the replacement is
  independently validated, not merely “present”.
- No new protocol-specific kernel object provider or FIFO is introduced.
- Results record per-protocol pass/partial/not-run status.

**NOT in scope:** a big-bang protocol replacement, removal of the kernel stack
before validation, or weakening cryptographic/security behavior for
convenience.

### T16 — Generalize to USB and Wi-Fi

**Depends on:** the T12 common device contract and the relevant T11
ObjectFIFO/T13 device-integration validation. It does not require completed T15
userland TCP/UDP/VPN migration.

**Scope (PLANNED):**

Demonstrate that the T12 model handles different device payload and metadata
formats while reusing the existing Handle/capability/ObjectFIFO machinery.

- Keep drivers and hardware-specific control in the kernel.
- Do not create separate device-type FIFOs.
- Do not create a hierarchical device filesystem.

**Acceptance conditions (verifiable):**

- At least one USB path and one Wi-Fi path demonstrate the same general device
  ABI with their genuinely different payload/metadata formats.
- Handles, rights, authorization, ownership, completion and teardown use the
  shared machinery; differences are device-specific data, not separate FIFO
  families.
- Hardware-specific control remains in the kernel and is authorized.
- No path hierarchy, fake files or `fd` compatibility layer is introduced.
- Results are honest about which hardware/driver modes were and were not run.

**NOT in scope:** universal driver unification for its own sake, a device
filesystem, or claiming all USB/Wi-Fi hardware is covered.

## 5. Dependency and delivery rules

1. **One stage, one gate.** Do not conflate the whole roadmap into one work
   item. Each T stage requires its own acceptance evidence.
2. **A roadmap is not an authorization.** This document is a plan. It does not
   by itself authorize executing any stage or making source, build, test, ABI,
   syscall or hardware changes. Implementation requires a separate, explicitly
   assigned task and the stage-specific acceptance gate.
3. **Simple is allowed to stay simple.** The roadmap must not imply that a
   process-local operation must go through an object. A direct syscall may be
   correct and smaller.
4. **Keep identity concepts separate.** An OID, a Handle, a capability, a
   binding, a Device and a path are not interchangeable.
5. **Distinguish evidence classes.** Write CURRENT IMPLEMENTED, PLANNED and
   AGENT-REPORTED explicitly. An agent-reported test is not an independently
   re-run test.
6. **First-PoC scope.** T8 does not require IKEv2 userland, ESP VPN, a network
   stack port, or device migration.
7. **Preserve security.** Never trade authentication, replay protection or
   authorization for migration convenience. Never add a silent plaintext
   fallback.
8. **Do not resurrect old specifications silently.** When older text conflicts
   with current source or these decisions, identify the discrepancy. Do not
   rewrite historical evidence as if it had always said the new thing.
9. **No self-authorizing task submissions.** This roadmap does not create T8 or
   later implementation tasks. T8 was completed under an explicit owner
   assignment; T9–T16 still require separate assignment. When the owner
   explicitly requests a stage, that assignment—not this document—authorizes
   it.

## 6. Cross-references

- [STATUS](STATUS.md) — current verification, the T8 result and the T7 result.
- [SYSCALL-T8-MULTIPROCESS](SYSCALL-T8-MULTIPROCESS.md) and
  [SYSCALL-T8-MULTIPROCESS.json](SYSCALL-T8-MULTIPROCESS.json) — T8 hard-gate
  evidence.
- [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md) and
  [SYSCALL-SIMPLE-RESTORE.json](SYSCALL-SIMPLE-RESTORE.json) — T7 direct-syscall
  evidence.
- [KERNEL-API-ABI](KERNEL-API-ABI.md) — kernel API/ABI boundaries and the
  five-object vs total-syscall distinction.
- [OBJECT-ABI-MIGRATION](OBJECT-ABI-MIGRATION.md) — migration history and
  provider families.
- [OBJECT-MODEL](OBJECT-MODEL.md) — object, file and Python boundaries.
- [VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md) — volatile
  objects and ObjectFIFO/TEE.
- [MINIMAL-IKEV2](MINIMAL-IKEV2.md) — current IKEv2/ESP profile.
- [REPOSITORY-LAYOUT](REPOSITORY-LAYOUT.md) — source layout.
- Working code: `kernel/core/ethercore.c`, `kernel/core/sysvobj.c`,
  `micropython/p9space.c`, `runtime/libc/port/oobject.c`,
  `runtime/libc/9sys/nsec.c`, `runtime/libc/9sys/wait.c`.

## 7. Status statement

T8 is **implemented and PASS** (2026-11-06), with the observed acceptance
matrix in [SYSCALL-T8-MULTIPROCESS](SYSCALL-T8-MULTIPROCESS.md). T9 is
**implemented as the documentation/measurement audit and PASS**; see
[SYSCALL-LEDGER](SYSCALL-LEDGER.md). T10 is **implemented and PASS** as the
narrow cleanup with the direct `_NSEC(53)` time path and the retained typed
`WAIT` path; see [SYSCALL-T10-CLEANUP](SYSCALL-T10-CLEANUP.md). T11 is
**validated and PASS** with the multiprocess ObjectFIFO evidence and one timeout
sentinel fix, plus a 2026-11-07 strengthened-validation PASS (70/70 checks, no
kernel change; subscription close while blocked NOT RUN as unreachable); see
[SYSCALL-T11-OBJECTFIFO](SYSCALL-T11-OBJECTFIFO.md).
T12–T16 remain **PLANNED, not implemented**. T7 is the previous partial result and is not
re-marked here. The roadmap is a plan and does not itself authorize executing
any stage; implementation of any T12–T16 stage requires a separate, explicitly
assigned task and that stage's acceptance gate.
