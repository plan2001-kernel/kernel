# N13 integrated process, object and UDP6 contract

This document describes the integrated process, object-view, Pipe and native
UDP6 implementation. It records the syscall and ownership contracts and the
conflict resolutions in `docs/STATUS.md`, `kernel/core/portfns.h`,
`runtime/abi/sys.h` and `micropython/p9space.c`.

## Authorization and spawn contract

`OEXECUTE` and `OSPAWN` use the same object-byte read path. It holds the
selected handle's grant gate while checking exact Space/OID, execute right,
revocation and current view membership, and while copying the published ELF
to an immutable kernel snapshot. That copy is the authorization linearization
point: a launch already admitted may finish after a later unbind or revoke.
Only the original boot process has the explicit bootstrap exception.

The original `struct Ospawn` remains 24 bytes (`cpu`, `membudget`, `flags`).
`flags=0` means no extension; `flags=1` appends the fixed 2480-byte
`struct Oinherit` (total 2504 bytes). The shared C ABI is in
[`runtime/abi/sys.h`](../runtime/abi/sys.h): `view=0` creates an EMPTY view,
`view=1` copies only the named roles, and `view=2` shares the entire explicitly
enumerated current view. SHARE admission requires the role list to match the
source view atomically; later binding edits are visible to both processes.
Each selected Pipe endpoint carries only its requested read/write rights.
Optional initial environment entries are copied into a fresh Egrp. An
optional stdout Pipe must be one of the selected writable endpoints. No O
handles, device authority or ambient network permit is inherited.

New syscall slots are OSPAWN=92, OTERMINATE=93, OBIND=96, OLOOKUP=97,
OUNBIND=98, OREVOKE=99, PIPEINHERIT=100 (RESERVED; use
OBJINVOKE(ObjOpPipeInherit=22)) and the five former NET6* numbers
NET6ADDR=116, NET6OPEN=117, NET6RECVFROM=118, NET6SENDTO=119 and
NET6CLOSE=120 (RESERVED as of phase 4; all UDP6 use is the OBJ* five-call
ABI, and address lookup is OBJINVOKE(ObjOpNet6Addr=24)). The syscall
generator, stubs and user bindings were checked against these slots and the
five-argument calling convention.

## CPU placement readiness

The boot CPU publishes `active.sched` only at the last safe handoff before
`schedinit()`, and each AP publishes it after its per-CPU prerequisite init
and immediately before its own `schedinit()`. Exact placement (`obootcpu` and
`OSPAWN`) therefore requires the target to be in range, in `active.machs`, to
have a `Mach` and to be scheduler-ready in `active.sched`. The earlier online
bit is set before LAPIC and timer setup and is not sufficient on its own. The
check never clamps, wraps or falls back to CPU 0. This is strict affinity, not
an exclusive CPU reservation.

## Resource contracts and limits

UDP6 is a process-local explicit permit, default denied for native children;
only authorized OSPAWN options can delegate it. Exec and exit revoke it,
blocked receive is cleaned up on interruption, and endpoint handles do not
reuse generations. A parent can terminate a child to revoke its permit. There
is no separate live-process grant/revoke syscall, port-scoped permit, traffic
quota or TCP6 implementation. **Update 2026-11-05:** the endpoint
has the canonical `VkindUdp6` object provider (`OBJOPEN(ObjOpenUdp6)`,
`OBJINVOKE(SendTo/RecvFrom)`); the `NET6*` numbers are RESERVED and dispatch
to nothing. `OBJINVOKE(ObjOpNet6Addr=24)` provides the authorized link-local
address lookup without opening a port, and both it and the endpoint open
enforce the boot/permit gate.

Memory budgets cap virtual mapped pages per process. Each COW or shared
mapping counts once in each process; rfork cannot drop or enlarge the
inherited ceiling. Exec replaces the charge before commit, detach releases
it, and growth beyond the ceiling fails. This is not an aggregate family
budget or resident-memory quota: kernel metadata, bounded executable staging
and shared physical pages are not charged. No swap is provided.

The OContext is in memory and does not change the GE/O v1 format. This work
does not add persisted native views, a native object graph index, full
container isolation, or audited restrictions for every legacy hardware and
administrative syscall. USB/UART and other services retain their existing
policies; no broader sandbox claim is made.

## Verification

Executed with the two sanctioned QEMU scripts, disposable files and TCG:

- Canonical MicroPython, kernel and EFI builds: PASS; kernel 124/124,
  selected closure 126/126; MicroPython 240 units, zero failures; EFI build
  PASS. Shared ABI layout check: `Ospawn=24`, `Oinherit=2480`, extension
  2504 bytes.
- `tools/qemu-o-boot.sh --tcg --cpus 4 --obootcpu 2 --udp6-peer 19100:19101`
  with the temporary seeded native proof: PASS, ending
  `INTEGRATED CORE PASS 64 65 UDP6 ... 19000`. It exercised authorized and
  denied object operations, concurrent COPY/EMPTY/SHARE children, explicit
  Pipe IPC/backpressure/EOF, stdout routing, exact CPU placement, spawn and
  rfork budgets, cleanup failures, UDP6 peer round-trip and endpoint cleanup.
- `tools/qemu-o-persist.sh --tcg --repl`: PASS on the same disposable disk;
  variable, function and alias identity survived reboot
  (`n13saved 314159 3 True` → `n13restored 314159 3 True`).
- Negative boot selectors (offline CPU and unknown executable OID): expected
  rejection. Simulated installer final-LF check and shell syntax: PASS.
- Whitespace check: PASS. ShellCheck, KVM, physical hardware/ESP/GRUB and
  physical p5 operations: NOT RUN.

Reproducible temporary inputs and full logs are under the ignored
`build/n13-integration/` directory. During bring-up, tests exposed and led to
fixes for a stale-PID termination window, blocked UDP receive cleanup,
concurrent GE sync buffer corruption, and premature REPL-input completion
detection. Earlier failed runs were setup/input issues and are retained only
in that ignored build evidence directory; the final runs above passed.
