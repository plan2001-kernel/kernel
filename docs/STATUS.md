# Plan2001-O — current status

> **Architecture note (2026-11-06):** older wording that the kernel boundary is
> “exactly five syscalls” is **superseded**. The five universal operations
> `OBJCREATE/OPEN/CLOSE/BIND/INVOKE` (136–140) are the general *object*
> boundary, not the whole syscall ABI. T7 restored seven direct process-local
> calls. The observed table is 135 named / 62 active / 73 reserved. The target
> is a small, coherent, understandable interface, not a numeric count. T8 is
> implemented and verified as recorded below (PASS, 2026-11-06). T9 is a
> documentation/measurement audit and is now recorded as PASS (2026-11-06);
> T10 is implemented as the narrow time/WAIT cleanup and is PASS (2026-11-06);
> T11 is validated and PASS (2026-11-06) with one FIFO infinite-timeout
> sentinel fix and a strengthened-validation follow-up PASS (2026-11-07, no
> kernel change); T12 passes its emulated USB endpoint integration gate
> (66/66 checks PASS, 2026-11-07; publication BLOCKED); T13 passes its emulated raw-Ethernet gate (55 checks and a fresh 66-check USB regression); T14 passes its bounded emulated IPv6 gate (24 guest / 245 peer assertions); T15 passes its minimal ESP integration gate (13 guest / 43 peer assertions); T16 passes its emulated USB/Ethernet consolidation gate (fresh 66/55); see
> the stage results below.

## Experimental ARM64 boot foundation (2026-11-08, QEMU PASS)

`docs/ARCHITECTURE-SUPPORT.md` records the bounded AArch64 boot proof: a
freestanding Clang/LLD kernel (`build/arm64/9karm64.elf`) and UEFI loader
(`build/arm64/bootaa64.efi`) enter EL1 on QEMU `virt`, enable a 2 GiB boot map
and PL011 console, validate the existing BootInfo v1 blob through the shared
`kernel/core/oplanboot.c`, reserve the kernel image and blob, hand out zeroed
4 KiB pages from the map's RAM (with an allocate/free/re-allocate self-test),
print the ARM64 boot marker and take three EL1 timer interrupts.
`tools/build-arm64.sh`, `tools/host/build-efi-arm64.sh` and
`tools/qemu-arm64-boot.sh` reproduced this build/boot in this round. The amd64
boot was **NOT RUN** and the amd64 compile/link was **NOT RUN** on this
AArch64 host (no x86-64 GCC/binutils cross toolchain; the earlier Debian-host
build remains the amd64 evidence). ARM64 has no scheduler, virtual memory,
GE/O, syscalls, userspace, networking, USB, storage or SMP support, and
physical ARM64 hardware was **NOT RUN**. amd64 remains the only supported
target.

## T16: common USB/Ethernet Device ABI (2026-11-08, emulated gate PASS)

[T16-DEVICE-RESULT](T16-DEVICE-RESULT.md) records a source-backed contract /
provider / legacy inventory, canonical builds and **fresh** T12 USB 66/66 plus
T13 Ethernet 55/55 checks on the exact T16 implementation tree. Both boots
reach normal GE/O commit/parent REPL. No runtime, ABI, driver or test changes
were needed; documentation now separates USB completion from Ethernet FIFO
semantics and clarifies control authorization/open-admission bounds.
USBIOCTL enumeration/preparation remains a demonstrated dependency, with a
separate legacy handle policy; no universal USB sandbox claim is made.

T16 explicitly excludes **all Wi-Fi** implementation/emulation/firmware/family/
service/tests; later work is unspecified and separately assigned. T11 is the
FIFO prerequisite; T14/T15 and TCP/UDP are not. T14/T15 reruns are NOT RUN
because there is no common-code change. Physical devices, combined-device
single-boot campaign and unforced failure/race cases are NOT RUN. Earlier
stage reports below remain dated evidence, not fresh T16 results.

## T15: minimal userspace ESP integration (2026-11-08, emulated gate PASS)

[T15-ESP-RESULT](T15-ESP-RESULT.md) records 13 guest assertions and 43
independent real-peer assertions: userspace transport-mode NH50 ESP, exact
bidirectional decrypted echo, packet tags/nonces, ordinary ND, no plaintext
echo fallback, existing Device ABI/ObjectFIFO and authorized child Handles.
Fresh T13 55/55 and T14 24 guest /245 peer regressions PASS on the same MPY
ELF; normal boot/GE/O commit/parent REPL and canonical builds PASS. The small
MPY aesgcm binding reuses existing BearSSL/espcrypto code in userspace; no
kernel networking/syscall/ABI/provider is added. Fixed public demo SAs are
TEST-ONLY, with no security across restart/reuse or production guarantees.
No persistent bundle, lifecycle/replay-window or VPN work. Physical NIC and
production security tests were NOT RUN in that round; current T16 is above. Scope:
[ESP-SA-BUNDLE](ESP-SA-BUNDLE.md), superseding the earlier expansive proposal.

## T14: userspace IPv6 (2026-11-08, minimal emulated gate PASS)

[T14-IPV6-RESULT](T14-IPV6-RESULT.md) records 24 guest assertions and 245
independent external-peer assertions over real virtio/T13 Device ABI/ObjectFIFO:
static IPv6/ICMPv6/ND, both echo directions, checksum/length/MAC validation,
multicast/DAD replies, bounded neighbors/aging/retries/timeouts, malformed
input, controlled close/revoke and no residual responder. A separate MPY
child runs on CPU 1; the parent returns to the normal committed GE/O REPL.
Fresh T13 regression (55/55) and canonical two-boot persistence PASS. All four
canonical builds PASS, with zero kernel/ABI/MPY C changes. Source/API and limits:
[user/ipv6](../user/ipv6/README.md). Physical NIC/KVM, full original T8 campaign
and USB T12 rerun were NOT RUN; no common device code changed. T15 ESP was
still PLANNED at that historical round. See the current T15 gate above. Earlier
documentation-round NOT RUN statements below are historical.

## Historical network target correction (2026-11-08, documentation only)

> Superseded by the later minimal T15 assignment and result above. This
> dated documentation-round description is retained as decision history;
> its bundle/lifecycle/security requirements are no longer active.

IKEv2 is excluded entirely from the active PoC and future target, including
handshake/negotiation, PSKs, UDP/500, an IKE daemon and deferred migration.
[ESP-SA-BUNDLE](ESP-SA-BUNDLE.md) specifies **PLANNED**, externally provisioned
userspace ESP: one versioned bounded GE/O bundle, two directional SAs,
four secret components/40 bytes plus SPIs/IPv6 selectors/profile/generation/
lifetime/policy, trusted confidential external delivery, tag/replay checks,
no nonce reuse/restart reactivation and fail closed. Manual AES-GCM remains
experimental with the RFC 4106 automatic-key-management compliance caveat.

T13 is merged at GitHub main `4e7f7a1bf4de126c3e99480b6c911af721d6a620`,
with its recorded emulated PASS; physical NIC NOT RUN. T14 IPv6/ICMPv6/ND
must be separately demonstrated before T15 ESP. UDP/TCP and T16 are independent.
Existing kernel ESP/control code is historical reuse material, not userspace
ESP evidence. [MINIMAL-IKEV2](MINIMAL-IKEV2.md) remains a NON-TARGET report.

This correction changes documentation only. Builds, boots, protocol tests and
hardware operations are **NOT RUN**, and earlier PASS results are not reruns.
See [T15-DOCS-RESULT](T15-DOCS-RESULT.md) for scope/checks/reference inventory.

## T13: raw Ethernet (2026-11-07, emulated integration gate PASS)

[T13-ETHERNET-RESULT](T13-ETHERNET-RESULT.md) records the verified GitHub T12
baseline, canonical builds, 55 executed T13 assertions on 2 QEMU/TCG CPUs,
complete external-peer frames with EtherType 0x88b5, cross-process TEE,
independent overflow, real bounded-handoff drops, rights, revoke, exit,
flood-time detach and link-down/recovery. The same binaries pass a fresh
66/66 T12 USB run. Contract: [DEVICE-ABI](DEVICE-ABI.md). Physical hardware,
TX queue/allocation fault injection and exhaustive race testing are NOT RUN.
The old native IP service startup and OBJ TCP/UDP opens are disabled under
the task's explicit override; remaining linked protocol code is documented.
GitHub branch/PR delivery follows the current workflow; MCP source snapshots
remain separate. The older T12 publication-blocked narrative below is
historical, not a blocker on the current GitHub branch workflow.

## T12: common device ABI (2026-11-07, emulated endpoint gate PASS)

The contract is [DEVICE-ABI](DEVICE-ABI.md); exact commands, patch provenance,
artifact hashes and limits are in [T12-DEVICE-RESULT](T12-DEVICE-RESULT.md).
The finishing pass requires Vcontrol for side-effecting USB setups (including
root-hub reads), latches sessions after provider transfer errors, and adds
tracked focused tests. Existing USB administration prepares a downstream
endpoint; its complete data/completion path uses the T12 OBJ session.

All four canonical builds PASS. The final disposable QEMU/TCG xHCI+mouse boot
passed **66/66 executed assertions**, including a real peripheral descriptor,
short reply, OUT transfer, actual STALL and reopen, idle detach, ObjectFIFO
handoff, Handle exhaustion/receive retry, concurrent process aliases and
last-owner process exit with a pending response. Original root-only 34/34
evidence is superseded for this gate, not counted again.

Physical hardware, active-transfer unplug/timeout injection, broad regressions
and T13–T16 are NOT RUN. Source publication is **BLOCKED**: no writable source
MCP operation is available; neither SSH nor task_report substitutes for it.
The full verified patch/source artifacts are supplied for integration; kuha
and server CURRENT have not been updated by this round.

## T11: ObjectFIFO multiprocess validation (2026-11-06 PASS; strengthened 2026-11-07 PASS)

The T11 result is [SYSCALL-T11-OBJECTFIFO](SYSCALL-T11-OBJECTFIFO.md) and its
[.json](SYSCALL-T11-OBJECTFIFO.json).

- **Original round (2026-11-06):** 59 executed checks, 0 FAIL, across three
  normal boots with real independent MicroPython processes. Boot A (15 checks)
  covered TEE with three subscriptions on one FIFO, per-subscriber bounded
  queues, drop-oldest (`slow` kept `A4,A5` with 4 drops; `fast` kept all six
  with 0), envelope, identity, empty queue and finite timeout. Boot B (32
  checks) covered sender `Vsend`/`Vdelegate` requirements, grant/subscription
  attenuation, negative and stale handles, FIFO-reference rejection, revoke
  wakeup, receiver exit and 50 teardown/repeat cycles. Boot C (12 checks, 2
  vCPUs) covered a GE/O capability handoff with OID/bytes preserved, no ambient
  Handle access, Handle exhaustion at 96 live Handles, and global quota
  saturation at 65,536 queued messages (the next send fails `Enomem` before
  partial publication).
- **Follow-up (2026-11-07, no kernel change):** four review-identified weak
  assertions were replaced and the suites rerun against the same kernel
  artifact `5b2dd2e1df2e7dd1b7f55c9261a9976b6123a0b1068ed23ef4330784901ee391`:
  boot A 15/15 (`build/t11/logs/bootA10/`), boot B 35/35
  (`build/t11/logs/bootB7/`), boot C 20/20 (`build/t11/logs/bootC4/`) —
  **70/70 observed PASS**. The new checks require the exact `Ehungup`
  (`i/o on hungup channel`) after a 300 ms no-result window (`blocked-settle`);
  a real full-width non-delegated parent Handle passed as data, with an
  authorized delegated control and exact `Ebadarg` denial (`ambient-result
  110`); exact reaped PID and `micropython <pid>: terminated` status; and the
  exact `Enomem` (`kernel allocate failed`) with all eight of 64 witness
  queues unchanged at `1024/1024/0`. A mutation run
  (`build/t11/logs/mut1/`, `MUT-RESULT 3 6`) confirms those assertions fail for
  deliberately wrong conditions. Subscription close while a receive is already
  blocked is **NOT RUN**: source analysis shows the receive's Handle reference
  delays `vsubshutd`, but the path is unreachable because Handles are
  process-local and subscriptions cannot cross OSPAWN/RFORK; no kernel fix was
  made.
- **One fix:** `ObjOpRecv` now accepts the signed/32-bit infinite timeout
  sentinels (`0xffffffffffffffff`, `0xffffffff`) and keeps finite waits
  bounded. Before the fix, the adapter default `-1` was rejected with
  `Ebadarg`; after it, default `obj_receive()` blocks and is woken by revoke or
  process termination. No syscall, operation number, ABI layout or dispatch
  entry changed.
- **Policy:** `vobjfifosub()` requires `Vsubscribe`, requires parent `Vsend`
  when the subscription requests `Vsend`, and does not accept `Vcontrol` or
  `Vsubscribe`. The remaining class bits are receive-policy masks intersected
  with the sender's object-Handle rights; no amplification was reproduced.
  The FIFO delivery core has no protocol/device branch.
- **Regressions:** canonical kernel/EFI/MicroPython/host builds PASS; T8
  multiprocess REPL PASS; T10 direct `_NSEC` time PASS (original round). The
  2026-11-07 follow-up did not rerun them because it changed no kernel, ABI or
  build input; it used the same evaluated artifact hashes. Dispatch table
  remains 135 named / 62 active / 73 reserved.
- **NOT RUN:** physical hardware, TCG, long randomized multi-CPU stress,
  individual allocation fault injection, exhaustive lock-order proof, direct
  kernel sleeper-state observation, and quota/rights combinations beyond the
  recorded cases; subscription close while a receive is blocked remains NOT RUN
  as unreachable through the supported contract.

## T10: narrow syscall semantic cleanup (2026-11-06, PASS)

The T10 result is [SYSCALL-T10-CLEANUP](SYSCALL-T10-CLEANUP.md).

- **Time changed:** libc `nsec()` now calls direct `_NSEC(53)` instead of
  `OBJOPEN(ProcessSelf)` + `OBJINVOKE(ObjOpTimeNow=27)` + `OBJCLOSE`.
  `plan9.time_ns()` uses the same direct libc call. The 64-bit
  `todget(nil,nil)` source, epoch, precision and `vlong nsec(void)` signature
  are unchanged. The old adapter returned 0 on Handle-open/invoke failure; the
  direct call has no failure path and always returns the current count. This
  re-specification is deliberate and documented.
- **Adapter retired:** `obj_process_self_time()` had no remaining source
  caller and was removed from libc and its header. The kernel `ObjOpTimeNow=27`
  provider is retained for raw `OBJINVOKE` users; its historical value and the
  generated dispatch table are unchanged. The table remains **135 named /
  62 active / 73 reserved**.
- **WAIT retained:** `obj_wait()` stays on
  `OBJOPEN(ProcessSelf,Vcontrol)` + `ObjOpWait=35` + `OBJCLOSE` because the
  reserved `_WAIT(36)`/`AWAIT(47)` cannot carry the typed `ObjWait` record and
  adding a number is out of scope. `pwait()` remains the sole kernel core.
- **Other candidates retained with reasons:** `ERRSTR(41)`, the OSPACE family,
  semaphores/segments/`RENDEZVOUS`, USB data I/O and the unsupported/legacy
  entries were not removed; missing trace callers are not proof of safe
  removal. No syscall was renumbered or reused.
- **Verified:** direct time REPL (`T10-TIME-MONO True`, two 64-bit values);
  normal parent REPL with child `space.spawn()`/`space.wait()` success,
  isolation, failed child, missing/bad ELF and denied writer; two-boot GE/O
  persistence (`T8-PERSIST 4242`). Kernel build 132/132, 134/134 closure,
  ABI/link PASS; MicroPython 247 units, 0 FAIL, no undefined symbols.
  **NOT RUN:** per-number runtime syscall trace, physical hardware/TCG,
  multi-CPU stress, long-blocking wait, T11–T16.

## T9: active syscall ABI semantic audit (2026-11-06, PASS — documentation only)

The authoritative source-derived syscall ledger is
[SYSCALL-LEDGER](SYSCALL-LEDGER.md). It covers every active syscall at the
measured published snapshot
`2b3e6126f2d195fc3c3d8d89f9410fd228ec87ff0adbce7880d5a955e7d62253`
(2026-11-06T18:27:56Z) on `kuha`.

- **Inventory re-derived, not trusted from older reports:** **135 named /
  62 active / 73 reserved**. Undefined gaps are 48, 49, 94, 95, 114 and 115.
  Historical discrepancy is documented: T0 measured 65/70 (including
  `_WAIT`, `AWAIT`, `OSPAWN`, `NOTIFY`, `NOTED`, `RFORK`, `OTERMINATE`);
  T6 retired seven to 58/77 and T7 restored four to the current 62/73.
- **Every active entry** records handler/dispatch, argument layout and
  validation, return/error encoding, authorization/handle lifetime, real
  static callers, observed/not-observed runtime status, and a
  category/disposition. The five universal `OBJ*` calls are treated as the
  object boundary, not the whole ABI; reserved numbers and Object operations
  are kept distinct.
- **Evidence:** static `objdump`/`nm` over `build/gcc/micropython.elf` and
  `build/gcc/gen/obootoid.elf`, plus the existing T7 trace
  (`build/syscall-simple/trace3/serial-boot.log`) and T8 multi-process logs
  (`build/t8/final-multiproc/`). T9 did not rebuild, add tests or run
  hardware. 25 active numbers have no source callsite and no direct call in
  the measured binaries; this is recorded as “truly unused in this tree”, not
  as permission to remove them.
- **No source, build script, test or ABI change occurred in T9.** The ledger
  identifies high-value questions for the separate T10 decision. T10 was
  completed separately; see the current T11–T16 status above.

## T8: first complete multi-process MicroPython PoC (2026-11-06, PASS)

The normal canonical boot reaches the parent MicroPython REPL; the parent
starts independent MicroPython children from the genuine `micropython.elf`
stored at GE/O OID `0x4f45584543000003` through
`space.spawn()` → `OBJINVOKE(ObjOpOspawn)`; the children run simple `-c`
programs, print through an explicit stdout Pipe, exit, and are reaped by
`space.wait()` with the correct PID/status. Two-boot GE/O persistence still
passes, including a REPL-assigned global.

- **One real fix:** `kernel/core/sysvobj.c` `vobjconsoleopen()` now allocates
  the console object with `cr.stream` reserved bytes. `VkindConsole` stores
  the stream number in `Vobj.size`, and `vobjput()` releases that many bytes;
  the old zero-byte reservation underflowed the global byte counter on the
  first successful piped child's console close, after which later
  `vobjalloc()` calls failed `Enomem` (`space.wait()` → `kernel allocate
  failed`). No syscall, object operation or ABI was added or changed; the
  dispatch table remains **135 named / 62 active / 73 reserved**.
- **PASS:** normal boot → REPL without a test-only boot selector; two-boot
  persistence (Counter 1 → 2; boot1 final commit 2, boot2 restore 2 and final
  commit 4; REPL `X8` 4242); genuine ELF
  OID bytes equal `micropython.elf`; independent child PIDs (10/11 vs parent
  3); observable child output `b'PID10'` and `b'ISO'`; success status `''`;
  failed child `'micropython 12: micropython'`; missing OID
  `oexec: no such object`; non-ELF `ospawn: not a static x86-64 ELF`;
  isolation (child sees no parent global, `space.info()` is None); child
  `space.open_handle('init')` denied `Eperm` (`OSError: permission denied`);
  parent remains the writer (`space.oput` succeeds, version advances) and
  the REPL stays interactive after every case. T7 OSPAWN/WAIT and the
  negative syscall/spawn regressions still pass.
- **Build:** kernel 132/132 selected, 134/134 closure, ABI and link PASS,
  `072d2c5ab267ac7375cb8685de0aee5b04018184bfe4668dadb060dbe9081261`;
  MicroPython 247 units, 0 FAIL, `bc9df483ab82a6c59a3b3d706f04cf574ef22ec13719b65ec6d096694e11d653`;
  EFI `d37c1ca2...`; host build PASS.
- **NOT RUN:** physical hardware, TCG, multi-CPU stress, multiple
  simultaneous unauthorized writers, long-blocking children, budget/heap
  exhaustion and ObjectFIFO multi-process validation (T11). See
  [SYSCALL-T8-MULTIPROCESS](SYSCALL-T8-MULTIPROCESS.md) and `.json`.

## T7: seven simple syscalls restored to direct historical numbers (2026-11-06, PASS/PARTIAL)

`BRK_(24)`, `SLEEP(17)`, `NOTIFY(28)`, `NOTED(29)`, `RFORK(19)`,
`OTERMINATE(93)` and `EXITS(8)` are direct historical syscalls on the
MicroPython path again.  Their former `OBJINVOKE` variants
(`ObjOpHeapBreak=26`, `ObjOpSleep=28`, `ObjOpNotifyRegister=30`,
`ObjOpNoted=31`, `ObjOpRfork=32`, `ObjOpOterminate=34`, `ObjOpExit=36`) are
retired with no dispatch case; a raw `OBJINVOKE` rejects them with
`obj: operation not supported`.

- Kernel cores are shared, not duplicated: `ibrk(BSEG)`, `kernelsleep()`,
  `setnotify()`, `notecont()`, `kernrfork()`, `kernoterminate()` and
  `pexit(status,1)`.  The T5b amd64 SysV `RDI/RSI` note ABI and the
  `lentry.S` `noteret`/`iretq` return are unchanged.  `sysnoted()` returns
  the private `NOTED_RET` marker and `dosyscall()` skips the ordinary
  `*retp` write only after a successful note return.
- Userland uses generated GCC stubs directly.  `sbrk()` calls `brk_`;
  `plan9.sleep()`, `main.c` and `lock.c` call `sleep`; `atnotify.c` calls
  `notify`/`noted`; `rfork()` and `space.terminate()` call `rfork`/
  `oterminate`; `exits() -> _onexit -> _exits` calls `EXITS`.  The old
  `9sys/rfork.c`, `9sys/notify.c` and `9sys/noted.c` adapters were removed.
  `ProcessSelf` stays for `ObjOpTimeNow` and `ObjOpWait`; `OSPAWN(92)`,
  `_WAIT(36)` and `AWAIT(47)` were not changed.
- Dispatch table: **135 named / 62 active / 73 reserved** (before 135/58/77).
- Canonical builds PASS: kernel 132/132 selected, 134/134 closure, ABI and
  link PASS, `8fd572b985fd57d2b348d946827ccb13b15b9a931e4e2613db7dbcb4c6bf8ead`;
  MicroPython 247 units, 0 FAIL, no undefined symbols,
  `bc9df483ab82a6c59a3b3d706f04cf574ef22ec13719b65ec6d096694e11d653`;
  EFI `06cddbfc...`; host build PASS.
- Disposable KVM QEMU PASS: normal boot -> REPL; two-boot GE/O persistence
  (Counter 1 -> 2, versions 1 -> 2); BRK valid/error; SLEEP; RFORK child
  0/PID with child `EXITS('child-ok')` and parent `WAIT`; direct
  `OTERMINATE` of a sleeping child; OSPAWN/WAIT regression; `NOTIFY`
  registration/`nil`; `NOTED` NCONT with the correct user `Ureg*` in RDI and
  note text in RSI; `NOTED` error without an active note; real MicroPython
  `onnote`+`atnotify`+`NOTED(NCONT)` with a test-only alarm-starter kernel
  build; retired `ObjOp` rejection; self-OTERMINATE, missing OSPAWN OID and
  bad RFORK flags negatives.
- Instrumented trace PASS: `nr=8/17/19/24/28/29/93` = 1/4/2/8/3/2/1 ACTIVE
  records, 4,985 records, dropped=0; retired `ObjOp` 26/28/30/31/32/34/36
  counts are zero.  Static stubs verified with `objdump`; `obootoid` still
  uses `mov $0x8,%rbp`.
- **PARTIAL / NOT RUN**: physical hardware, TCG, ordinary alarm service and
  tick/HZ, functional `_onexit` registration, the full T6 authorization
  matrix, and T1-T5b re-measurement.  See
  [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md) and `.json`.

## T6 (historical): process lifecycle over five typed OBJ operations (2026-11-06, PARTIAL)

> Superseded for `RFORK(19)` and `OTERMINATE(93)` by T7 above; `EXITS(8)`
> was already active.  `OSPAWN(92)`, `_WAIT(36)` and `AWAIT(47)` remain as
> described here.


`RFORK(19)`, `_WAIT(36)`, `AWAIT(47)`, `OSPAWN(92)` and `OTERMINATE(93)` are
permanently RESERVED (`nil` dispatch, `sysreserved=1`). The replacement is
`OBJINVOKE(ObjOpRfork=32/ObjOpOspawn=33/ObjOpOterminate=34/ObjOpWait=35/
ObjOpExit=36)`. `EXITS(8)` deliberately remains active because the separate
`obootoid` bootstrap image's error path still uses it; that is documented as a
T10 dependency. The table is 135 named / 58 active / 77 reserved.

- Kernel cores are shared, not duplicated: `kernrfork`, `kernospawn`
  (explicit `VkindSpace` + `Oexecute`), `kernoterminate`, `pwait()` in
  `vobjwait`, and `pexit(status,1)` in `vobjexit`. The legacy direct handlers
  are gone. No new syscall number and no GE/O/tick/ALARM/network/note change.
- Userland `rfork()`, `wait()`, `await()` and `exits()` are libc adapters over
  the typed operations; `space.spawn`/`space.terminate` use `obj_ospawn`/
  `obj_oterminate`. The MPY ELF has no retired stub and no retired undefined
  symbol. `obootoid` is unchanged and still shows `mov $0x8,%rbp` in its
  `exits` stub.
- Canonical builds PASS: kernel 132/132 selected, 134/134 closure, ABI and
  link PASS, SHA-256 `d6f9029fe0e3938298c342ffe043900127ba42180a8cfdc9579a747b1bb2bb20`;
  MicroPython 250 units, 0 FAIL, no undefined symbols, SHA-256
  `ca2e140256896b2ae1a54f46a40bc6f2b5439e7ae2beaad03b894c7ceaefb484`.
- Disposable KVM QEMU PASS: normal boot -> REPL `6*7=42`; two-boot GE/O
  persistence (Counter 1 -> 2, versions 1 -> 2); `rfork(RFPROC)` child 0 /
  parent PID, group-only `RFENVG` change, child `exit('child-ok')` and parent
  `wait()` status; `OSPAWN` from the real MPY OID with a reaped child;
  `OTERMINATE` of a direct sleeping child; missing OID, oversized argv and
  bad Handle errors; raw 19/36/47/92/93 `Ebadarg` without a fatal note.
- Instrumented trace PASS: ObjOp 32/33/34/35/36 executed (1/1/1/2/1 across two
  runs), all five retired numbers 0 on parent and child paths, 0 dropped.
- **PARTIAL / NOT RUN**: explicit OSPAWN vhandles and the other inheritance
  combinations, wrong-Vkind/missing-rights/wrong-PID/non-child cases, very long
  status/argv, heap/budget limits, synchronization faults, and the
  `EXITS`/`OEXECUTE` loader error fallback. T1-T5b were not re-measured.
  See [SYSCALL-T6-PROCESS](SYSCALL-T6-PROCESS.md) and `.json`.

## T2: MicroPython persistent GE/O writes/commit through the five OBJ calls (2026-11-05)

The normal MicroPython REPL/persistence write path in `p9space.c` now uses
`OBJINVOKE(ObjOpPutOid=25)` for named-OID replacement,
`OBJINVOKE(ObjOpCreatePersist=12)` for atomic new-object creation and
`OBJINVOKE(ObjOpCommit=13)` for the final commit, all on T1's authorized
`VkindSpace` Handle. `ObjOpPutOid` is a typed INVOKE operation, not a new
syscall: it stages one OID without committing, keeps the 64 MiB native
transaction bound, and preserves first-boot OID_GRAPH/Environment/code writes.

- `ocommit()`, `space_oput()`, `space_oregister()` and
  `p9_space_create_fun_code()` no longer call `OSPACEPUT(57)`,
  `OSPACECREATE(61)` or `OSPACECOMMIT(58)`. The old numbers and their kernel
  handlers remain; the only linked callers are the explicit legacy
  `space.hput/hcreate/hcommit` API (T2b target).
- T0 instrumentation on the new code measured `OSPACEPUT = 0`,
  `OSPACECOMMIT = 0`, `OSPACECREATE = 0` and `OSPACEGET = 0` in every
  MicroPython init/REPL/persistence phase. `obj_put_oid` is 3 in init and 18
  in REPL; `obj_commit` is 1 and 6 — exactly T0's former 27 writes and 9
  commits, so commits did not grow.
- Normal two-boot persistence PASS (version 1 -> restore 1 -> commit 2,
  Counter n 1 -> n 2). REPL two-boot persistence PASS: a global, a function,
  a replaced persistent byte object and an empty object survived; invalid OID
  raised `ValueError`, missing OID and a read-only Handle raised `OSError`.
- Evidence: [SYSCALL-T2-GE-WRITE](SYSCALL-T2-GE-WRITE.md) and `.json`;
  logs under `build/syscall-t2/`.
- **NOT RUN**: physical hardware, TCG, >1 MiB direct byte objects, and the
  `space.h*` migration (T2b).

## T1: MicroPython GE/O reads through the five OBJ calls (2026-11-05)

MicroPython's GE/O read paths in `p9space.c` now use
`OBJOPEN(ObjOpenSpace)`, `OBJOPEN(ObjOpenGeSpace)`, `OBJINVOKE(ObjOpRead)`
and `OBJCLOSE`. The legacy `OSPACEGET` read syscall is still active for the
`obootoid.elf` error fallback (T10), but MicroPython no longer calls it.

- `p9space.c` opens one authorized `VkindSpace` read Handle and one GE Handle
  per OID; the legacy Space Handle remains for the T2 write/commit path.
  `space.oget`, `space.hget` and the Environment/code/OID_GRAPH loader use
  the OBJ path.
- `ObjINVOKE(Read)` on `VkindGe` now returns the published O version in
  `ObjCall.aux`; libc adds `obj_read_version()` and MicroPython exposes
  `plan9.obj_read_version()`. Other providers keep `aux=0`.
- The 1 MiB per-call OBJ read bound is preserved; an oversized read raises an
  error instead of truncating. No syscall was added, renumbered or removed.
- T0's instrumentation on the new code measured **OSPACEGET = 0** in the
  MicroPython init/commit, REPL and both persistence boots (T0 baseline: 52
  calls). `OBJOPEN`/`OBJINVOKE`/`OBJCLOSE` carry the reads; the boot loader's
  fallback remains linked but unexecuted.
- Canonical builds PASS; normal O-boot -> REPL -> `6*7`, two-boot
  persistence/restore, and normal smoke PASS. REPL edge tests PASS: missing
  OID, empty object, bad Handle, Vwrite-only Handle, version 5 -> 35, and
  100 open/read/close cycles with no Handle growth.
- Evidence: [SYSCALL-T1-GE-READ](SYSCALL-T1-GE-READ.md) and
  `.json`; logs under `build/syscall-t1/`.

## NET6 UDP6 syscall retirement (phase 4, 2026-11-05)

The five former NET6 UDP6 compatibility calls are retired:
`NET6ADDR=116`, `NET6OPEN=117`, `NET6RECVFROM=118`, `NET6SENDTO=119` and
`NET6CLOSE=120` are permanently RESERVED (`nil` `systab`, `sysreserved=1`,
no linked `sys*` handler symbol, raw call `Ebadarg`). The table is now
**135 named / 66 active / 69 reserved**; numbers were not renumbered or
reused and no new syscall was added.

- UDP6 endpoints are `OBJOPEN(ObjOpenUdp6)`, data is
  `OBJINVOKE(ObjOpSendTo=14/ObjOpRecvFrom=15)` with the unchanged
  IPv6-only datagram, 0–1232-byte, address/port and millisecond-timeout
  semantics, and release is `OBJCLOSE`.
- The address lookup is `OBJINVOKE(ObjOpNet6Addr=24)`: handle 0,
  `in==NULL`, `outlen=16`, same `obootstrap`/`net6permit` gate as before,
  and no port is reserved. Default Handle rights and explicit
  OSPAWN/`Vdelegate` delegation are unchanged.
- libc adds `obj_udp6_addr`; MicroPython adds `obj_udp6_addr` and keeps
  `net6_addr/net6_open/net6_sendto/net6_recvfrom/net6_close` as userland
  adapters over `obj_udp6_*`/`obj_close`. `nativeudp6.c` is unchanged.

Fresh evidence on `kuha` (disposable KVM QEMU, local IPv6/UDP peer):

- Canonical kernel PASS: 132/132 selected, 134/134 full closure, ABI PASS,
  link PASS, 0 undefined symbols (`build/gcc/9pc64.elf`, 1,149,936 bytes,
  SHA-256 `9f7a8219505bd17a937e3dc586a139073868fbd9c3afc20c2edfadd69ecffd39`).
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols
  (`build/gcc/micropython.elf`, 533,672 bytes, SHA-256
  `ef521c803e72ff1894b9f61e0cfcd7ffa3842c13e8db302483c91ead8732384c`).
- Static retirement PASS (`build/qemu-o/phase4-static/check.txt`): all five
  names have `nil` dispatch and `sysreserved=1`, no `sysnet6*` symbol is
  linked, `vudp6addr` is linked, and `modplan9.o` has no raw `net6*`
  relocation; it references only `obj_udp6_addr/open/sendto/recvfrom` and
  `obj_close`.
- Guest/host QEMU PASS (`build/qemu-o/phase4-udp6c/`, REPL `PHASE4 PASS`):
  real peer received `NET6-PING` from guest port 4444 and `OBJ-PING` from
  port 4445; the guest received both replies with the peer address and
  source port 40000. `obj_udp6_addr` and the `net6_addr` adapter returned
  the same 16-byte link-local address. A read-only Handle denied send
  (`Eperm`), `GRANT` amplification was denied (`Eperm`), a second close
  and a stale send both returned `Ebadarg`, and raw calls 116–120 each
  returned `Ebadarg`; no panic, bad-disk, flush or sync error.
- **NOT RUN**: physical network hardware, multi-CPU stress and a
  receive-buffer-too-small path (the MicroPython binding always supplies
  the 1232-byte capacity). No USB or device-driver work was added.

## Pipe/UART/KBD syscall retirement (phase 3, 2026-11-05)

The native Pipe, UART and PS/2 keyboard compatibility family is retired.
Numbers 71-74 (`PIPENEW/READ/WRITE/CLOSE`), 75-78
(`KBDOPEN/READ/CLOSE/LEDS`), 79-85 (`UARTCOUNT/OPEN/READ/WRITE/IOCTL/STATUS/
CLOSE`) and 100 (`PIPEINHERIT`) are permanently RESERVED: `systab` entries
are `nil`, `sysreserved=1`, no `sys*` handler symbol exists, and a raw call
returns `Ebadarg` without posting a fatal note. At the end of phase 3 the
table was **135 named / 71 active / 64 reserved**; after phase 4 it is
**135 named / 66 active / 69 reserved**. `OBJ*` 136-140 remain the only
object boundary.
No syscall was added, renumbered or reused.

- Pipe is reached through `OBJCREATE(ObjKindPipe)` / `OBJINVOKE(PipePeer)` /
  `OBJINVOKE(Read/Write)` / `OBJCLOSE`. The one-shot `RFPROC` selection is
  `OBJINVOKE(ObjOpPipeInherit=22)` with `arg0` carrying legacy bits 1/2;
  the mapped rights must be a subset of the Handle, the next `RFPROC`
  consumes the mark, and `OSPAWN` keeps its separate explicit `pipes` list.
- `OBJINVOKE(ObjOpUartCount=23)` is the handleless discovery call (handle 0)
  returning the same `uartnports()` index space as `OBJOPEN(ObjOpenUart)`.
- `OBJINVOKE(ObjOpKbdLeds=19)` keeps the per-Handle `Vcontrol` form and also
  accepts handle 0 as the eve-only global LED control that does not claim
  the PS/2 scan-code controller.
- MicroPython keeps `pipenew/piperead/pipewrite/pipeclose/pipeinherit`,
  `uart_count/uart_open/uart_read/uart_write/uart_ioctl/uart_status/
  uart_close` and `kbd_open/kbd_read/kbd_close/kbd_leds` as userland
  adapters over OBJCREATE/OPEN/INVOKE/CLOSE; they make no retired call.
  `plan9.sysraw(n)` is a diagnostic userland trampoline for the raw-number
  tests and adds no kernel entry point.

Fresh evidence on `kuha` (disposable KVM QEMU):

- Canonical kernel PASS: 132/132 selected, 134/134 full closure, ABI PASS,
  link PASS, 0 undefined symbols (`build/gcc/9pc64.elf`, 1,154,216 bytes,
  SHA-256 `c705bc55764a474e502e06a45e38070f662e56f12686e1e02a2683cbb7ad936e`).
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols
  (`build/gcc/micropython.elf`, 533,512 bytes, SHA-256
  `2cf35f5dd197692d351997bb27cac92d111f24e5501e37156cd8a54c76bb6ef2`).
- Static retirement PASS: all 16 reserved numbers have `nil` dispatch and
  `sysreserved=1`; none of their `sys*` handler symbols is linked.
- Disposable KVM QEMU PASS (`build/qemu-o/sysretire-2/`, REPL `RETIRE-OK`):
  OBJ and legacy-adapter Pipe data/close; `ObjOpPipeInherit` + `RFPROC`
  child read `FORK-PIPE`; OSPAWN explicit list child read `SPAWN`; UART
  count/control/status/data through OBJ and adapters plus invalid-port and
  console-denial paths; KBD OBJ/legacy open denied while the REPL held the
  claim, non-KBD and guessed-Handle LED denied, global handle-0 LED allowed.
- Raw-call PASS: `sysraw(71..85,100)` each returned `Ebadarg`; no panic,
  bad-disk, flush or sync error.
- **NOT RUN**: real PS/2 scan-code reads or LED hardware observation outside
  QEMU, physical UART hardware, and multi-CPU stress. No USB work was added.

## QEMU xHCI root hub over the OBJ USB service (2026-11-05)

Task `3893a8e4003145529d58927baddb54c9` demonstrated the first real emulated
USB controller path. A disposable KVM QEMU boot added
`-device qemu-xhci,id=xhci -device usb-mouse,bus=xhci.0` to the canonical
`tools/qemu-o-boot.sh` invocation (the retained script still defaults to no
USB controller; the patched copy and logs are under `build/qemu-o/usb33/`,
which is not published). The same canonical artifacts were used:
`build/gcc/9pc64.elf` SHA-256
`ebc9e590e7894ab1df4840863f61e7cc27f61a31ecce88892874cc1f0bc5cde8` and
`build/gcc/micropython.elf` SHA-256
`0ff113c8bf3d05ca124bd453144a4fb5531e0aa5770f11ab43b5f9571d838ec1`.
Boot report `result=PASS`, no panic or bad-disk/sync error.

- QEMU emulated xHCI (`1b36:000d`) with a `usb-mouse`; QEMU monitor
  `info usb` reported `Device 0.0, Port 1, Speed 480 Mb/s`.
- Kernel probe logged `usbxhci: 0x1b36 0xd: port 7000004000 size 16384 irq 11`.
- `usbcore_init()` created one root hub: `obj_usb_list()` returned total 1,
  entry `idx=0 devno=1 epno=0 ttype=1(Tctl) mode=2(ORDWR) speed=2(Highspeed)
  state=1(Denabled) inuse=0 maxpkt=64`.
- The new OBJ endpoint path opened and closed the root hub endpoint both as
  `OBJ_USB_EP_CTL` and `OBJ_USB_EP_DATA` (7/7 checks PASS).
- Earlier 0-endpoint runs are explained by the QEMU configuration: the
  canonical script uses `-nodefaults` and attaches no USB controller, so
  `usbcore_reset()` found no HCI. No kernel defect blocked the root hub path.
- **NOT RUN**: full device enumeration/usbd, class or HID drivers, mass
  storage, data/control transfer, a kernel-side port-status readout (the OBJ
  ABI does not expose USB control transfer or data I/O), mixed SuperSpeed
  root hubs, and physical USB hardware. No canonical source, syscall or
  retained test tooling was changed.

## USB endpoint open under the OBJ ABI (2026-11-05)

Task `3b6774358ecd4b5687b392078a5c92eb` adds
`OBJINVOKE(ObjOpUsbEpOpen=21)` and `VkindUsbEp=10` without a new syscall or
Handle table. A `VkindUsb` service Handle carrying the explicit `Vcreate`
right can open one `(devno, epno, kind, mode)` endpoint; `rights==0` remains
the metadata-only `Vmeta|Vdelegate` default, and `Vcreate` is available only
to the trusted eve/bootstrap user at `OBJOPEN(ObjOpenUsb)` time. Metadata,
attenuated and `OBJOPEN(ObjOpenHandle)` copies cannot open endpoints, and
GRANT/OSPAWN cannot add `Vcreate`. Endpoint rights are derived from kind and
mode, not a caller mask. DATA uses `usbcore_dataopen()`; CTL only references
the endpoint. `OBJCLOSE`/exec/exit releases the endpoint exactly once (DATA:
dataclose then putep; CTL: putep). Data I/O, `USBIOCTL` and control transfer
remain unimplemented. `ObjOpUsbList` now accepts an `outlen` above 256
records and clamps it like the adapter, while still rejecting a non-multiple
capacity.

Fresh evidence on `kuha`:

- Canonical kernel PASS: 132/132 selected, 134/134 full closure, ABI PASS,
  link PASS, 0 undefined symbols (`build/gcc/9pc64.elf`, 1,155,160 bytes,
  SHA-256 `ebc9e590e7894ab1df4840863f61e7cc27f61a31ecce88892874cc1f0bc5cde8`).
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols
  (`build/gcc/micropython.elf`, 533,336 bytes, SHA-256
  `0ff113c8bf3d05ca124bd453144a4fb5531e0aa5770f11ab43b5f9571d838ec1`).
- Targeted KVM QEMU PASS (`build/qemu-o/task-usbep-1/`): 32/32 assertions.
  Default metadata rights, metadata/attenuated/copy `Vcreate` denial,
  no-amplification through `OBJOPEN(ObjOpenHandle)`/`GRANT`, an eve
  `Vcreate` Handle reaching endpoint lookup (then `Ebadarg` because the
  boot had zero endpoints), invalid kind/mode/devno/epno denial, direct
  `ObjOpUsbList` capacities of 1/256/300 records, non-multiple capacity
  rejection, legacy USB list/epopen compatibility, and double close. No
  panic, bad-disk or sync error; `result=PASS`.
- Successful endpoint open, a live endpoint Handle's derived rights, DATA
  `usbcore_dataclose()`/`putep` release and the CTL reference path are
  **NOT RUN**: the environment has no emulated or physical USB endpoint.
  Non-eve `Vcreate` denial and multi-CPU/allocation-fault campaigns are also
  **NOT RUN**; no test harness or new Handle table was added.

The self-test printed `USBEP SELFTEST PASS 32 0` and the QEMU report
recorded `result=PASS`. Source publication follows the binding workflow.

## Pipe lock/lifecycle and KBD claim-path fix (2026-11-05)

Task `1435242a333c41c8a2aac7480abb23dc` makes a bounded locking fix in
`kernel/core/sysvobj.c`; ABI, syscall numbers, object types, rights masks,
PipePeer once-only semantics, explicit `RFPROC`/`OSPAWN` inheritance and KBD
exclusivity are unchanged.

- `vobjpipeprocfork()` no longer calls `vhandalloc()` (and therefore
  `smalloc()`/`resrcwait()`) under `from->lk`. It pins the eligible source
  `Vhand`s, snapshots their one-shot marks, allocates and publishes the copies
  without the table lock, and only then clears the marks and drops the pins.
- `vobjpipepeer()` allocates its `Vhand` before taking `vp->lk`; the peer check
  and the non-sleeping `vinstall()` are atomic under that lock. A full handle
  table now fails cleanly and leaves the pending peer intact, so a later retry
  after freeing a slot still succeeds.
- `vobjkbdopen()` allocates the object, the process handle table and the
  `Vhand` before claiming the exclusive scan-code controller. `vk->held` is set
  only after a successful claim, and the claim is held only across the
  non-sleeping `vinstall()`; all failure paths release it exactly once through
  `vobjput()`. The previous code was already leak-free but allocated while
  holding the claim.
- A shared `vhandinit()` helper centralizes `Vhand` initialization.

Fresh evidence on `kuha` (disposable KVM QEMU):

- Canonical kernel PASS: 132/132 selected, 134/134 closure, ABI PASS, link
  PASS, 0 undefined symbols (`build/gcc/9pc64.elf`, 1,154,936 bytes, SHA-256
  `cbed0d32a192e0ac32e81eac19a759f29134113f7e19798a3f93de7aff5985e3`).
  MicroPython PASS: 246 units, 0 FAIL (`build/gcc/micropython.elf`, SHA-256
  `bf723c8e672e556b90ffbbe2f74fbee821ed40cfcd29abf2e2889a8b9d306f6d`,
  unchanged).
- Targeted QEMU PASS (`build/qemu-o/task-pipe-fix/`): OBJ Pipe data in both
  directions, attenuation and grant denial; peer/double-peer denial;
  stale/double-close denial and read on a closed handle; legacy plus
  OBJ-to-legacy cross-route data; `RFPROC` inherited endpoint read and a second
  fork receiving no handle after the one-shot mark was consumed; `OSPAWN`
  explicit Pipe list; full handle table rejecting `PIPEPEER` followed by a
  successful retry and read after a slot was freed; KBD OBJ/legacy open and
  LED denial, including 200 repeated denied opens without a handle or claim
  leak observable through the next loop iteration.
- `RESULTGOOD` marker reached; the REPL returned to a prompt and the O volume
  committed normally (`result=PASS`, no panic).

Allocation-fault injection (no such harness), successful KBD
acquire/release (the REPL holds the single PS/2 claim), KBD-unavailable
(`*nokbd`) denial, real scan-code reads, multi-CPU stress and hot-unplug are
**NOT RUN**. No new source test harness was added.

## Native Pipe/UART/KBD object provider candidate (2026-11-05)

Task `4900c68069db4b41aab89e4ebca79b3a` moves native Pipe, UART and PS/2
keyboard behind the five-call object ABI without adding or renumbering a
syscall. `OBJCREATE(ObjKindPipe=3)` creates a blocking byte-stream pair and
installs end 0; `OBJINVOKE(ObjOpPipePeer=16)` installs the peer with the same
rights. `OBJOPEN(ObjOpenUart=10)` and `OBJOPEN(ObjOpenKbd=11)` install
`VkindUart` and `VkindKbd` Handles; `ObjOpUartCtl=17`,
`ObjOpUartStatus=18` and `ObjOpKbdLeds=19` are typed INVOKE operations.
Default rights, subsets only, stale/double-close `Ebadarg`, exec/exit
teardown, the 32-endpoint Pipe, 16-UART and 4-KBD per-process limits, and the
one-shot `PIPEINHERIT`/`RFORK` and explicit `OSPAWN` Pipe lists are unchanged
in meaning. `PIPENEW/READ/WRITE/CLOSE`, `PIPEINHERIT`, `UART*` and `KBD*`
remain compatibility routes over the same `Vhand` core; there is no second
handle table. Scan codes are still raw bytes, not decoded KeyEvents; Pipe is
still a byte stream, not an ObjectFIFO.

Fresh evidence on `kuha` (disposable KVM QEMU):

- Canonical kernel PASS (`--kernel`: 132/132 selected, 134/134 closure, ABI
  PASS, link PASS, no undefined symbols; `build/gcc/9pc64.elf`, 1,154,904
  bytes, SHA-256 `5e098a298b569c888772cfa4deec699dcb7a3e21f2e05435eba8639cdd216014`).
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols
  (`build/gcc/micropython.elf`, 528,488 bytes, SHA-256
  `bf723c8e672e556b90ffbbe2f74fbee821ed40cfcd29abf2e2889a8b9d306f6d`).
- Native Pipe/UART/KBD QEMU PASS (`build/qemu-o/task-pipe2-5/`): OBJ and
  legacy Pipe data in both directions, cross-route OBJ/legacy I/O, peer and
  end-1 PEER denial, attenuated write and `GRANT` denial, stale/double-close
  denial; UART data/control/status via OBJ, legacy and cross-route on the
  enumerated free port, with console-port, bad-port and wrong-Handle denials;
  KBD OBJ/legacy open and LED denials while the REPL holds the single PS/2
  claim.
- Cross-process Pipe regression PASS: an explicitly marked `RFPROC` child read
  the inherited endpoint (`FORK-PIPE`), and an OSPAWN child read only its
  explicit Pipe list (`SPAWN`).
- The test exposed a pre-existing UART fault: `uartenable()` passed string
  literals to in-place tokenizing `uartctl()`, writing read-only kernel
  memory. Opening the second UART reproduced a panic before the fix
  (`task-uart-only1`) and PASS after writing those four arguments into local
  buffers (`task-uart-only3`); no UART permission or data semantics changed.

For this round, actual keyboard scan-code reads/LED hardware, external-peer
UART receive and error injection, hot-unplug, unprivileged UART/KBD denial,
exec/exit observation in a live multi-process session, allocation-fault and
multi-CPU stress are **NOT RUN**. The previous UDP6, capability-fix and
object/GE/TCP evidence below remains retained and was not rerun.

## UDP6 object provider (2026-11-05; NET6 family superseded by phase 4 above)

Task `b91ee58f883e4f14a550e9b6157190e1` moved native UDP6 behind the five-call
object ABI without adding or renumbering a syscall. `OBJOPEN(ObjOpenUdp6=8)`
binds one `VkindUdp6` `Vhand`; `OBJINVOKE(SendTo=14/RecvFrom=15)` transfers
one complete 0–1232-byte datagram with sender address/port metadata and the
existing millisecond timeout. Default Handle rights are
`Vread|Vwrite|Vmeta|Vdelegate`; subsets are allowed, other bits are `Eperm`,
and no open/copy/grant path can amplify them. The former
`NET6ADDR/OPEN/RECVFROM/SENDTO/CLOSE` numbers 116–120 are now RESERVED by
phase 4 and have no dispatch entry; the object provider and the separate
`Net6tab` removal are unchanged. Address lookup is
`OBJINVOKE(ObjOpNet6Addr=24)` under the same non-transferable boot/permit gate.
Exec/exit release UDP6 Handles through `vobjprocexit`; only explicit OSPAWN
`Vdelegate` delegation can pass one on.

Fresh evidence on `kuha` (disposable KVM QEMU, local Ethernet-over-UDP peer):

- Canonical kernel PASS: 132/132 selected, 134/134 closure, ABI PASS, link
  PASS, 0 undefined symbols (`build/gcc/9pc64.elf`, 1,150,616 bytes, SHA-256
  `46b61c36ec19c04fb0b2d92f26b0a75230867df4956e17d5e23ee5af8672b0e8`).
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols
  (`build/gcc/micropython.elf`, 522,808 bytes, SHA-256
  `30fdaa64f85b3c5ee190905fc5c6e0ecfa726535e257b3401086add294d189e9`).
- UDP6 OBJ + NET6 peer PASS (`build/qemu-o/task-udp6-obj5/`): real IPv6 peer
  received `OBJ-PING` from guest port 4444 and `NET6-PING` from port 4445;
  the guest received both replies with the peer address and source port 40000.
  A read-only Handle denied send with `Eperm`, `GRANT` amplification denied
  with `Eperm`, and a second close returned `Ebadarg`.
- UDP6 boundary PASS (`build/qemu-o/task-udp6-big/`): 1232-byte send and
  receive round trip succeeded; 1233 bytes was rejected by the userland
  adapter. The receive-buffer-too-small path was not exposed by the
  MicroPython binding and is implemented but **NOT RUN**.
- The earlier capability-fix and object/GE/TCP evidence below remains the
  retained previous status; it was not rerun in this round.

For this round, unprivileged spawn denial of UDP6 open, OSPAWN UDP6 handoff,
observed exec/exit endpoint release, the raw receive-too-small path, multi-CPU
and allocation-fault campaigns are **NOT RUN**; no broad test tree was added.
Source publication follows the binding workflow.

## Capability-fix candidate (2026-11-05)

Reviewed and applied the user-supplied capability-fix diff (SHA-256
`78886b5f019c82db6fe59b9a3a0c41e934706b6af1000fed20c59e80ccfcd68f`, task
`885009021eb2401398cb6923ac3c59c0`) on top of canonical snapshot
`f3daebf4e0cb21ef1ca3e2c05589232f4f9c005738da0b41c44cf0cc2db06838`
(841 files). The transported patch had one line-splitting artifact in
`vobjcreatege`; it was corrected only so the correct preimage applies. The
applied core changes are in `ge/storage/{ocontext.c,ocontext.h,sysgefs.c}`,
`kernel/core/{sysobj.c,sysvobj.c,vobjint.h}` and `micropython/p9space.c`.
No syscall was added; GE/O v1 bytes, SpaceID/OID and the `OBJ*` 136-140
numbers are unchanged.

The candidate was reviewed rather than accepted blindly. Additional defects
found and fixed in this round:

- FIFO destruction detached subscriptions without clearing `s->fifo`, leaving
  a dangling `Vfifo*` for a still-live subscription Handle; clearing now
  happens under the FIFO lock.
- `sysonmount` checked only the grant's own `revoked` flag, not its shared
  origin, so a revoked derived capability could still be mounted.
- `vobjcreatege` and `vobjopenbinding` called `vinstall(vgettab(1), ...)`
  without a nil check; both now fail with `Enomem` rather than dereference a
  nil table.
- `vobjbind` rejected role removal under `RFNOMNT`; removal is no longer
  treated as namespace extension.

Implemented behavior in this candidate:

- Derived `Ohand` capabilities pin their origin and serialize on its gate;
  revocation checks the origin, so derived capabilities share the revocation
  lineage. A `Vhand` rights mask remains an upper bound: later OPEN, BIND and
  grant cannot amplify it.
- OSPAWN and OPEN reject subscription Handles; OSPAWN delegation requires
  `Vdelegate`.
- ObjectFIFO send records the sender's rights ceiling; receive intersects it
  with the subscription. TEE, drop-oldest, revoke and queue bounds are
  preserved, and no allocation or fallible publication occurs after dequeue.
- `OBJINVOKE(CreatePersist)` reserves the returned Handle before the durable
  `ospacecreate` call and closes it afterwards; handle-table exhaustion
  therefore does not allocate a durable OID.
- OBJBIND and the native O view share one pgrp/OContext entry. COPY and SHARE
  replace the role at one linearization point, EXCLUSIVE rejects an existing
  role, and removal clears the entry. A GE binding stores both the attenuated
  `Ohand` and the `Vhand`.

Fresh evidence (disposable QEMU runs, KVM on `kuha`, 4 vCPU unless noted):

- Canonical kernel build PASS: 132/132 selected, 134/134 closure, link PASS,
  0 undefined symbols (`build/gcc/9pc64.elf`,
  SHA-256 `caf627e99a44272d2a9a72084cd9dbe6a6627b01154bb619679104189b771fbf`).
  MicroPython build PASS: 246 units, 0 FAIL, no undefined symbols
  (`build/gcc/micropython.elf`,
  SHA-256 `641cf791fb98c9e8dccf075ed761086978744882522818b6723f8a35e16caaa7`).
- Syscall table confirmed: 135 named, 87 active, 48 reserved; `OBJ*` 136-140
  remain active.
- OBJ self-test 35/35 PASS (`build/qemu-o/task-cap-selftest/`): create/read,
  attenuation, bind/unbind, TEE, envelope, drop-oldest, revocation,
  negative operations.
- Durable creation 1/1 PASS (`build/qemu-o/task-cap-a1/`): 48 consecutive
  `obj_create_ge` operations produced strictly sequential OIDs.
- Handle-cap and rights 12/12 PASS (`build/qemu-o/task-cap-a2/`): the 32-slot
  Handle table rejects the 33rd live Handle, a failed durable create allocates
  no OID, and read-only, metadata-only, delegation, amplification denial,
  COPY, EXCLUSIVE, SHARE and REMOVE behave as specified.
- Namespace and OSPAWN 5/5 PASS (`build/qemu-o/task-cap-b/`): COPY and SHARE
  child views read the same persistent object, and a two-process FIFO
  capability handoff delivered the object and envelope.
- Two-boot GE/O persistence PASS (`build/qemu-o/task-cap-persist/`, default
  1 vCPU): boot 1 committed version 1; boot 2 restored version 1 and committed
  version 2 from the same O partition.
- IPv6 HTTP single-process PASS (`build/qemu-o/task-tcp1/`) and two-process
  TCP capability handoff through ObjectFIFO PASS (`build/qemu-o/task-tcp2/`),
  both against the local UDP-encapsulated Ethernet peer.

The earlier claimed Python RFORK worker timeout and sequential same-process
TCP stall did **not** reproduce in either TCP run; their root cause remains
unconfirmed, and test sequencing/host peer timing is plausible but not proven.
**NOT RUN:** allocation fault injection beyond the deterministic handle-table
exhaustion above, long fuzz, external IPv6 beyond the local peer, multi-CPU
stress beyond the 4-vCPU boots, physical hardware/disk writes, USB migration
and unrelated syscall reduction. Source publication follows the binding
workflow; the published `snapshot_id` is reported by `kernel_status` after
activation and is not asserted locally.

## Source publication pipeline (2026-11-04)

The binding development workflow is documented in
[DEVELOPMENT-WORKFLOW](DEVELOPMENT-WORKFLOW.md). Every task round publishes the
complete canonical source tree to `plan2001.com`, including failed or
incomplete rounds; publication is not gated on build or test success. The
implementation on `kuha` is `tools/host/mcp-publish.py` (SSH staging, remote
byte-for-byte verification, journaled and recoverable activation, session
history, previous-snapshot retention, idempotent re-verification, history-only
handling of superseded drafts and pending retry) plus the root-side helper
`tools/host/mcp-publish-remote.py` and the `EXIT`-trap wrapper
`tools/host/session-publish.sh`.

The read-only MCP source service at `https://plan2001.com/mcp` exposes
`kernel_status`, `kernel_pop`, `kernel_history`, `kernel_list`,
`kernel_manifest`, `kernel_read`, `kernel_search` and
`kernel_archive_chunk`. `kernel_status` additionally reports
`activation_consistent` and any `pending_activation` journal without changing
existing fields. `kernel_history` lists every published session, including
`result=failed` / `incomplete`, with its `snapshot_id` and hashes. The previous
complete snapshot is never deleted or replaced as the only copy.

`latest` means the real newest `kuha` source snapshot, not a forced older
"known-good" version. A candidate kernel status below is independent of source
publication: the tree may be a reviewable kernel candidate and still be the
published source of record.

**Automatic OpenCode worker hook:** the canonical tree carries
`.opencode/plugins/plan2001-worker-publish.js` (implementation
`tools/host/opencode/plan2001-worker-publish.js`). An OpenCode process started
in this worktree loads it automatically; optional global installation is
`tools/host/opencode/install.sh`. The hook observes `task_claim` and
`task_report`, publishes after a report, and also publishes a claimed but
unreported task at `/new`, `session.idle`, `session.error` and
`session.deleted`. It keeps machine-local state in
`build/mcp-publish/opencode-worker-state.json`. The manual commands remain
valid fallbacks.

### Publication verification (2026-11-04)

Evidence from the live Ubuntu host `plan2001.com` and the read-only MCP source
service:

- Journaled activation interruption after `CURRENT` was written but before the
  session record: `CURRENT` pointed at the new snapshot, `.activation.json`
  remained, and the client marked the attempt `activated-unverified` instead
  of claiming `CURRENT` was unchanged. `--recover-only` completed the journal,
  appended exactly one session (`f8c62786...`) and cleared the journal.
- Deliberately broken MCP post-verification after a real activation: the
  attempt was marked `activated-unverified`; recovery re-verified the same
  `session_id` (`828d4fea...`) without appending a second session. Session
  count was unchanged (7 before, 7 after).
- Session deduplication by `session_id` passed; a repeated recovery of an
  already-recorded activation did not create a new history row.
- Local transaction tests passed: complete activation, idempotent no-journal
  reconcile, current-already-newer journal completion, superseded draft kept in
  history with `activated=false`, and session dedupe.
- Version ordering passed live: an older pending snapshot
  `1fa012fd...91af` was recovered after newer snapshot
  `f0ab50e8...c898` was active. `CURRENT` stayed on `f0ab50e8...c898`, the old
  draft was stored with `activated=false` and
  `superseded_by=f0ab50e8...c898`, and it remained readable.
- Automatic OpenCode worker cycles, using the project plugin and the real MCP
  queue: a completed `task_report` was published without a manual command
  (tasks `84c02dc1...628f` and `43a9f59b...9afd`, snapshot
  `f67cd6a7...12e6`), and a failed `task_report` was published as
  `result=failed` (task `b0659c17...25bb`, same snapshot, session
  `8d6c06f8...f2e9`).
- Plugin event tests passed: `/new` (`session.created`), `session.error`,
  `session.idle`/`session.deleted`, and successful `task_report`. Each
  published exactly once and did not duplicate an already published task.
- Full-archive MCP reconstruction passed for snapshot `10c8f9a4...bd995`
  (session `f1375b7d...1383`, 841 files, deterministic archive `1820...`
  bytes). `kernel_status` reported `activation_consistent=true` and
  `pending_activation=null`.
- Previous-version readability passed: snapshot `f67cd6a7...12e6` was read
  through `kernel_manifest` (841 files) and `kernel_read`
  (`docs/STATUS.md`, SHA-256 prefix `d39007ca...`). The older
  `eed6f23e...0ad5` also remained readable with its own `snapshot_id`.
- Broad kernel regression testing remains **NOT RUN by design** for this
  publication-only task. The final publication after this STATUS update is the
  active snapshot reported by MCP; its session record is the authoritative
  record of the final full-archive verification.

## Universal five-operation object ABI: retirement and GE integration (2026-11-04)

The canonical object kernel boundary is exactly `OBJCREATE=136`,
`OBJOPEN=137`, `OBJCLOSE=138`, `OBJBIND=139`, `OBJINVOKE=140`
(`runtime/abi/obj.h`, `kernel/core/sysobj.c`). A single versioned `ObjCall`
router validates Handle rights, operation, direction, size and address, then
calls the shared RAM/FIFO/TCP/GE core. `BIND` installs a bounded role for
visibility only; GE-backed objects also mirror the binding into the native O
view, so `OLOOKUP` and NS copy/share/revocation apply. A failed native mirror
now rolls back the generic entry, so the two views cannot diverge.

The `VOBJ*` 121-135 and `TCP6*` 108-113 families are **retired**: the numbers
stay reserved with `nil` dispatch entries and their `sys*` wrappers are gone;
MicroPython keeps its historical Python method names as userland adapters
over OBJ* only. The always-failing pathname/FS and legacy FD numbers are
reserved the same way (48 numbers total), and a raw call returns `Ebadarg`
instead of reaching any removed implementation. No number was renumbered or
reused; GE/O v1 bytes and OIDs are unchanged.

Persistent GE/O is reachable through the same five calls:
`OBJOPEN(ObjOpenSpace)` applies the existing bootstrap/volume authorization
and returns a `VkindSpace` Handle; `OBJCREATE(space=<Space Handle>,
flags=ObjKindGe, ...)` explicitly allocates and commits a persistent OID;
`OBJINVOKE(CreatePersist)` allocates through a Space Handle;
`OBJOPEN(ObjOpenGeSpace, {space,oid})` reopens an existing OID;
`OBJINVOKE(Read/Write)` uses the existing `osreadhand`/`oswritehand` gate; and
`OBJINVOKE(Commit)` commits pending writes. Volatile byte/FIFO identity is
never promoted or cloned. `OVOLOPEN`/`OSPACE*`/`OEXECUTE`, the native
namespace family and `OQUERY` remain because they are still the tested
volume/bootstrap/namespace/relation implementation.

Fresh evidence (all disposable QEMU/TCG runs, regular files under `build/`):

- Kernel build PASS (132/132 selected, 134/134 closure, link PASS, 0
  undefined symbols; `build/gcc/9pc64.elf`); MicroPython build PASS (246
  units, 0 FAIL, no undefined symbols; `build/gcc/micropython.elf`).
- Static retirement check PASS: 48 reserved numbers, every `systab` entry
  `nil`, every `sysreserved` bit set, and none of the 48 removed `sys*`
  wrapper symbols present (`build/qemu-o/abi-static/check.txt`).
- Object ABI self-test **11/0 PASS** (`build/qemu-o/abi-selftest-final8/`):
  byte create/read, size query through the compatibility adapter, Handle
  attenuation and write denial, bind/unbind, ObjectFIFO TEE, envelope,
  drop-oldest overflow, revocation, and a raw retired `CLOSE` call returning
  `Ebadarg` (not the old FD implementation).
- Two-boot OBJ-only GE persistence PASS
  (`build/qemu-o/ge-persist-final3/report.txt`): boot 1 used
  `OBJOPEN(ObjOpenSpace)`, `OBJCREATE(ObjKindGe)`, `OBJINVOKE(Write)` and
  `OBJINVOKE(Commit)`; boot 2 used `OBJOPEN(ObjOpenSpace)` +
  `OBJOPEN(ObjOpenGeSpace)` and read `b"newabi"` from the same OID. The OID is
  not authority; the second boot acquires the Space Handle first.

**Not run this round:** IPv6 TCP HTTP, two-process TCP/ObjectFIFO handoff,
namespace COPY/SHARE/revocation campaigns, multi-CPU concurrency stress,
concurrent bind/unbind race campaign, exec/exit generic binding cleanup,
allocation-exhaustion injection, long connection-cycle/latency campaigns,
PID-reuse/guessed-Handle attacks, a read-only TCP TEE subscriber, optional
concurrent Proc C TEE subscriber, TCP endpoint-cap/collision/close-race
regression, UDP6 provider migration or UDP6 peer smoke, external IPv6 beyond
the local UDP-encapsulated peer, and physical hardware/disk writes. The
canonical tree retains no companion TCP peer or multi-process harness, and
adding a broad test tree is outside the sanctioned QEMU policy. Same-identity
volatile-to-GE promotion remains blocked. The tree remains a reviewable
candidate, not a declaration of new HEAD.

See [VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md),
[OBJECT-MODEL](OBJECT-MODEL.md), [KERNEL-API-ABI](KERNEL-API-ABI.md) and
[TEST-RESULTS.json](TEST-RESULTS.json).

## Volatile objects and ObjectFIFO candidate (2026-11-03, historical)

The VOBJ* numbers described in this section are reserved and have no dispatch
entry in the current tree; this section records the earlier candidate, not
the current ABI.

The current tree adds a RAM-only scope-qualified object identity
`(incarnation, space, oid)`, rights-bearing process-local Handles, and a
general message-framed ObjectFIFO with independent TEE subscriptions. The
implemented syscalls are 121-131 (`VOBJ*`); GE/O v1 bytes and persistent
objects are unchanged. A QEMU/REPL self-test passed 20/20 checks (create,
read, zero-length, scope tag, fanout, envelope, identity, attenuation, GE/O
snapshot bridge, denied rights, stale Handle, lifetime, independent capacity,
drop-oldest overflow, dead subscriber, non-blocking receive, revocation and
double close). A real IPv6/NDP/TCP peer connected to the guest, an accepted
connection published an event through the ObjectFIFO, and the guest returned
`HTTP/1.1 200 OK`. A two-boot disposable test restored the existing Counter
and showed two different boot incarnations; oid 1 in the new incarnation did
not alias the old one. Same-identity volatile-to-GE promotion, multi-CPU
concurrency stress, allocation-exhaustion injection, external IPv6 and the
full TCP6 regression suite were **NOT RUN**. See
[VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md) and
[TEST-RESULTS.json](TEST-RESULTS.json).

The follow-on continuation adds a unified `VOBJIO/VOBJLIST/VOBJOPEN/VOBJACCEPT`
family (132-135), TCP6 listener/connection objects, GE capability objects,
explicit OSPAWN flags=2 Handle delegation, and compatibility shims over the
same TCP core. A two-process QEMU test passed: worker A (pid 17) accepted a
real IPv6 connection, sent the GE object and connection object through an
ObjectFIFO, and exited; parent B received both, read the persistent GE bytes,
read the HTTP request and returned `HTTP/1.1 200 OK`, confirmed by the host
peer. The legacy TCP6 single-process test and the 20-check FIFO self-test
remain PASS. Same-identity volatile-to-GE promotion, native NS binding of
these objects, generic UDP6, multi-CPU stress, allocation exhaustion, and the
full TCP6 regression suite remain blocked or **NOT RUN**. See
[VOLATILE-OBJECT-OBJECTFIFO](VOLATILE-OBJECT-OBJECTFIFO.md).


## KUHA kernel-only hardware boot candidate (2026-11-02)

The kernel now has an explicit **kernel-only boot mode**: absent `obootexec`
opens the GE/O volume by `PARTUUID=` read-only in kernel context, prints
`PLAN2001 KERNEL READY: kernel-only, no external executable`, and holds a
stable scheduler idle state. MicroPython is selected only by the explicit
selector `obootexec=0x4f45584543000003`; `obootexec=0` is rejected. The
installer's default `plan9.ini` is kernel-only and omits `obootexec` and
`obootrepl`; the explicit MicroPython reinstall remains available.

On `kuha` the kernel-only candidate was built (`build/gcc/9pc64.elf`
`06549dd0...`, stamp `2026-11-02 08:57 UTC`; loader `b8cfa2c1...`), passed a
kernel-only QEMU/TCG boot, was installed and verified, and was then physically
booted **twice** by the operator: both boots reached `PLAN2001 KERNEL READY`,
opened p5 read-only by PARTUUID, had no panic and recovered to Debian; p5
stayed unchanged (`N = 19`). After that PASS the operator requested the normal
MicroPython REPL boot. The ESP was reinstalled with the explicit selector
(`obootexec=0x4f45584543000003`, `obootrepl=1`); the exact p5 MicroPython
object reached `>>>` in QEMU, but the physical MPY boot is not yet observed.
See `build/hw-kuha-20261102T083935Z/KUHA-HW-BOOT-REPORT.md`.

## Weekend UJ1 kernel integration candidate (2026-11-02)

The kernel-only candidate from the `weekend.zip` UJ1 snapshot was integrated on
top of published `a5a3532`. The candidate adds native TCP6 and process TCP
endpoints, ESP/AES-GCM with the private control Pipe, NS1–NS4 namespace and
R1 relation paths, and the build-manifest/ABI entries 101–113. The canonical
tree keeps the userspace IKE, TinyCC, UJ1 and test harnesses in the separate
companion bundle `Plan2001-O-weekend-tests/`, not as kernel deliverables.

Fresh host builds in this tree: kernel and MicroPython with GCC 14, EFI loader
with Clang 19/LLD, formatter and mpy-cross. Focused QEMU/TCG checks passed for
the canonical boot, two-boot GE/O persistence, the NS-CBOR/N13/NS-COMPLETE
suites, the TinyCC Space 2 test and the UJ1 two-boot user journey. One GE R1
rerun passed; an earlier run failed because the REPL-input driver lost the
leading `ge` characters, not because of a kernel assertion. The historical
weekend integration report is not present in the published tree,
so its exact commands, hashes and NOT RUN/blocked items cannot be linked from
here.

## Current state

Plan2001-O is an experimental operating system for the amd64 architecture.
amd64 is the only supported and intended target. The source tree is the
compact N11.7 layout (see
[REPOSITORY-LAYOUT](REPOSITORY-LAYOUT.md)); the former 9front `sys/src`
userland, the formatter VM/Ken C bootstrap, the GEFS FS/9P server units in
`ge/format`, and the unused legacy `runtime` sources have been removed. N11.7
removed only source-proven dead legacy code; see the N11.7 section below.
The working system is a separate
**GCC 14 amd64 kernel**, a **Clang 19/LLD PE32+ EFI
loader**, an independently built **GCC 14 MicroPython ELF** stored in GE/O at
OID `0x4f45584543000003`, and a **Linux GCC GE/O formatter + mpy-cross** for
image preparation. The canonical `oboot=server` boot
opens the native GE/O volume on partition 5 by PARTUUID, calls
`OVOLOPEN`/`OSPACEOPEN`/`OEXECUTE`, restores the `init` Space, runs
`init.run()`, commits and enters the MicroPython REPL. The kernel embeds no
interpreter and does not run `oinit`.

On the physical Dell Precision 7520, the **pre-cleanup binary** reached an
interactive `>>>` REPL with PS/2 keyboard, `dir()`, `import space` and
`space.info()`, and made real GE/O commits (init version advanced to 5, O
version to 6). The **N10.1 GCC kernel / Clang UEFI loader** was then booted
from the single `Plan2001-O` GRUB entry and also reached an interactive
MicroPython REPL (owner-observed). **N11.HW1 installed** the then-current
kernel/loader/MicroPython on physical p5 and the ESP; the owner subsequently
reported three physical boots with a working `>>>` REPL and a REPL variable
that survived across them. That is an **owner-reported hardware PASS, not an
agent-verified log**, and it applies to the **pre-N11.7 installed
configuration**. N11.7 itself was built and booted only in QEMU. N11.HW2 built
the main `f116b82` kernel, loader and fresh MicroPython on `kuha`, reamed the
verified experimental p5, and installed the matching ESP state with readback
verification; its physical boot was never observed. **N11.HW3** built the
then-current main (`33fd190`) timestamped kernel/loader/MicroPython, verified
the existing p5 read-only as byte-identical and reused it without reaming, and
installed/verified the timestamped ESP/GRUB state; no agent-observed N11.HW3
hardware boot is recorded. **N11.HW4** has now built the accepted current main
(`fd2441c`) timestamped kernel/loader/MicroPython on `kuha` (kernel stamp
`2026-10-29 15:35 UTC`; no kernel-relevant source changed since N11.HW3),
verified the existing p5 read-only as compatible and reused it without
reaming, and installed/verified the matching timestamped ESP/GRUB state;
**physical boot of the N11.HW4 timestamped kernel has not yet been observed
and remains the operator's action**.

## The `plan9.ini` contract

`plan9.ini` must end in LF (`0x0a`), including the last line. The EFI loader
returns a distinct error for a nonempty final line without LF, prints exactly
`plan9.ini: missing final newline`, and stops; it does not silently drop the
last setting or fall back to another CPU count/profile. `tools/gcc-o-install.sh`
writes the complete file directly (no shell command substitution) and verifies
the staged and installed bytes with exact-file comparison, SHA-256, size and an
explicit final-byte check.

Canonical content:

```ini
bootfile=/EFI/plan2001-gcc/9pc64.elf
oboot=server
obootvol=PARTUUID=<live-p5-partuuid>
obootrepl=1
*ncpu=1
```

## Current verification

- N12 delta after N13 (`OPC-20261030-N12-DELTA`): **QEMU/TCG PASS; hardware
  and Pipe/UDP6 regression proof NOT RUN**. The retained refinements include: `active.sched` scheduler-readiness gating for
  exact CPUs, installer `--ncpu` 1..128 / `--obootcpu` 0..127 enforcement,
  and corrected boot-selector documentation. The N13 `OSPAWN` ABI, grants,
  views, budgets, AP trampoline and argv contract are unchanged. Kernel
  124/124 selected and 126/126 full closure, MPY 240/240 units, EFI, ABI and
  Whitespace checks passed. Six bounded 4-vCPU TCG boots reached
  `firstuser pid=3 cpu=2 requested=2`; an `OSPAWN` child reached
  `firstuser pid=10 cpu=3 requested=3`, and an offline `obootcpu=2` on one
  vCPU panicked instead of falling back to CPU 0. The two-boot persistence
  check restored `n13saved`, `n13f` and an alias (`N13-RESTORED 314159 3
  True`). A REPL smoke rejected an unknown OID and an undersized child
  budget, then spawned with an explicit 32 MiB budget on CPU 3. Host-only
  installer checks rejected `--ncpu` 0/129/4096, `--obootcpu` 128 and
  `--obootcpu >= --ncpu`; no ESP, GRUB or p5 write was made. Full N13
  object-view/Pipe/UDP6 regression proof and physical hardware are NOT RUN.
- N13 integrated object views, process runtime, budgets and native UDP6
  (`CDX-20261030-N13-INTEGRATION`): **QEMU/TCG PASS; hardware NOT RUN**.
  The integration branch combines the pinned source heads from PRs #14, #16
  and #15 while retaining their provenance and leaving those PRs open. The
  shared OEXECUTE/OSPAWN grant check, explicit EMPTY/COPY/SHARE spawn views,
  selected Pipe/environment/stdout, rfork-safe per-process virtual mapping
  ceilings and default-denied native UDP6 permit are integrated. Four-vCPU
  native proof reached `INTEGRATED CORE PASS`; the two-boot persistence proof
  restored a variable, function and alias identity. Canonical kernel,
  MicroPython and EFI builds, exact syscall slots/layout, negative boot
  selectors and simulated installer LF validation passed. See
  [N13-INTEGRATION](N13-INTEGRATION.md) for exact commands, evidence and
  limitations. Full container isolation, aggregate-family/resident memory
  quotas, persisted native views, TCP6, KVM and physical hardware are not
  implemented or verified.
- QEMU-O host validation (OPC-20261029-QEMU-O1): **PASS** — the two
  sanctioned scripts build or reuse the canonical artifacts and use
  disposable regular-file GPT disks. On QEMU 10.0.13/OVMF 4M/KVM with the
  default virtio controller, `tools/qemu-o-boot.sh` reached the loader,
  `OEXECUTE`, `init.run()`, GE/O commit and the `>>>` REPL; `oinspect`
  confirmed the exact MicroPython object. `tools/qemu-o-persist.sh` booted
  the same disk twice: boot 1 committed `init` version 1 (Counter n 1), boot
  2 restored version 1, ran `init.run() -> 2`, committed version 2 and
  reported Counter n 2; the O Space version advanced 1→2→3 and the
  MicroPython object bytes were unchanged. A REPL-only two-boot check also
  restored `x = 12345` through `print(x)` → `12345`. TCG fallback booted
  successfully; the bounded `--interactive` GTK path also passed. The AHCI
  diagnostic option reproduced the earlier `sdE0: bad disk` behavior
  intermittently (one complete boot and two failures in
  three runs) with the same GPT layout, generated-PARTUUID handling and
  identical O image bytes that pass reliably through virtio, localizing it to
  the AHCI/QEMU timing/attachment path rather than GE/O. Evidence:
  `build/qemu-o/{boot,persist,repl-check,repl-persist}/report.txt` and their
  serial/boot logs (including the canonical build logs under
  `build/qemu-o/boot/`). This records the earlier QEMU-O1 baseline; the newer
  integrated branch function/alias persistence result is listed above.
- N11.HW4 current-main physical install refresh: **PASS (built, p5
  verified/reused, ESP/GRUB installed and verified; physical boot NOT yet
  observed)** — on `kuha` from the verified N11.HW4 source snapshot; since N11.HW3 the
  diff is limited to agent instructions and documentation; no
  kernel/include/GE/runtime/MicroPython/host-tool/build-script source changed. A fresh `-j 8` canonical build produced `build/gcc/9pc64.elf`
  (955,232 bytes, SHA-256
  `85f90c23bd1c4c74ed8664591e4cd0daac395be331f7619b5d19a9536948fc74`,
  ELF64 x86-64 `ET_EXEC`, statically linked; 122/122 selected, 124/124 full
  closure, ABI PASS, link PASS) with exactly one embedded stamp
  `PLAN2001-O-KERNEL-BUILD:2026-10-29 15:35 UTC`,
  `build/gcc/bootx64.efi` (22,016 bytes, SHA-256
  `6e6aa89d41bf727b2f3d93a309cd852365bc9f8fc88c0e388df99749784ade34`,
  PE32+ EFI application) and `build/gcc/micropython.elf` (458,648 bytes,
  SHA-256 `0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8`,
  240 units, 0 FAIL, byte-identical to the ELF on p5). Live identity was
  re-derived: `/dev/nvme0n1p5`, partition 5, live PARTUUID
  `a91d7405-f2d0-4269-82b4-f528ac65b185`, 16,018,046,976 bytes, unmounted, no
  holders; ESP `/dev/nvme0n1p1` at `/boot/efi` (vfat, FAT UUID `ADC3-AA1E`).
  Read-only `oinspect` confirmed valid primary and backup O v1 superblocks
  (blksz 16384, narena 8), Space `N = 17`, 970 tree records, and OID
  `0x4f45584543000003` version 1 holding the same 458,648-byte ELF with
  matching SHA-256; the environment object declares runtime
  `mp-1.27.0-mpy6.3`, so p5 was **reused, not reamed**. The recorded N11.HW3
  runbook value was `space.N = 12`; the current `N = 17` therefore reflects
  some intervening write that predates this read-only pass (N11.HW4 wrote no
  p5 bytes). The 125-byte LF-terminated `plan9.ini` (SHA-256
  `6b16494b3b0d90b7b195fa235905225b329085b44f1b3ce84f377c9c7bdca9fa`, final
  byte `0a`) carries `bootfile=/EFI/plan2001-gcc/9pc64.elf`, `oboot=server`,
  `obootvol=PARTUUID=a91d7405-f2d0-4269-82b4-f528ac65b185`, `obootrepl=1` and
  `*ncpu=1`; the hardened installer `--plan` → `--install` → `--verify` all
  exited 0 with `verify=PASS`, matching installed kernel/loader/ini readback
  hashes, exactly one `Plan2001-O (2026-10-29 15:35 UTC)` entry (id
  `plan2001-o`, chainloader `/EFI/plan2001-gcc/BOOTX64.EFI`), Debian still
  `set default="0"`, non-Plan2001 entries unchanged and legacy 42/43
  non-executable. No QEMU boot was run (no kernel-relevant source changed and
  no build failed) and no physical reboot was performed; **the operator must
  select the timestamped entry and observe the physical boot — hardware boot
  is not claimed**. Evidence:
  `build/n11-hw4/{baseline.txt,kernel-build.log,mpy-build.log,efi-build.log,p5-oinspect.log,install-plan.log,install.log,verify.log}`
  and `build/gcc/o-install/verify.txt` (`verify=PASS`).
- N11.HW3 current-main timestamped physical install: **PASS (built, p5
  verified/reused, installed/verified; physical boot NOT yet observed)** — on
  `kuha` at `main` `33fd190a8cfcef5b3d3219d01a3373dba3f97db7` (source-identical
  to N11.11 `4a486fb`), a fresh canonical build produced
  `build/gcc/9pc64.elf` (955,232 bytes, SHA-256
  `e463746cd117fef8ef24ef19449092ccc07f1c085cdf76f575ae75f505bdb7af`) with
  exactly one embedded stamp `2026-10-29 05:01 UTC`,
  `build/gcc/micropython.elf` (458,648 bytes, SHA-256
  `0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8`,
  byte-identical to N11.11/N11.HW2, 240 units, 0 FAIL, no undefined symbols)
  and `build/gcc/bootx64.efi` (22,016 bytes, SHA-256
  `cb0be50b68ac48695ca9ee320899a6ddeb31228d0d6d09aca723943da5619af2`, PE32+
  EFI application). Live identity was re-derived: `/dev/nvme0n1p5`, partition
  5, live PARTUUID `a91d7405-f2d0-4269-82b4-f528ac65b185`, 16,018,046,976
  bytes, unmounted, no holders; ESP `/dev/nvme0n1p1` at `/boot/efi` (vfat, FAT
  UUID `ADC3-AA1E`). Read-only `oinspect` confirmed both primary and backup
  superblocks valid (O v1), OID `0x4f45584543000003` = 458,648 bytes with the
  same SHA-256 as the new ELF, and runtime `mp-1.27.0-mpy6.3`, so p5 was
  **reused, not reamed**. The old ESP state was the untimestamped N11.HW2
  kernel (`b1a4c769…`), loader (`139e218d…`) and title `Plan2001-O`. The real
  hardened installer `--plan` → `--install` → `--verify` then passed against
  the same artifact set: installed kernel/loader/ini readback hashes match, the
  installed ELF stamp equals the GRUB title
  `Plan2001-O (2026-10-29 05:01 UTC)` with stable id `plan2001-o` and
  chainloader `/EFI/plan2001-gcc/BOOTX64.EFI`, exactly one Plan2001 entry,
  Debian still `set default="0"`, non-Plan2001 entries byte-identical, legacy
  42/43 scripts non-executable, 125-byte LF-terminated `plan9.ini`. Evidence
  logs: `build/n11-hw3/{baseline.txt,kernel-build.log,mpy-build.log,efi-build.log,install-plan.log,install.log,verify.log}`
  and `build/gcc/o-install/` (including `verify.txt` = `verify=PASS`). The
  operator must select the timestamped entry and observe the physical boot;
  no hardware boot is claimed.
- N11.HW2 current-main physical install: **PASS (installed/verified, physical
  boot pending)** — on `kuha` at `main` `f116b82` the fresh canonical build
  produced `build/gcc/9pc64.elf` (955,200 bytes, SHA-256
  `b1a4c76936d780a214c456ad2d97d62e98d57506de49f390b67a1b0d73745c8d`),
  `build/gcc/bootx64.efi` (22,016 bytes, SHA-256
  `139e218d4180f8d3c0bb72effed36ac456b1de167446957389767f7e0968dc46`) and
  `build/gcc/micropython.elf` (458,648 bytes, SHA-256
  `0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8`),
  with the formatter and `mpy-cross` 1.27.0/`.mpy` v6.3 rebuilt. The live
  target was independently confirmed as `/dev/nvme0n1p5` (partition 5,
  16,018,046,976 bytes, live PARTUUID
  `a91d7405-f2d0-4269-82b4-f528ac65b185`, unmounted, no holders); ESP is
  `/dev/nvme0n1p1` at `/boot/efi` (FAT UUID `ADC3-AA1E`). The fresh
  `build/hw-n11-8-5-p5.img` (exact 16,018,046,976 bytes, SHA-256
  `8a632f01dc5d04951fcbd18da6bc5310ee866877ae35bcdfb4587b5fdcab6900`,
  runtime `mp-1.27.0-mpy6.3`) passed `--image-check`, `--plan`, `--install`
  and `--verify`; full p5 readback SHA matched and `oinspect` confirmed O v1,
  primary+backup superblocks and the exact new MP ELF at OID
  `0x4f45584543000003`. ESP `--plan`/`--install`/`--verify` passed with
  matching kernel/loader hashes, a 125-byte LF-terminated `plan9.ini`, the
  live PARTUUID, exactly one `Plan2001-O` entry (id `plan2001-o`), inactive
  legacy 42/43 entries and Debian still `set default="0"`. The operator must
  select `Plan2001-O` and observe the physical boot; no physical boot is
  claimed here.
- Physical pre-cleanup hardware boot: **PASS** (REPL, keyboard, `dir()`,
  `import space`, `space.info()`, GE/O commits).
- N11.HW1 physical p5/ESP install: **PASS (installed)** — p5
  `/dev/nvme0n1p5` (live PARTUUID
  `a91d7405-f2d0-4269-82b4-f528ac65b185`, 16,018,046,976 bytes, unmounted, no
  holders) was reamed from `build/hw-n11-6-p5.img` (SHA-256
  `6020bd4c96d2d0d160d38215e9ea523144cb1665f41d2aaf059ed5eb6a94962c`);
  full readback SHA matched and read-only `oinspect` confirmed O v1,
  superblocks OK and the exact 458,816-byte `358a6d7f…` MP ELF. ESP
  `/boot/efi/EFI/plan2001-gcc/` was updated and `--verify` passed with one
  `Plan2001-O` entry and Debian still `set default="0"`.
- Owner-reported physical boot after N11.HW1: **PASS (owner-reported, not an
  agent-read log)** — three boots on the Dell Precision 7520 reached the O
  Space and the REPL, and a REPL-assigned variable survived across the boots.
  This validates the **pre-N11.7 installed configuration**, not the N11.7
  build.
- N11.7 source cleanup: **PASS** — removed the never-called `Mntcache` layer
  (`kernel/core/cache.c`, `cinit`/`copen`/`cread`/`cwrite`/`cupdate`/
  `ctrunc`/`cclunk`, `Chan.mcp`, `fscache`, `initcode`), reference-less
  `emptystr`, `eqqid`, `canmount`, `devpower` and `devconfig`, the dead
  pathname bodies of MicroPython's `mp_lexer_new_from_file`/`mp_import_stat`
  callbacks, seven orphan `runtime/libc/9sys` units and the unused
  `9p.h`/`auth.h`/`draw.h`/`mouse.h` headers. See the N11.7 section below.
- N11.7 GCC kernel build: **PASS** — 123/123 selected units, 125/125 full
  closure, ABI check PASS, link PASS, `build/gcc/9pc64.elf`, no `nm -u`
  undefined symbols.
- N11.7 GCC MicroPython build: **PASS** — 240 units, 0 FAIL,
  `build/gcc/micropython.elf`, 458,648 bytes, SHA-256
  `0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8`,
  68 manifest libc objects, no undefined symbols.
- N11.7 O image and QEMU boot: **PASS** — `build/n11-7-clean-129.img`,
  135,266,304 bytes (129 MiB), SHA-256
  `75ec32ef2868c97e34cc8f50364231247af0ebdc1c3ac7cd7e700fa2a3d42e32`;
  `oinspect` confirms OID `0x4f45584543000003` holds the exact 458,648-byte
  N11.7 MP ELF. QEMU `oboot=server` reached `OEXECUTE`, `init.run()`,
  `OSPACECOMMIT`, `PLAN2001-O: REPL READY` and `>>>`. This does **not** prove
  the N11.7 build on physical hardware; no p5/ESP write was made.
- N11.7D Dev-operation audit (historical): **PASS (no code removals in that
  pass)** — that earlier audit removed nothing. Its no-removal result is kept
  as history; N11.7D.2 below supersedes it. The owner's three hardware boots
  and persisted variable belong to the earlier installed pre-N11.7
  configuration, not to either Dev-cleanup pass.
- N11.7D.2 dead Dev-operation removal (amd64-only): **PASS** — removed the 17
  unreachable `dev.c` operations `devdir`, `devgen`, `devclone`, `devwalk`,
  `devstat`, `devdirread`, `devpermcheck`, `devopen`, `devattach`,
  `devcreate`, `devremove`, `devwstat`, `devbread`, `devbwrite`, `devreset`,
  `devinit` and `devshutdown`; `dev.c` went from 428 to 26 lines and from 19
  to 2 function definitions. Retained `mkqid` (called by `pipe/pipecore.c`)
  and `devno` (called by `pgrp.c`/`sysfile.c`), plus the active `Dev`
  dispatch used by `chan.c`, `sysfile.c`, `fault.c` and `cons.c`.
  `portfns.h` lost the 17 corresponding prototypes (plus the now-orphaned
  `newpath` prototype), the `Devgen devgen;` declaration; the `newpath`
  helper and its `PATHSLOP` constant were also removed from `chan.c`.
  `portdat.h` lost the `Dirtab` typedef/struct, the `Devgen` typedef and
  `DEVDOTDOT`. `Chan.dri`, `Dev`, `Chan`, `Qid`, `Path`,
  `Walkqid`, `Fgrp`, `Pgrp` and live layouts were preserved. `chan.c` lost
  the three empty `devtab` reset/init/shutdown loops while retaining
  `todinit()` and the `closeproc` worker. Kernel build and link are **PASS**
  (`build/gcc/9pc64.elf`, 982,408 bytes, SHA-256
  `be3993ccd3f034bf55fedc86580002b43cbd85ef89612ca8c4d341a1880f7169`) with
  no `nm -u` symbols. A fresh QEMU `oboot=server` boot reached `OEXECUTE`,
  `init: run()`, `OSPACECOMMIT`/`GE/O COMMIT SUCCESS`, `REPL READY` and `>>>`
  (log `build/n11-7d2-final-serial.log`, disposable image
  `build/n11-7d2-final.qemu.img`). No physical p5/ESP write was made.
- N11.8.1 legacy Chan/FD pipe-creation seam removal (amd64-only): **PASS** —
  removed the provably unreachable old FD-backed pipe island: `newfd2()` and
  `openmode()` from `sysfile.c`; `pipechanpair()`/private `pipeendpoint()`
  from `pipe/pipecore.c`; `newchan()` from `chan.c`; `mkqid()` from `dev.c`;
  and their four `portfns.h` prototypes. `syspipe()` keeps its ABI, argument
  decoding, `validaddr`/`evenaddr` ordering and `-1` output initialization,
  then raises `Ebadsharp` directly; the canonical amd64 observable result is
  unchanged. `devno()` and `pgrp.c::devmask()` remain. `Fgrp`/fd/`Chan`/
  `Path`/`Dev`, `newfd`/`sysdup`, `fdtochan`/`fdclose`, process exit/rfork/
  exec teardown and the native `PIPENEW`/`PIPEREAD`/`PIPEWRITE`/`PIPECLOSE`
  handle path were not changed. Kernel build/link **PASS** (123/123 selected,
  125/125 full closure, ABI PASS, 0 undefined symbols;
  `build/gcc/9pc64.elf`, 982,176 bytes, SHA-256
  `b76ed3db38100175d2f49d37580bced01e65fc67d3df2f7fe7fd060f5366509e`).
  One disposable QEMU `oboot=server` boot **PASS** — `OEXECUTE`,
  `init.run()`, `GE/O COMMIT SUCCESS`, `REPL READY`, `>>>`, no panic (log
  `build/n11-8-1/serial.log`, image `build/n11-8-1/disk.img`, GPT
  `build/n11-8-1/disk.partuuid`). No physical p5/ESP write was made, and
  MicroPython was not rebuilt (no MP source or required-ABI change).
- N11.8.2 legacy FD success-layer removal and Fgrp decoupling (amd64-only):
  **PASS** — `sysfile.c` lost 13 definitions (`newfd`, `fdtochan`,
  `fdclose`, `growfd`/`findfreefd`/`unlockfgrp`, Chan read/write/seek/stat
  dispatch and directory-stat serialization); `dup`/`close`/`read`/`pread`/
  `write`/`pwrite`/`seek`/`oseek`/`fstat`/`_fstat` keep their ABI, argument
  decoding, pre-FD pointer/alignment checks and canonical `Ebadfd`, while
  `PIPE` keeps `Ebadsharp` and `IOWRITE` is unchanged. `struct Fgrp` is now
  only `{ Ref; }` (`fd`/`flag`/`nfd`/`maxfd`/`exceed`/`DELTAFD` gone);
  `dupfgrp`/`closefgrp` retain presence/refcount semantics and allocation
  failure. Removed `forceclosefgrp`, `Proc.closingfgrp`, the `interrupted()`
  force-close call and the `execcommit` close-on-exec FD loop; `pprint`
  returns 0 in the canonical no-stderr state; `isdir`/`dirchanstat` and
  their prototypes are gone. The now-uncalled `cclose` was also removed;
  `Chan`/`Path`/`Dev`/`Image`, `ccloseq`/`chanfree`/`pathclose`/`chanpath`/
  `attachimage` remain for the compiled image/fault path. `RFORK` presence/inheritance and all native
  handle tables are unchanged. Kernel build/link **PASS** (123/123 selected,
  125/125 full closure, ABI PASS, 0 undefined symbols;
  `build/gcc/9pc64.elf`, 969,168 bytes, SHA-256
  `2a2b2529355fc4597077a2a141387164e01225777f505238a12bac744bf5d3ed`).
  One disposable QEMU `oboot=server` boot **PASS** — `OEXECUTE`,
  `init.run()`, `GE/O COMMIT SUCCESS`, `REPL READY`, `>>>`, no panic (log
  `build/n11-8-2/serial.log`, image `build/n11-8-2/disk.img`). MicroPython
  was not rebuilt; no physical p5/ESP write was made.
- N11.8.3 Fgrp retirement and dead Image/Chan island removal (amd64-only):
  **PASS** — the `Fgrp` type and `dupfgrp`/`closefgrp` API are gone.
  `Proc.fdgrp` is a `Ref *` legacy-RFORK presence token (`nil` = absent) with
  fallible `newfdgrp()` and `closefdgrp()`; `RFFDG`/`RFCFDG`, absent-group
  rejection, independent allocations, shared-pointer `incref`, `waserror`
  rollback and `pexit` release are unchanged. Removed the file-backed
  `Image -> Chan` island: `attachimage`, `newimage`, `putimage`, the image
  cache/reclaim functions, `data2txt`/`txt2data`, `fault.c::pio`/
  `faulterror`, `Segment.image`/`fstart`/`flen`, `Page.image`/`daddr`,
  `PGHASH`, the `Image` type and the unused `Image` allocation pool/
  `conf.nimage`. Removed the then-orphaned Chan lifecycle `ccloseq`,
  `chanfree`, `pathclose`, `chanclose`, `closeproc`/`clunkq`, `chanalloc`,
  `chanpath`, `eqchantdqid` and `chandevinit`; `chan.c` retains
  `incref`/`decref`, `kstrcpy`/`kstrdup`, `chandevreset`/`chandevshutdown`
  and `validname`/`validnamedup`. `Chan`/`Path` remain only as opaque
  pointer types for compiled SD/LED signatures, with no field reader. Native
  O ELF pages remain fully resident and COW/reclaim/wait/non-swap behavior
  is unchanged; a missing `SG_TEXT`/`SG_DATA` page is now an explicit
  fail-closed panic (unreachable in valid O execution). Kernel build/link
  **PASS** (123/123 selected, 125/125 full closure, ABI PASS, 0 undefined
  symbols; `build/gcc/9pc64.elf`, 959,664 bytes, SHA-256
  `f69c8dcd0c51a7aa1580a1c78800752674c3e914f3ca70ac5b34acf78f1c4338`).
  One disposable QEMU `oboot=server` boot **PASS** — `OEXECUTE`,
  `init.run()`, `GE/O COMMIT SUCCESS`, `REPL READY`, `>>>`, no panic (log
  `build/n11-8-3/serial.log`, image `build/n11-8-3/disk.img`). MicroPython
  was not rebuilt; no physical p5/ESP write was made. The QEMU smoke does
  not exercise `rfork` edge cases or memory-pressure behavior.
- N11.8.4 declaration contraction and safe resident-page fault (amd64-only):
  **PASS** — removed `struct Chan`, `struct Path`, the full legacy `struct
  Dev` callback table and `struct Walkqid`; `Path` and its typedef are gone,
  `Dev` is only `{ int dc; }` for `dev.c::devno()` and
  `sdcore.c::sdlegacydev()`, and `Chan`/`Walkqid` survive only as incomplete
  pointer types in compiled SD/LED/network signatures. Removed the unused
  channel-flag enum, the `CHANINVALID` sentinel and the empty
  `chan.c::chandevshutdown()` plus its `portfns.h` declaration and
  already-`panic`-unreachable `amd64/main.c` call. Retained `Qid` from
  `<fcall.h>`, `devtab[]`/`devno()`/`devmask()`, the `Pgrp` device mask,
  `chandevreset()`/`todinit()` ordering, `incref`/`decref`,
  `kstrcpy`/`kstrdup`, `isfrog` and `validname`/`validnamedup`. In
  `fault.c`, a missing resident `SG_TEXT`/`SG_DATA` page (reachable through
  user `SEGFREE`) now returns a distinct `Faultnopage`: `fault()` restores
  `psstate`/`insyscall` and either fails the syscall via `Eio` when an error
  label is present or returns to `faultnote`/the failed user fault, instead
  of panicking; memory-pressure `Faultretry` handling, lazy BSS/stack fill,
  resident COW, `PTERONLY` text mapping, `putmmu` flags, OEXECUTE residency
  and the non-swapping behavior are unchanged, with no pager or zero-fill.
  Kernel build/link **PASS** (123/123 selected, 125/125 full closure, ABI
  PASS, 0 undefined symbols; `build/gcc/9pc64.elf`, 959,624 bytes, SHA-256
  `87377864b6184a2bec94e2a395d5d0260f542d65e3bd1248f22ac9d8487b0bb5`).
  One disposable QEMU `oboot=server` boot **PASS** — `OEXECUTE`,
  `init.run() -> 2`, GE/O `COMMIT SUCCESS`, `REPL READY`, `>>>`, no panic
  (log `build/n11-8-4/serial.log`, image `build/n11-8-4/disk.img`).
  MicroPython was not rebuilt; no physical p5/ESP write was made. The QEMU
  smoke does not exercise malicious `SEGFREE`, `rfork` edge cases or
  memory-pressure behavior.
- N11.8.5 Dev registry and legacy driver-Qid removal (amd64-only): **PASS** —
  `kernel/amd64/pc64` has no `dev` rows; `tools/host/build-kernel.py::
  make_pc64()` emits no `Dev` extern or `Dev* devtab[]`, and the regenerated
  `build/gcc/gen/pc64.c` contains no legacy registry. `kernel/core/dev.c`
  (only `devno()`) is removed from `KERNEL_SOURCES` and the source tree;
  `portdat.h` no longer declares `Dev`/`devtab[]`/`Chan`/`Walkqid`, and
  `portfns.h` no longer declares `devno()`. `pgrp.c::devmask()` now preserves
  the canonical empty-Dev effects without `devno()`: non-inverting calls
  leave the mask untouched and `RFNOMNT` sets all 256 bits, with the same
  `Pgrp` lock, ownership and `rfork` error ordering. `sdcore.c` dropped
  `sdlegacydev()` and reports `native-only`; discovery, the exactly-once
  guard, `SDifc` operations and native block/flush/GE/O paths are unchanged.
  Removed the dead #S subfile metadata/callbacks (`sdaddfile`, `SDfile`,
  `SDrw`, `SDunit.efile`/`nefile`, NVMe `readsmart`, AHCI `ahciledr/w`,
  `ledr/ledw`) while retaining actual AHCI enclosure LED control and NVMe
  controller/`nvmerctl` code. Network identity no longer uses Qid:
  `Proto.qid` and `Fsproto()` QID writes are gone, `ipcore.c` no longer
  includes the now-dormant `ipdevqid.h`, and the unchanged numeric bound is
  `Maxconv = 4096` (legacy `Maskconv + 1`); protocol indices and
  conversation allocation are unchanged. `netif.h` lost the Qid-directory
  enum/`NETQID` macros and orphan `netif*` prototypes, and `ip.h` lost
  orphan `ifclog*`/`routewrite`/`chandial`. `Qid` remains only in
  `kernel/core/lib.h` for GEFS formatting (`ge/storage/dump.c`). Kernel
  build/link **PASS** (122/122 selected, 124/124 full closure, ABI PASS, 0
  undefined symbols; `build/gcc/9pc64.elf`, 955,200 bytes, SHA-256
  `b1a4c76936d780a214c456ad2d97d62e98d57506de49f390b67a1b0d73745c8d`). One
  disposable QEMU `oboot=server` boot **PASS** — `sd: controllers
  initialized (2) native-only`, `OEXECUTE`, `init.run()`, GE/O `COMMIT
  SUCCESS`, `REPL READY`, `>>>`, no panic (log `build/n11-8-5/serial.log`,
  image `build/n11-8-5/disk.img`). MicroPython was not rebuilt; no physical
  p5/ESP write was made. The QEMU smoke does not prove physical AHCI/NVMe
  SMART/LED behavior or IPv6 traffic.
- N11.9 GCC/amd64/IPv6-only conditional cleanup: **PASS** — removed
  provably constant preprocessor alternatives in the canonical compile
  closure: the `E7_IPV6_ONLY` V4 alternatives in
  `kernel/net/{arp,udp,ipifc,ipaux,icmp6,loopbackmedium}.c` (29 conditional
  sites → 0) and the `PLAN2001_GCC` 6c/lock/ABI alternatives in 17 kernel
  files plus `include/amd64/u.h`, `include/libc.h`, `include/a.out.h`,
  `ge/format/fns.h` and `ge/storage/gfns.h` (51 → 1, only the force-include
  shim's own `#ifndef PLAN2001_GCC` default). The two
  `PLAN2001_GCC_USER` `jmp_buf`/`setjmp` splits were retained because the
  kernel/EFI and MicroPython/host builds genuinely differ. IPv4 ARP entry
  still returns `-1` and received IPv4 packets are still dropped; IPv6
  ND/ICMPv6/UDP6/routing/Ether/loopback were not rewritten. Kernel
  build/link **PASS** (122/122 selected, 124/124 full closure, ABI PASS, 0
  undefined symbols; `build/gcc/9pc64.elf`, 955,200 bytes, SHA-256
  `23372e625fc456f743cb2982902974fd932dde768fc2dfb0672c66889a1ec042`).
  Because shared headers changed, MicroPython was rebuilt: **PASS**, 240
  units, 0 FAIL, `build/gcc/micropython.elf`, 458,648 bytes, SHA-256
  `0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8`
  (byte-identical to N11.7/N11.HW2). The host GE/O formatter also rebuilt
  **PASS** (`build/format-o/format-o`, 73,496 bytes, SHA-256
  `cc3edb4e56f02a169e7d74c8b206db9ae6d5f189ca88f04caf382c1afebee493`).
  The Clang/LLD EFI loader still compiles and links (`build/gcc/bootx64.efi`,
  22,016 bytes, PE32+ EFI application; no EFI boot was attempted).
  One disposable QEMU `oboot=server` boot **PASS** — `OEXECUTE` OID
  `0x4f45584543000003` len 458,648, `init: run() -> 1`, GE/O
  `COMMIT SUCCESS`, `REPL READY`, `>>>`, no panic (log
  `build/n11-9/boot.log`, image `build/n11-9/disk.img`). No physical
  p5/ESP write was made; this is **not** new physical hardware
  verification, and the N11.HW2 physical-install versus owner-observed-boot
  distinction above is unchanged.
- N11.10 GCC `-fplan9-extensions` removal: **PASS** — the canonical kernel,
  independent MicroPython ELF and host GE/O formatter no longer pass
  `-fplan9-extensions`. The flag was removed from
  `tools/host/build-kernel.py` (kernel and generated user-image flags),
  the then-named `tools/host/build-mp-gcc.py` (now `build-mpy-gcc.py`) and
  `tools/host/build-format-o.sh`; no other
  canonical invocation reintroduces it. Anonymous typedef fields and implicit
  embedded-pointer conversions were replaced with named fields and explicit
  member addresses across 81 `kernel/`, 19 `ge/`, 4 `include/` and 2
  `runtime/` source files. Families converted include `Lock`/`QLock`/`RWLock`/
  `Rendez`/`Ref`/`Timer`, `PMach`/`PFPU`/`PMMU`, `ISAConf`/`Hciimpl`/`Netif`/
  `SDperm`, `Iphash`/`Routehint`/`RouteTree`, `Key`/`Val`/`Kvp`/`Limbo`/`Avl`,
  the nested FP save states and `Bhdr`/`Amlio`; callers now use
  `lock(&x->lk)`, `incref(&x->ref)`, `newpage(..., &s->qlk)`,
  `limbo(op, &b->limbo)` and equivalent explicit accesses. Field order and
  types were preserved; the IPv6/ICMP packet fields use named
  `ip6hdr`/`icmphdr` substructures with the same offsets and sizes. Kernel
  build/link **PASS** (122/122 selected, 124/124 full closure, ABI PASS, 0
  undefined symbols; `build/gcc/9pc64.elf`, 955,200 bytes, SHA-256
  `20b37b304aa234593ea73eff0aaffe50ede1decfae3ea765b391e48b75441907`).
  MicroPython **PASS** (240 units, 0 FAIL, `build/gcc/micropython.elf`,
  458,648 bytes, SHA-256
  `0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8`,
  byte-identical to N11.9/N11.HW2). Host formatter **PASS**
  (`build/format-o/format-o`, 73,496 bytes, SHA-256
  `cc3edb4e56f02a169e7d74c8b206db9ae6d5f189ca88f04caf382c1afebee493`,
  byte-identical to N11.9). Clang/LLD EFI loader **PASS**
  (`build/gcc/bootx64.efi`, 22,016 bytes, PE32+; no EFI boot attempted).
  One disposable QEMU `oboot=server` boot **PASS** (software emulation; KVM
  access was unavailable): `OEXECUTE` OID `0x4f45584543000003` len 458,648,
  `init: run() -> 1`, GE/O `COMMIT SUCCESS`, `REPL READY`, `>>>`, no panic
  (log `build/n11-10/boot.log`, image `build/n11-10/disk.img`). No physical
  p5/ESP write was made; this is **not** new physical hardware verification,
  and the N11.HW2 install-versus-boot distinction above is unchanged.
- N11.11 build identity and MP→MPY naming: **PASS (source/build/QEMU)** — the
  kernel build generates `build/gcc/gen/kbuildid.h` with one UTC identity,
  `PLAN2001-O-KERNEL-BUILD:YYYY-MM-DD HH:MM UTC` (honouring
  `SOURCE_DATE_EPOCH` when supplied), and `kernel/amd64/ouser.c` prints
  `Plan2001-O kernel build: <stamp>` once immediately before the userspace
  handoff. The built ELF `build/gcc/9pc64.elf` (955,232 bytes, SHA-256
  `d719742a85fba7c39f58136d7c1b6f48d62208e540e795675075c9ca2a6a7216`)
  contains exactly one valid stamp, `2026-10-29 04:43 UTC`.
  `tools/host/gcc-o-install.sh` derives the title
  `Plan2001-O (2026-10-29 04:43 UTC)` from that ELF, keeps stable id
  `plan2001-o`, rejects missing/ambiguous/malformed stamps, and checks the
  installed ELF bytes and stamp before/at `--install` and `--verify`; the
  single-entry, Debian-default, plan9.ini-LF, legacy-script and rollback
  safeguards are unchanged. The installer `--plan`/`--install`/`--verify`
  paths were **NOT RUN** against a real or disposable ESP/partition (no
  configured fixture and no ESP/GRUB write was permitted); the title
  derivation was verified non-destructively against the actual ELF.
  Plan2001-O-owned MicroPython names moved to MPY: the entry points are now
  `tools/build-mpy-gcc.sh` and `tools/host/build-mpy-gcc.py`, with internal
  MPY variables, `build/gcc/mpy` intermediates and MPY diagnostics; the
  public/persistent names `OID_MP_EXEC` (numeric OID unchanged), `MpOid`→private
  `MpyOid`, `MPEXEC`/`NOMPEXEC`, `seed/mp`/`seed/mpoid`, the
  `mp-1.27.0-mpy6.3` Environment runtime, the historical `mpelf`/`mpelf_end`
  embedded-payload symbol guard and upstream `mp_*`/`.mpy` names are
  intentionally retained. Canonical kernel build/link **PASS** (122/122
  selected, 124/124 closure, ABI PASS, 0 undefined symbols). MicroPython
  **PASS** (240 units, 0 FAIL, `build/gcc/micropython.elf`, 458,648 bytes,
  SHA-256 `0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8`,
  unchanged). Host tools and a fresh disposable O image were produced through
  the renamed seeder path: `build/n11-11/o.img`, 135,266,304 bytes, SHA-256
  `75ec32ef2868c97e34cc8f50364231247af0ebdc1c3ac7cd7e700fa2a3d42e32`,
  with the exact 458,648-byte MicroPython ELF at OID
  `0x4f45584543000003` and runtime `mp-1.27.0-mpy6.3`. Clang/LLD EFI loader
  **PASS** (`build/gcc/bootx64.efi`, 22,016 bytes, PE32+). One disposable
  QEMU `oboot=server` boot **PASS** (software emulation; KVM access was
  unavailable): the build line appeared exactly once and matched the ELF,
  followed by `OEXECUTE`, `init: run() -> 1`, GE/O `COMMIT SUCCESS`,
  `REPL READY`, `>>>`, no panic (log `build/n11-11/boot.log`, image
  `build/n11-11/disk.img`). No physical p5/ESP/GRUB/firmware write was made;
  this is **not** new physical hardware verification, and the N11.HW2
  install-versus-boot distinction above is unchanged.
- The N11.6 and earlier entries below are **historical verification
  records**, not the current tree.
- N11.6 O-only Space API removal: **PASS** — removed the Python exports
  `space.define`, `space.mkenv`, `space.create`, `space.view`, `space.View`
  and `space.upgrade`, the V1–V7 file/DMOBJ backend, the V7 upgrade/view
  world and the unused V1–V4 restore/check code. `space.open` and the native
  O API remain. `p9space.c` 2,595 → 1,199 lines; `p9image.c` 3,450 → 1,859;
  `p9space.h` 114 → 69.
- N11.6 GCC MicroPython build: **PASS** — 247 units, 0 FAIL,
  `build/gcc/micropython.elf`, 458,816 bytes, SHA-256
  `358a6d7f7913bac6a149c6e3b2e13bd6d15bfd7f1da2c568e698014ae7d422b7`.
- N11.6 O image and QEMU boot: **PASS** — `build/n11-6-clean.img`,
  16,018,046,976 bytes (15276 MiB), SHA-256
  `6020bd4c96d2d0d160d38215e9ea523144cb1665f41d2aaf059ed5eb6a94962c`;
  `oinspect` confirms OID `0x4f45584543000003` holds the exact 458,816-byte
  N11.6 MP ELF. QEMU `oboot=server` reached `OEXECUTE`, `init.run()`,
  `OSPACECOMMIT`, `PLAN2001-O: REPL READY` and `>>>`. Cross-power-cycle
  persistence was **NOT TESTED** by that agent.
- N11.5 Space source audit (historical): **PASS with retained legacy** — the V1–V7 path
  backend and the V7 upgrade/view world remain because `space.open/create/
  define/mkenv/view/upgrade` are still public and deleting their
  implementations would change visible errors. Removed only the dead
  `p9_space_isopen()` accessor, its unused prototype, a stale
  `p9_world_env()` prototype and an unused local in `ospace_open()`.
  `space_globals` and `p9image.c` are unchanged.
- N11.5 GCC MicroPython build: **PASS** — 247 units, 0 FAIL,
  `build/gcc/micropython.elf`, 508,144 bytes, SHA-256
  `d595e127b966883dcfe9ae7f890400c8ac1eb227ae3585447c6ff77c27c9f8c9`
  (unchanged from N11.4).
- N11.5 O image and QEMU boot: **PASS** — `build/n11-5-clean.img`,
  16,018,046,976 bytes (15276 MiB), SHA-256
  `aaf9e1c1e96b2d48cb7fb14980d84e0d3ef13e4b414f2caeed483763f5b5eabb`;
  `oinspect` confirms OID `0x4f45584543000003` holds the exact N11.5 MP ELF.
  QEMU `oboot=server` reached `OEXECUTE`, `init.run()`, `OSPACECOMMIT`,
  `PLAN2001-O: REPL READY` and `>>>`. Cross-power-cycle persistence is
  **NOT TESTED**.
- N11.4 GCC MicroPython build: **PASS** — `build/gcc/micropython.elf`,
  508,144 bytes, SHA-256
  `d595e127b966883dcfe9ae7f890400c8ac1eb227ae3585447c6ff77c27c9f8c9`;
  247 units, ABI checks PASS, no undefined symbols, 75 libc members.
  `p9file.c` and `boot.py` are gone; pathname `open()` is not registered,
  while `io.StringIO`/`io.BytesIO` remain. The generated syscall stubs are
  byte-identical to the N11.3 stubs.
- N11.4 O image: **PASS** — `build/n11-4-clean.img`, 16,018,046,976 bytes
  (15276 MiB), SHA-256
  `aaf9e1c1e96b2d48cb7fb14980d84e0d3ef13e4b414f2caeed483763f5b5eabb`.
  `oinspect` accepted both superblocks, O v1, the Environment, `.mpy` code
  and OID `0x4f45584543000003` containing the exact 508,144-byte N11.4 MP ELF.
- N11.4 QEMU O-server boot of that image: **PASS** — `OEXECUTE`, `init.run()`,
  `OSPACECOMMIT` (init version 1, O version 2), `PLAN2001-O: REPL READY`
  and `>>>`. The boot does not demonstrate cross-power-cycle persistence.
- N11.3 GCC kernel build: **PASS** — `build/gcc/9pc64.elf`, 992,120 bytes,
  SHA-256 `8157e6f5d3315c207257b05b3e945f50400015bd44ef0b7afb8bb6200302ac76`;
  124/124 selected units, 126/126 full closure, ABI check PASS, link PASS.
  `KERNEL_SOURCES` no longer contains `legacyexec.c`, `legacypath.c` or
  `rebootcmd.c`; the removed pathname syscall numbers resolve to
  `sysunsupported()`.
- N11.3 QEMU O-server boot (historical): **PASS** on the N11.2 image; the
  same kernel path remains in N11.4. The USB guard boot-safety correction is
  unchanged.
- N10.1 GCC kernel build (historical): **PASS** — `build/gcc/9pc64.elf`,
  1,009,928 bytes, SHA-256
  `71966836f9731864d86116aac0f4375538add1aa1de5c2236fbf4da9f695b986`;
  ELF64 `ET_EXEC`, explicit manifest, no interpreter bytes.
- N10.1 Clang/LLD EFI loader build: **PASS** — `build/gcc/bootx64.efi`,
  22,016 bytes, PE32+ EFI application with the same entry/subsystem checks.
  The COFF timestamp makes the file SHA-256 build-specific, so no stable
  hash is claimed.
- GCC MicroPython build (historical N11.2): **PASS** — 518,672 bytes,
  SHA-256 `fe51097ccacd7842121567a56a0c4d7b259475d0fccc6ef481a6db53e88da14f`.
- N11.2 runtime cleanup: **PASS** — tracked files fell from 1,051 to 756;
  `runtime/` fell from 411 to 116 files. `runtime/libc` now has an explicit
  MP manifest (75 members) plus the formatter/kernel union; SHA-2 and the
  live `runtime/{ip,aml,fis}` members are retained; unused algorithms,
  amd64 Ken C assembly, `ucd` generators/data and 9front mkfiles are gone.
- N11.2 host formatter/seeder: **PASS** — GCC 14 builds
  `build/format-o/format-o` (73,496 bytes) from the O-only `ge/format`
  units and an explicit 13-file fmt manifest, and
  `build/mpy-cross/mpy-cross` from MicroPython 1.27.0. No 9front VM,
  Ken C `6c`/`6l` or Plan 9 `mk` is used.
- N11.2 disposable 129 MiB O v1 image (historical): **PASS** — `build/n11-2-clean.img`,
  135,266,304 bytes, SHA-256
  `10bbdb154aa1d27a886834507d316967d92b7b108e9e5196cc8e05ef2a53fe7c`
  (byte-identical to N11.1). Read-only `oinspect` accepted both
  superblocks, all eight arenas/logs, `init -> SpaceID 1`, the Environment
  (`mp-1.27.0-mpy6.3`), the `.mpy` code object, and OID
  `0x4f45584543000003` containing the exact full GCC MicroPython ELF. The
  seeded tree is multi-level (height 2, 36 blocks, 1,085 records).
- N11.2 QEMU O-server boot (historical): **PASS** — the normal
  kernel/loader opened the O volume, called `OEXECUTE`, ran `init.run()`
  (Counter 1, alias same), committed, and reached
  `PLAN2001-O: REPL READY` and `>>>`. p5 was not touched.
- Previous N9.8 QEMU O-server smoke: **PASS** for that earlier binary
  (loader, kernel, `OVOLOPEN`/`OSPACEOPEN`/`OEXECUTE`, `init.run()`, commit,
  no panic). It was **not repeated** after the N10 source move; no N10 boot
  PASS is claimed. p5 was not modified.
- N10.1 ESP/GRUB install on the Dell `kuha` (Precision 7520): **PASS**.
  The ESP now holds the N10.1 kernel (`71966836…`), loader (`08f36695…`),
  22,016 bytes, and the 125-byte LF-terminated `plan9.ini`
  (`6b16494b…`, final byte `0a`);
  GRUB has exactly one Plan2001 entry, `Plan2001-O` (id `plan2001-o`), which
  chainloads `/EFI/plan2001-gcc/BOOTX64.EFI`. The historical
  `42_plan2001` and `43_plan2001_o` scripts are non-executable and their old
  ESP directories were not modified. Debian remains `set default="0"` and all
  non-Plan2001 entries are unchanged. `--plan` was read-only (grub.cfg hash
  unchanged); `--verify` passed. p5 was not written.
- Physical boot of the N10.1 kernel: **PASS** (owner-observed interactive
  MicroPython REPL from the single `Plan2001-O` GRUB entry). No separate
  photographed log is claimed for this observation.
- Broad historical suites, multi-boot matrices and debug harnesses: removed from
  the tree and **NOT RUN BY DESIGN**.

## N11.7 legacy removal

**Removed (source- and link-proven dead):**

- `kernel/core/cache.c` (514 lines) and its `cache` manifest object: the
  inherited `Mntcache` read-ahead layer. `cinit` had no caller, so
  `copen`/`cread`/`cwrite`/`cupdate`/`ctrunc`/`cclunk` and `Chan.mcp` were
  never reachable. `fscache` could therefore never be allocated, and the only
  `pagereclaim(fscache)` call was a no-op. Clean idle-image reclamation was
  later removed with the dead Image cache by N11.8.3; there is no swap and
  no paging mechanism. `ge/storage/cache.c` is a different, active GE/O
  cache and was not touched.
- `Chan.mcp`, the `Mntcache` typedef/incomplete declaration, `fscache`, and
  the never-generated `initcode.i`/`initcode[]` build remnant.
- Unreferenced helpers `emptystr`, `eqqid`, `canmount`, `devpower` and
  `devconfig`, plus their prototypes.
- The dead pathname bodies of `mp_lexer_new_from_file()` and
  `mp_import_stat()`; both MicroPython port symbols remain with the same
  observable `ENOENT`/`NO_EXIST` result under the pathless kernel.
- Orphan `runtime/libc/9sys` units `access.c`, `dirread.c`, `dirstat.c`,
  `dirwstat.c`, `dirfwstat.c`, `getuser.c`, `nulldir.c`, and their unused
  prototypes.
- Unreferenced inherited headers `include/9p.h`, `auth.h`, `draw.h` and
  `mouse.h`; no source, kernel, MicroPython, formatter or EFI include chain
  used them. `fcall.h` and `runtime/abi/sys.h` remain live contracts.

**Retained in the current source tree (as of N11.7; N11.8.5 later removed the
canonical `Dev` registry, `devno`, and the `Chan`/`Walkqid` types):**

- `Dev`, `Chan`, `Qid`, `Path`, `Walkqid`, `Rgrp`, `Egrp`, `Pgrp`,
  `Proc.fdgrp` (legacy RFORK presence token), `devno`, active `Dev` dispatch,
  the legacy FD syscall slots (now canonical `Ebadfd` paths),
  `sysfstat`/`sys_fstat` and the generated systab callbacks remain in the
  current amd64 source tree where linked. N11.7D.2 removed the unreachable
  directory/lifecycle helper family; N11.8.1 removed the unreachable
  `mkqid`/`newchan`/`pipechanpair`/`newfd2` pipe-creation island; N11.8.2
  removed the FD success layer; N11.8.3 removed the `Fgrp` type/API, the
  file-backed `Image -> Chan` island and the orphan Chan close lifecycle
  (see Current verification). `devno` is retained for
  `pgrp.c::devmask()`. The canonical `pc64` configuration registers no
  `dev` rows; the former retention rationale based on dormant non-target
  sources is superseded by the amd64-only scope decision.
- PIPE: `plan9.pipe()` uses the legacy `PIPE` ABI -> `syspipe()`, which keeps
  its pointer validation, `-1` output initialization and `Ebadsharp` result;
  N11.8.1 removed the old `devno('|')`/`pipechanpair()`/`newfd2()` tail, which
  was already unreachable because the canonical kernel registers no pipe
  `Dev`. The handle-based `PIPENEW`/`PIPEREAD`/`PIPEWRITE`/`PIPECLOSE` path is
  separate and remains. Redirecting `plan9.pipe` to `PIPENEW` would change a
  public API and was **not** done.
- IPv6: `-DE7_IPV6_ONLY` is the canonical define. IPv4 ARP/UDP branches
  compile out (the V4 ARP entry returns -1; received IPv4 packets are
  dropped), while IPv6/ND/neighbour, Ether and UDP6 paths remain. No network
  rewrite was attempted.
- `kernel/amd64/obootoid.c`, `kernel/amd64/gcc/apbootstrap.S` and
  `rebootcode.S` remain generation inputs for the canonical amd64 build.

**Requires a new API design decision:**

- `plan9.pipe()` versus the native pipe handles (above).
- A native persistent relation/API model beyond `OID_GRAPH`; N11.8.1 began
  the resource-model work by removing the dead pipe-creation seam, N11.8.2
  removed the FD success layer, and N11.8.3 removed `Fgrp` and the dead
  file-backed Image/Chan island. Complete removal of the remaining opaque
  `Chan`/`Path` type references and legacy SD/LED signatures is still
  unimplemented.

## Implemented core

- **GE/O v1:** `(SpaceID, OID)` identity, a local OID allocator, versioned
  opaque byte records, commits, a flat `init` catalog and a native
  `PARTUUID=` volume open through a bounded block provider. `space.oput`,
  `space.oget` and `space.oregister` are the user-facing native bindings.
- **Process object views and Pipe IPC:** bounded in-memory `OContext` role
  bindings, copied/shared/empty `Pgrp` views, generation-checked local
  handles and kernel-enforced object rights. Pipe endpoints cross `rfork`
  only through explicit, attenuated, one-shot selection. See
  [N13-OBJECT-CONTEXT](N13-OBJECT-CONTEXT.md); this is not full container
  isolation or a persistent native relation graph.
- **Native O startup:** the kernel bootstrap opens the O volume and Space, then
  loads MicroPython by fixed OID through native `OEXECUTE`.
- **Python persistence:** code, the Environment manifest and state come from O.
  The whole Python graph is still committed as one `OID_GRAPH` object, not as
  independently indexed native relations. A REPL function's binding,
  defaults and native code OID are in that graph; executable bytecode is a
  separate native O object. REPL functions with nested code are rejected.
- **REPL:** `obootrepl=1` selects the interactive MicroPython path; absent or
  another value keeps the non-interactive server path. `*ncpu` is a normal SMP
  setting, not a test switch.
- **MicroPython storage boundary:** N11.4 removed `p9file.c` (Python
  `open()`/FileIO on Plan 9 fds) and the built-in `boot.py`/fd-console
  fallback. N11.6 removed the pathname/file Space API and backend:
  `space.create/define/mkenv/view/View/upgrade` are gone. `space.open`,
  the native O object API, `OID_GRAPH`, the Environment manifest and the
  native keyboard/`IOWRITE` REPL remain. `io.StringIO`/`io.BytesIO` and the
  stream machinery remain.
- **Storage/network:** native SD and GE/O paths, native IPv6, framebuffer
  console, PS/2 keyboard, UART, PCI, USB and the existing scheduler/syscall
  core. N11.3 removed the pathname `EXEC`/`OPEN`/`CREATE`/`REMOVE`,
  path-based `STAT`/`WSTAT`, `FD2PATH` and bind/mount/chdir handlers; those
  ABI numbers fail through `sysunsupported()`. The legacy FD syscall slots
  remain but their success paths were removed by N11.8.2; `Chan`/`Dev`,
  `Proc.fdgrp` presence, the retained opaque `Path` type and `validname`
  remain where linked; see
  [LEGACY-DEPENDENCIES](LEGACY-DEPENDENCIES.md).
- **No swap:** the canonical kernel has no swap device or swap-backed fault
  path. Clean page/image reclamation is distinct from swap.

## N9.8 cleanup

This tree removed the repository-owned test, smoke, fixture, analytics and
debug-only paths, and consolidated the ESP installers:

- no repository-owned QEMU smoke, regression harnesses, fixture seeders or
  source-analysis generators (the only later exception is the two sanctioned
  scripts documented in [QEMU-O-TESTING](QEMU-O-TESTING.md));
- no diagnostic checkpoint fields, probe/multi-mode boot selectors or other
  debug knobs;
- one canonical ESP/GRUB/`plan9.ini` installer (`tools/gcc-o-install.sh`);
- one persistent Plan2001 GRUB entry (`Plan2001-O`, id `plan2001-o`), with the
  historical 42/43 scripts deactivated and their old entries out of the menu;
- one canonical seed path (`tools/mkovol`, `tools/host/oseed.py`, `tools/host/odemo.py`,
  `MPEXEC`, Environment/code OIDs).

Historical investigations and logs are retained as reference material, not maintained runbooks.

## Commands

The single maintained command set:

```sh
# native Linux formatter + mpy-cross host tools for mkovol
WHAT=kernel tools/build.sh

# independent artifacts
tools/build-gcc.sh --kernel -j 24
tools/build-efi-gcc.sh -j 24
tools/build-mpy-gcc.sh -j 24

# ream/seed and install the new O image on p5
tools/gcc-ovol-ream.sh --image build/fresh-p5.img 15276
tools/gcc-ovol-ream.sh --plan --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid> --image build/fresh-p5.img
sudo -n tools/gcc-ovol-ream.sh --install --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid> --image build/fresh-p5.img

# ESP kernel/loader/plan9.ini and the single Plan2001-O GRUB entry
tools/gcc-o-install.sh --plan --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>
sudo -n tools/gcc-o-install.sh --install --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>
tools/gcc-o-install.sh --verify --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>
```

Reaming p5 and installing the ESP entry are separate. Reaming does not touch
GRUB or the ESP; ESP installation does not format or write p5. The installer
and reamer require the live partition identity, expected size, unmounted state
and no holders, and never perform a whole-disk operation.

## Not implemented / proposed

- Native persistent indexed relations beyond `OID_GRAPH`.
- A general executable-object/program catalog and loader.
- Arbitrary-process IPC delegation and a remote object protocol.
- Pathless console and complete removal of the remaining opaque `Chan`/
  `Path` legacy signatures, `Proc.fdgrp`, `Rgrp`, `Egrp` and related
  inherited type names.
- Encrypted GE/O, verified boot, SEALED/authenticated networking and the
  production administration/authority model.
- A general crash-recovery or durability guarantee.

See [O-FORMAT](O-FORMAT.md) for the storage contract,
[KERNEL-API-ABI](KERNEL-API-ABI.md) for API/ABI boundaries,
[GCC-PORT](GCC-PORT.md) and [GCC-HW-BOOT](GCC-HW-BOOT.md) for the GCC build and
physical-boot howto, and [README](../README.md) for the overview.
