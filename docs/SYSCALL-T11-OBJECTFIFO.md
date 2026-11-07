# T11 — Validate the existing general ObjectFIFO

**Status: PASS (2026-11-06), validation plus one narrowly reproduced defect
fix; strengthened follow-up PASS (2026-11-07), no kernel change.** This round
validates the existing universal `ObjectFIFO`; it does not redesign it, add a
FIFO variant, add a syscall or start T12–T16. The 2026-11-07 follow-up replaces
four weak assertions with negative/positive tests and records the
subscription-close-while-blocked case as NOT RUN with source analysis
(section 5A). The 2026-11-06 results are retained as history.

## 1. Authorization and tested baseline

T11 was assigned as a separate task after T8/T9/T10. The inspected host is
`kuha` (Linux 6.12.111+deb13-amd64, GCC 14.2.0, QEMU 10.0.13 with KVM and 2
vCPUs for the final concurrency run). The source tree before this round was
byte-identical to the published snapshot

```text
f1e1d180094266542b16883d7fa38af9aefda057332ebf025586e754e888edca
2026-11-06T19:49:27Z, 870 files
```

All 870 manifest entries matched the working tree before any T11 change
(file size and SHA-256). The only kernel source change in T11 is the FIFO
receive timeout defect described in section 6; the tested kernel ELF is:

```text
build/gcc/9pc64.elf
sha256 5b2dd2e1df2e7dd1b7f55c9261a9976b6123a0b1068ed23ef4330784901ee391
```

The test inputs were later copied unchanged into `tools/host/t11/`; their
hashes are recorded in the companion JSON. The final published source snapshot
also contains this document and test-status updates, so it necessarily has a
different content hash from the tested kernel source; the kernel ELF hash
identifies the executed kernel.

## 2. What was validated

The tests used the normal canonical boot and genuine independent MicroPython
processes. A small REPL bootstrap starts one orchestrator child through the
real `space.spawn()`/`OBJINVOKE(ObjOpOspawn)` path. The orchestrator receives a
delegated `VkindSpace` Handle, a code-feed `Pipe` Handle and an output `Pipe`
Handle through the explicit `Oinheritv` (`vhandles`) list. It then starts
further MicroPython children with `space.spawn(..., handle=spacehandle,
vhandles=...)` and waits with `space.wait()`.

This keeps the REPL input short and avoids implicit handle inheritance. No
kernel path, `Chan`, `fd`, VOBJ* shim or protocol-specific FIFO is used.
Test code is fed as ordinary bytes over a native Pipe and executed by the
child; the child processes are real ELF MicroPython processes, not coroutines.

Three boots were executed:

| Boot | VM | Result | Log |
| --- | --- | --- | --- |
| A | 1 vCPU | 15/15 PASS | `build/t11/logs/bootA9/` |
| B | 1 vCPU | 32/32 PASS | `build/t11/logs/bootB5/` |
| C | 2 vCPU, 2 kernel CPUs | 12/12 PASS | `build/t11/logs/bootC2/` |

Total: **59 observed PASS checks, 0 FAIL**. A pre-fix amplification probe is
kept at `build/t11/logs/amp-prefix3/`; pre-fix negative-timeout evidence is in
`build/t11/logs/bootB3/`. No boot log contains `panic`, `suicide`, `BOOT
FAILED`, a bad-disk/flush/sync error, or an observed use-after-free report.

**2026-11-07 follow-up (current evidence).** The original 59-check run is
preserved as history. The follow-up reran all three suites against the same
kernel artifact after strengthening boots B and C: boot A 15/15
(`build/t11/logs/bootA10/`), boot B 35/35 (`build/t11/logs/bootB7/`), boot C
20/20 (`build/t11/logs/bootC4/`) — **70/70 observed PASS** — plus a
deliberately-wrong mutation run (`build/t11/logs/mut1/`, `MUT-RESULT 3 6`)
showing the new assertions fail for the conditions they are meant to reject.
No kernel source changed in the follow-up. Details are in section 5A.

## 3. Multiprocess TEE, identity and independence (boot A)

One FIFO had three independent subscriptions: the orchestrator plus a slow
child (capacity 2, 1600 ms delay) and a fast child (capacity 8, immediate). The
children acquired their own subscriptions from attenuated delegated FIFO
Handles; each queue was independent.

| Case | Expected | Observed |
| --- | --- | --- |
| `tee-subs` | 3 subscribers on the same FIFO | 3 |
| `env0`–`env5` | each of six envelopes preserved per message | all PASS |
| `parent-order` | parent sees `A0..A5` in order | `A0,A1,A2,A3,A4,A5` |
| `identity` | `(incarnation,space,oid)` unchanged by transport | equal |
| `fast` | capacity 8 receives all six, per-sub drop 0 | `A0,A1,A2,A3,A4,A5,0` |
| `slow` | capacity 2 keeps newest two, per-sub drop 4 | `A4,A5,4` |
| `empty` | non-blocking receive on empty queue errors | `vobj: receive would block` |
| `timeout` | bounded receive returns a timeout error | `vobj: receive timed out` |
| `waitpids`, `waitok` | both child PIDs and successful status | PASS |

This proves ordered per-subscriber delivery, independent bounded queues,
drop-oldest overflow accounting and TEE/fanout without one subscriber blocking
another. Note: `VobjFifostat.drops` is the FIFO-wide counter; the per-subscript
counter is `VobjInfo.sub_dropped` (`obj_info` index 13). The first iteration of
the test used the FIFO-wide value and exposed this reporting distinction.

## 4. Authority, rights and negative cases (boot B)

| Area | Cases and observed result |
| --- | --- |
| Sender authority | `send-vsend` PASS; missing `Vsend` on FIFO → `Eperm`; missing `Vdelegate` on sent object → `Eperm` |
| Attenuation | `obj_grant(fifo, VSEND+VMETA)` yields exactly those rights; granting `VCONTROL` beyond them → `Eperm` |
| Received-handle policy | delegated object rights flow only as far as the sender held; adding `Vwrite` → `Eperm`; default subscription receives no `Vdelegate` and cannot grant from it |
| FIFO-reference publication | publishing another FIFO → `vobj: FIFO references are not transferable`; publishing the FIFO to itself → `Ebadarg` |
| Envelope | 65-byte envelope rejected; 0–64 accepted (`env-limit` PASS) |
| Stale/duplicate handles | read after close → `Ebadarg`; second close → `Ebadarg` |
| Closed subscription | receive after close → `Ebadarg` |
| Capacity validation | `obj_fifocreate(-1)` and subscription capacity 1025 both rejected |
| Subscription rights policy | `Vcontrol` not requestable; `Vsubscribe` not requestable; requesting `Vsend` when the parent FIFO Handle lacks it → `Eperm`; subscription Handle cannot be granted |
| Pending message lifetime | publish, close the sender's object Handle, then receive and read `PEND` → PASS |
| Revoke while blocked | child blocks with the default timeout; `obj_revoke` wakes it with `Ehungup` (reported `HANGUP`) |
| Receiver exit | blocked child terminated; subscription count 1 → 0; status `micropython 12: terminated` |
| Teardown/repeat | subscription close drains its queue; 50 create/publish/close/revoke cycles PASS |

### 4.1 `vobjfifosub()` requested rights versus parent FIFO rights

The implementation does not literally intersect every requested bit with the
parent FIFO Handle. The observed and intended policy is narrower and coherent:

- The parent Handle must carry `Vsubscribe`; creation fails without it.
- `Vsend` is accepted only if the parent FIFO Handle also carries `Vsend`.
- `Vcontrol` and `Vsubscribe` are not in the requestable set, so a subscription
  Handle cannot revoke the FIFO or create further subscriptions.
- The remaining class bits (`Vread`, `Vwrite`, `Vaccept`, `Vconnect`,
  `Vdelegate`, `Vclose`, `Vcreate`, `Vcommit`) are a receive-policy mask for
  handles delivered through this subscription. They do not operate on the FIFO
  itself, and they never add a right: the delivered grant is intersected with
  the sender's object-Handle rights. `recv-no-add`/`default-no-delegate`
  verified this.
- Subscription Handles cannot be duplicated by `OBJOPEN`, `OBJBIND`,
  `OBJGRANT` or OSPAWN; in particular `obj_grant(subscription, ...)` fails.

A pre-fix probe requested `Vcontrol` in a subscription and was already denied
with `Ebadarg`; the FIFO remained open (`AMP-FIFO-CLOSED 0`). No authority
amplification through subscriptions was reproduced. The earlier sentence in
`VOLATILE-OBJECT-OBJECTFIFO.md` that called subscription rights a literal
subset was imprecise; that document now states the mask/intersection policy.

## 5. Lifecycle, limits, GE/O and quota (boot C, 2 vCPUs)

| Case | Expected | Observed |
| --- | --- | --- |
| `ge-create` | persistent GE capability created through the Space Handle | kind `VkindGe`, OID matches |
| `ge-sub` | child subscribed before publication | 2 subscribers |
| `ge-interproc` | child reads `GE-PAYLOAD` and the same OID | `PASS` |
| `ge-wait` | child exits successfully | `PASS` |
| `no-ambient-handle` | child cannot read a non-delegated parent object by guessing Handle numbers 1–63 | `NOSECRET` |
| `handle-bound` | live Handle table is bounded | 90 creates succeeded with 6 existing Handles, then `vobj: too many handles` |
| `handle-recover` | after closing the created Handles, a new Handle succeeds | `PASS` |
| `quota-subs` | 8 children × 8 subscriptions = 64 subscribers | 64 |
| `quota-sends` | 1024 sends fan out to 64 queues | 1024 sends |
| `quota-global-limit` | 65,536 queued messages fill the global quota; the next send fails before partial publication | `kernel allocate failed` (`Enomem`) |
| `quota-children` | release message wakes all eight control subscribers; all wait statuses are successful | `PASS` |

The quota test deliberately fills every per-subscription queue to its 1024
capacity. At the next send, `vmsgreserve(64)` fails before any queue can drop
an old message. The send therefore fails with `Enomem` and leaves the queues
unchanged; it does not partially deliver. This is the observed interaction
between global reservation and per-subscription drop-oldest under saturation.

The 2-vCPU boot also exercised concurrent child creation, subscribe, publish,
receive, revoke, terminate and teardown without a panic or hang. This is a
limited concurrency observation, not a long stress campaign or a multi-CPU
proof of every lock interleaving.

*This table records the original 2026-11-06 run. Section 5A replaces the
`no-ambient-handle` and `quota-global-limit` checks with the strengthened
versions and records the subscription-close-while-blocked case as NOT RUN.*

## 5A. Follow-up: strengthened validation (2026-11-07)

This was a narrow follow-up to the completed T11 task, not a second T11 audit.
The working tree matched the published T11 snapshot
`4a14bfa757a78dcea3976b8872495d17f8e9bd589ff659ba4b4be9d572a7a21d` before any
change. The executed kernel ELF was `build/gcc/9pc64.elf`, SHA-256
`5b2dd2e1df2e7dd1b7f55c9261a9976b6123a0b1068ed23ef4330784901ee391`;
MicroPython was `589a048560c3952671e5ffdf6dc6d5b65337ba78619de821fe1667128840b6aa`;
the loader was `37e78845d2b555bc6bac95b9c05eb4ec49e350332fa24b612f6a413d02eb78b0`.
**No kernel or ABI source changed in this follow-up**; only the two T11 test
orchestrators and these documents changed.

### 5A.1 `revoke-wakes` (boot B)

*Original weakness:* the worker published `READY` before it necessarily entered
`obj_receive`, and the test accepted any `OSError`. A revoke that arrived before
the receive, or the pre-fix immediate `receive would block`, could still pass.

*Strengthened procedure:* the worker publishes `READY` and immediately calls the
default infinite `obj_receive(s)`. The parent then performs a 300 ms observation
receive on the result FIFO and requires the **specific** timeout error
`vobj: receive timed out`. Only after that does it revoke the FIFO. The woken
worker publishes the exact `str(e)` it observed, and the parent requires the
exact `i/o on hungup channel` (`Ehungup`), not merely some `OSError`.

The 300 ms window proves no result (including an early `would block` error) was
produced after `READY`; it is a defensible observation procedure, not a direct
"this process is in `tsleep`" probe because the API does not expose a sleeper
count. The default infinite timeout is preserved. Observed:
`T11-B blocked-settle PASS`, `T11-B revoke-err i/o on hungup channel`,
`T11-B revoke-wakes PASS`.

### 5A.2 `no-ambient-handle` (boot C)

*Original weakness:* the child guessed integer handles 1–63. Real handles are
generation-tagged, so most guesses could not name anything even if ambient
authority existed.

*Strengthened test:* the parent passes its real full-width secret Handle value
as **data** in the child program, and separately delegates a control object
(`AUTHORIZED-CTRL`) over the explicit OSPAWN `vhandles` list with
`Vread+Vmeta`. The child identifies the delegated control and result FIFO by
`obj_info` kind, must read the control bytes (`ambient-control` PASS), must get
the exact `bad arg in system call` (`Ebadarg`) when it tries the passed secret
Handle value (`ambient-denied` PASS), and must not find the parent's secret OID
among its own handles (`ambient-oid-unreachable` PASS). Combined marker
`no-ambient-handle` PASS, raw result `110`. This is a real non-delegated
Handle/identity boundary with an authorized positive control, not a guessing
loop.

### 5A.3 `exit-reaped` (boot B)

*Original weakness:* `ck('exit-reaped', True)` was unconditional.

*Strengthened test:* after `space.terminate(p2)`, the parent asserts the
reaped PID equals `p2` (`exit-pid`) and the status is exactly
`micropython <p2>: terminated` (`exit-status`); `exit-reaped` is the conjunction.
Observed status for PID 12: `micropython 12: terminated`; all three checks PASS.

### 5A.4 `quota-global-limit` (boot C)

*Original weakness:* any `OSError` was accepted and no queue was checked for
partial publication.

*Strengthened test:* total subscribers remain exactly 64, now 8 children × 7
subscriptions plus 8 parent-owned witness subscriptions. After 1024 sends,
65,536 messages are queued. The next publish must fail with the exact
`kernel allocate failed` (`Enomem`). The test snapshots FIFO `sends`/`drops`
and all eight witness `(sub_capacity, sub_queued, sub_dropped)` triples before
and after the failed send. Observed: exact `Enomem`;
`quota-no-partial-send`, `quota-no-partial-drop`, `quota-witness-count`,
`quota-witness-full` (all eight at `1024/1024/0`) and
`quota-queues-unchanged` PASS. The failed publish therefore delivered nothing
and dropped nothing, and the queues remained unchanged.

### 5A.5 Subscription close while a receive is blocked — NOT RUN

Source review confirms a latent reference-order gap: `vobjfiforecv()` holds the
`Vhand` reference from `vlookup()` across the blocking `tsleep`
(`kernel/core/sysvobj.c:1216-1304`). `vobjclosehandle()` removes the slot and
calls `vhandput()` (`kernel/core/sysvobj.c:978-1005`), but because the receive
still holds a reference, the count does not reach zero and `vhandput()` returns
before `vsudetach()`/`vsubshutd()` (`kernel/core/sysvobj.c:483-497`). A close
issued while a receive is blocked would therefore not call `vsubshutd()` and
would not wake that receiver; it would remain asleep on `s->rd` until another
event (FIFO revocation or process termination).

That path is **not reachable through the supported contract**. The Handle table
is process-local (`Proc.vobjhandles`); OSPAWN creates an empty table and copies
only explicit `vhandles`, and `vobjspawninherit()` rejects a subscription
Handle with `object: subscriptions cannot cross OSPAWN`
(`kernel/core/sysvobj.c:709-739`). RFORK/newproc also starts with an empty
native handle table even when user memory is shared
(`kernel/core/sysproc.c:147-149`). Thus no other process or thread can close the
blocked process's subscription Handle, and the blocked process cannot close it
itself. No supported note handler closes Handles either.

The tested, reachable cases remain distinct and PASS already: FIFO revocation
wakes blocked receivers with `Ehungup` (`revoke-wakes`); receiver termination
interrupts the call, releases the subscription and removes it
(`exit-reaped`, `exit-teardown`); a stale Handle after close fails `Ebadarg`
(`stale-handle`, `double-close`, `closed-sub`). The supported contract is:
**a subscription Handle is process-local and non-transferable; close it only
when no receive is concurrently blocked on it.** A direct close-versus-blocked
receive test is **NOT RUN** because it cannot be expressed with the supported
API. No kernel change was made: no reachable defect was demonstrated. If future
work ever shares the Handle table across threads, this wakeup gap must be
revisited and tested.

### 5A.6 Discrimination and results

A deliberately-wrong mutation run (`build/t11/logs/mut1/`) was used to show the
new checks are not vacuous:

| Condition | Expected | Observed |
| --- | --- | --- |
| revoke before the 300 ms observation window | `blocked-settle` FAIL | FAIL |
| normally-exiting child asserted as terminated | `exit-status` FAIL | FAIL |
| child reads its own authorized control Handle | `ambient-denied` FAIL | FAIL |
| publish with the quota not exhausted | `quota-global-limit` + `quota-no-partial-send` FAIL | FAIL, FAIL |
| publish fails with a different error (`vobj: FIFO is closed`) | `quota-wrong-error` FAIL | FAIL |
| controls (`early-ready`, `exit-pid`, `ambient-authorized-control`) | PASS | PASS |

The mutation marker is `MUT-RESULT 3 6` (3 expected PASS controls, 6 expected
FAIL checks). The strengthened suites themselves are boot A `15/15`
(`bootA10`), boot B `35/35` (`bootB7`) and boot C `20/20` (`bootC4`), all with
`panic=NO` and no bad-disk/flush/sync error.

## 6. Reproduced defect and minimal fix

**Defect:** an infinite FIFO receive used the adapter's negative timeout. The
MicroPython adapter passes `-1`, which becomes `0xffffffffffffffff` in the
unsigned `ObjCall.arg0`. `sysobjinvoke()` applied `bounded(..., 0x7fffffff)`
to that value and rejected it with `Ebadarg` instead of blocking. The
historical FIFO contract is `<0` for an infinite wait. A child that used the
default `obj_receive(subscription)` therefore exited instead of blocking; this
was observed in `build/t11/logs/bootB3/` (receiver subscription disappeared
and termination failed).

**Fix (one isolated kernel hunk, `kernel/core/sysobj.c`, `ObjOpRecv`):** treat
`0xffffffffffffffff` (signed -1) and `0xffffffff` (the 32-bit sentinel used by
the UDP path) as `-1` before calling `vobjfiforecv()`; keep all finite waits
bounded to `0x7fffffff` milliseconds. No syscall, operation number, ABI
layout, rights rule or dispatch-table entry changed.

After the fix, boot B's blocked receiver used the default timeout, was woken by
`obj_revoke` with `Ehungup`, and the terminated receiver's subscription count
dropped to zero. Boot A's finite timeout and empty-queue checks still pass.

## 7. Locking, reference and delivery-core audit

- State locking remains: `Vfifo.lk` for subscriptions/closed/counters,
  `Vsub.lk` for queue/counters, `Vsub.recvlk` for one receiver, `Vstats.lk` for
  global quotas, and the Handle-table lock for lookup/install. The send path
  holds the FIFO lock while allocating pre-reserved nodes and then takes each
  subscription lock; wakeups occur after the subscription lock is dropped.
- A message node holds an object reference; a subscription, Handle and FIFO
  attachment each hold references. Closing the producer Handle does not
  invalidate queued messages (boot B `pending-sender-close`). Revocation drains
  queues, wakes receivers and releases references; FIFO teardown clears
  subscription back-pointers.
- Receive reserves its result Handle before dequeuing and transfers the message
  reference without further fallible allocation.
- The 50-cycle repeat and the 65,536-message saturation did not leak enough
  state to prevent subsequent operations; handle exhaustion recovered after
  closing all test Handles.
- Not run: allocator fault injection beyond the global quota and Handle cap,
  lock-order instrumentation, long-duration randomized stress, and physical
  hardware.

## 8. Delivery core has no protocol/device branch

A source audit of the five FIFO functions in `kernel/core/sysvobj.c`
(`vobjfifosub`, `vobjfifosend`, `vobjfiforecv`, `vobjfifostat`,
`vobjrevoke`) found no token matching TCP, UDP, IPv6, IP, Ethernet, USB,
Wi-Fi/WLAN, protocol, packet, ARP, DNS or HTTP. The FIFO core carries an
opaque object reference and an opaque bounded envelope; authorization and
rights checks are separate from delivery. No protocol-specific FIFO variant
exists in the tested tree.

## 9. Regressions

The canonical build and targeted prior-stage paths remain PASS:

- `tools/build-gcc.sh --kernel -j 8`: 132/132 selected, 134/134 closure,
  ABI/link PASS.
- `tools/build-efi-gcc.sh`: PASS.
- `tools/build-mpy-gcc.sh -j 8`: PASS, 247 units / 0 FAIL, no undefined
  symbols (`micropython.elf` sha256
  `589a048560c3952671e5ffdf6dc6d5b65337ba78619de821fe1667128840b6aa`).
- `tools/build.sh`: PASS (host formatter/mpy-cross).
- T10 targeted REPL regression: `T10-TIME-MONO True`, two 64-bit values,
  `build/t11/logs/regressT10/`.
- T8 multiprocess regression: child PIDs/status, isolation, failed child,
  missing OID, bad ELF and denied writer all PASS,
  `build/t11/logs/regressT8/`.

The generated syscall table remains **135 named / 62 active / 73 reserved**;
no number was added, removed or renumbered.

## 10. Commands (reproduction outline)

```sh
# canonical build
tools/build-gcc.sh --kernel -j 8
tools/build-efi-gcc.sh -j 8
tools/build-mpy-gcc.sh -j 8
tools/build.sh

# generate the REPL bootstrap for each orchestrator
python3 tools/host/t11/mk-boot-input.py \
  tools/host/t11/boot-a-orch.py build/t11/inputs/boot-a-parent.py

# normal boot, real MicroPython, REPL bootstrap; one boot per suite
tools/qemu-o-boot.sh --no-build --output-dir build/t11/logs/bootA9 \
  --timeout 600 --obootexec 0x4f45584543000003 \
  --repl-input build/t11/inputs/boot-a-parent.py \
  --expect 'T11-A-RESULT PASS'

# boot B likewise; boot C with --cpus 2 --ncpu 2
```

The 2026-11-07 follow-up used the same commands with fresh output directories
and a longer timeout (the typed REPL feed is the slow part):

```sh
python3 tools/host/t11/mk-boot-input.py \
  tools/host/t11/boot-b-orch.py build/t11/inputs2/boot-b-parent.py
tools/qemu-o-boot.sh --no-build --output-dir build/t11/logs/bootB7 \
  --timeout 3600 --obootexec 0x4f45584543000003 \
  --repl-input build/t11/inputs2/boot-b-parent.py \
  --expect 'T11-B-RESULT PASS'
# boot A: build/t11/logs/bootA10 ; boot C: --cpus 2 --ncpu 2,
# build/t11/logs/bootC4, --expect 'T11-C-RESULT PASS'
```

The discrimination harness is local build material
(`build/t11/mut/boot-mut-orch.py` → `build/t11/mut/boot-mut-parent.py`, run in
`build/t11/logs/mut1/` with `--expect 'MUT-RESULT'`); it is not a canonical
test input.

Boot C's parent input is generated from `boot-c-orch.py` by the same script.
The scripts are test inputs, not part of the kernel ABI.

## 11. Limitations and NOT RUN

- Physical hardware, TCG fallback, long randomized multi-CPU stress, fault
  injection of individual allocations, and exhaustive lock-order proof were
  not run.
- Boot C exercised real 2-vCPU concurrency for one bounded scenario; it is not
  a claim that every interleaving or long-duration leak is excluded.
- Long-blocking waits beyond the test scenarios, cross-boot volatile Handles
  (not an ABI feature), and GE capability revocation semantics beyond Handle
  close were not tested.
- Subscription class bits were tested as no-amplification masks, not as every
  possible provider/rights combination.
- Global queue saturation was tested exactly at 65,536 with 64 subscriptions;
  larger FIFO counts and other quota combinations were not run.
- **Follow-up:** subscription close while a receive is already blocked is NOT
  RUN; section 5A.5 explains the source-level reference ordering and why the
  path is unreachable through the supported process-local/non-transferable
  subscription contract. Direct observation of kernel sleeper state is not
  available, so `blocked-settle` uses a bounded no-result window plus the exact
  timeout and hangup errors rather than a kernel-internal probe.
- **Follow-up:** T8 and T10 regression boots and canonical rebuilds were not
  rerun because this follow-up changed no kernel, ABI or build input; it used
  the same artifact hashes as the tested T11 kernel. Their 2026-11-06 PASS
  results remain the prior evidence, not a claim about this follow-up.

## 12. Changed files

**2026-11-06 round:**

- `kernel/core/sysobj.c` — infinite-timeout sentinel handling for
  `ObjOpRecv` only.
- `docs/SYSCALL-T11-OBJECTFIFO.md`, `docs/SYSCALL-T11-OBJECTFIFO.json` — this
  result and machine-readable evidence.
- `docs/STATUS.md`, `docs/ROADMAP-T8-T16.md` — T11 status from evidence.
- `docs/VOLATILE-OBJECT-OBJECTFIFO.md` — corrected Handle limit (96 observed)
  and precise subscription-rights policy; no historical result rewritten.
- `tools/host/t11/` — reproducible test orchestrators and bootstrap generator.

**2026-11-07 follow-up (no kernel source change):**

- `tools/host/t11/boot-b-orch.py` — `blocked-settle`/exact `Ehungup` and exact
  reaped PID/status assertions.
- `tools/host/t11/boot-c-orch.py` — real full-width Handle denial with an
  authorized control, exact `Enomem`, witness queue snapshots and unchanged
  counters.
- `docs/SYSCALL-T11-OBJECTFIFO.md`, `docs/SYSCALL-T11-OBJECTFIFO.json` —
  follow-up evidence and the NOT RUN subscription-close analysis.
- `docs/STATUS.md`, `docs/ROADMAP-T8-T16.md` — current T11 status.
- Local, non-published discrimination harness under `build/t11/mut/` and run
  logs under `build/t11/logs/` (build artifacts, not canonical test inputs).

## 13. Status statement

T11 is **PASS**: the existing universal ObjectFIFO carried immutable object
references among real independent MicroPython processes, with independent
bounded subscription queues, TEE/fanout, observed drop-oldest counters,
attenuated rights, no authority amplification, defined revoke/close/exit
teardown, bounded resources and a disclosed and tested global-quota failure
mode. The only defect fixed was the infinite-timeout sentinel handling. The
2026-11-07 follow-up strengthens four previously weak assertions (exact
`Ehungup`, real full-width Handle denial with a positive control, exact reaped
PID/status, exact `Enomem` with unchanged witness queues), reruns 70/70 checks
PASS, and marks subscription-close-while-blocked NOT RUN as unreachable through
the supported process-local contract; it changes no kernel code. T12 is not
started.
