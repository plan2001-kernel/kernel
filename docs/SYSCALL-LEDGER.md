# Plan 2001-kernel syscall ledger (T9)

> Historical measurement: the current branch reduces the dispatch table to
> 33 active / 102 reserved. See [SYSCALL-CONSOLIDATION](SYSCALL-CONSOLIDATION.md).


**Status: T9 PASS (documentation and measurement only), 2026-11-06.**
This is the source-derived ledger required by the then-current T9 roadmap
(since removed).
T10 was subsequently implemented separately (PASS); T11–T16 remain
**PLANNED**. No syscall, object operation, ABI value, kernel source, userspace
implementation, build script or test was changed to produce this document.

> **T10 update (2026-11-06):** after this T9 measurement, libc `nsec()` was
> changed to call direct `_NSEC(53)` and the unused libc
> `obj_process_self_time()` adapter was removed. The kernel `ObjOpTimeNow=27`
> provider was retained. No syscall number, Object operation value or dispatch
> entry changed. The entries below remain the T9 snapshot measurement; see
> [SYSCALL-T10-CLEANUP](SYSCALL-T10-CLEANUP.md) for the current time and WAIT
> decisions.

- **Host:** `kuha`, Linux 6.12.111+deb13-amd64, GCC 14.2.0, QEMU 10.0.13 (KVM
  used by the cited runs).
- **Measured revision (input):** published source snapshot
  `2b3e6126f2d195fc3c3d8d89f9410fd228ec87ff0adbce7880d5a955e7d62253`
  (2026-11-06T18:27:56Z). At the start of T9, the pre-edit public tree
  recomputed to exactly this id. Adding this ledger and updating STATUS/ROADMAP
  changes the published tree id; those are documentation-only changes and do
  not alter the measured syscall source or binaries.
- **Canonical artifacts inspected:** `build/gcc/9pc64.elf`
  `072d2c5a…9081261`, `build/gcc/micropython.elf`
  `bc9df483…e11d653`, `build/gcc/gen/obootoid.elf`
  `10c63c33…a13769`. These are the T8 final artifacts recorded in
  [SYSCALL-T8-MULTIPROCESS](SYSCALL-T8-MULTIPROCESS.md).
- **Counts measured here:** **135 named / 62 active / 73 reserved.** The
  historical target 135/62/73 is a coincidence of this measurement, not a
  quota; the derivation is in §1.

## 0. Scope, evidence vocabulary and ABI conventions

### 0.1 What T9 measured and what it did not

T9 re-derived the inventory from the measured source and checked the linked
user binaries with `objdump`/`nm`. It used existing, already-supported runtime
evidence; it did **not** rebuild the kernel/MicroPython, did not add tests and
did not run hardware.

Evidence labels used in the entries:

- **STATIC:** direct `call`/tail-`jmp` in a measured ELF, source callsite, or
  an authoritative dispatch/provider lookup.
- **RUNTIME:** observed in an existing supported QEMU trace/test log.
- **HISTORICAL:** reported by an older round (T0–T8) and not re-run by T9.
- **NOT OBSERVED:** absent from the cited runtime trace. This is not proof of
  non-use.
- **NOT TESTED:** no existing test that exercises the path was found.
- **PROVABLY UNREFERENCED (in this tree):** no source callsite and no direct
  call in the measured `micropython.elf` or `obootoid.elf`. This is scoped to
  the current PoC tree and measured binaries; it is not a claim that the
  historical ABI number may be reused or removed without T10 review.

### 0.2 Dispatch, arguments, return and error encoding

- Dispatch is `systab[]` in the generated
  `build/gcc/gen/systab-gcc.h`, produced from `runtime/abi/sys.h` by
  `tools/host/build-kernel.py`. A non-`nil` entry is **active**; a `nil` entry
  with `sysreserved[nr]=1` is **reserved** and fails with `Ebadarg` without
  reaching a handler. `dosyscall()` enforces the table and the reserved rule
  (`kernel/core/sysproc.c:2260`, especially 2288–2310).
- The a.out AMD64 entry passes syscall number in `RBP` and at most
  `MAXSYSARG=5` 64-bit argument slots on the user stack
  (`kernel/amd64/dat.h:27`, `kernel/core/portdat.h:114`). `sysva_arg` returns
  pointers, `uvlong` and other default types as 64-bit; `long` and
  `unsigned long` are narrowed to 32 bits (`kernel/core/portdat.h:140-161`).
  A handler that reads beyond five slots sets `sa.bad`, and `dosyscall()`
  raises `Ebadarg`.
- Normal success returns the handler's `uintptr` value. A kernel `error()`
  unwinds to `dosyscall()`, which writes `-1` to the return register and
  swaps the current error text into `errstr`
  (`kernel/core/sysproc.c:2320-2338`). The exception is `NOTED(29)`: a
  successful note return uses `NOTED_RET`, restores the saved `Ureg` through
  `lentry.S`, and deliberately skips the ordinary return write
  (`kernel/core/sysproc.c:2260-2325`; `kernel/amd64/trap.c:425-445`).
- Pointers are user virtual addresses validated by `validaddr`/`okaddr`
  (read or write direction), `evenaddr` for aligned words, and bounded
  lengths in each handler. Handle values are process-local; a stale
  generation/slot fails `Ebadarg`.

### 0.3 Categories

Entries use the roadmap categories: **direct process-local**,
**object/Space/identity**, **resource/device**, **bootstrap dependency**,
**temporary compatibility**, **truly unused (in this tree)**, or
**unresolved**.

## 1. Inventory derivation

Commands were run in `/home/lapere/Src/Plan2001-O` on 2026-11-06. The
snapshot id was reproduced without publishing by using the deterministic
public-tree staging code in `tools/host/mcp-publish.py` against the classified
public file list before this document was added; the result was exactly
`2b3e6126f2d195fc3c3d8d89f9410fd228ec87ff0adbce7880d5a955e7d62253`.

Inventory command (the same method used for the tables below):

```sh
python3 - <<'PY'
import re
h=open('runtime/abi/sys.h').read()
named={int(n):k for k,n in re.findall(r'#define\s+(\w+)\s+(\d+)',h)}
t=open('build/gcc/gen/systab-gcc.h').read()
st=t[t.index('Syscall *systab[]'):t.index('char *sysctab[]')]
active=[k for k,v in re.findall(r'\[(\w+)\]\s*(\w+),',st) if v!='nil']
rs=t[t.index('char sysreserved[]'):t.index('int nsyscall')]
reserved=re.findall(r'\[(\w+)\]\s*1,',rs)
print('named',len(named),'active',len(active),'reserved',len(reserved))
print('gaps',[i for i in range(0,141) if i not in named])
PY
```

Result:

```text
named 135 active 62 reserved 73
gaps [48, 49, 94, 95, 114, 115]
```

Numbers 48, 49, 94, 95, 114 and 115 are not defined, not reserved and are not
syscalls. All 135 defined names have exactly one `systab` slot; 62 are non-nil
and 73 are nil with `sysreserved=1`.

### 1.1 Current active numbers

`0 SYSR1, 1 _ERRSTR, 6 ALARM, 8 EXITS, 9 _FSESSION, 10 FAUTH, 12 SEGBRK,
17 SLEEP, 19 RFORK, 24 BRK_, 28 NOTIFY, 29 NOTED, 30 SEGATTACH,
31 SEGDETACH, 32 SEGFREE, 33 SEGFLUSH, 34 RENDEZVOUS, 37 SEMACQUIRE,
38 SEMRELEASE, 40 FVERSION, 41 ERRSTR, 52 TSEMACQUIRE, 53 _NSEC,
54 OVOLOPEN, 55 OSPACEOPEN, 56 OSPACEGET, 57 OSPACEPUT, 58 OSPACECOMMIT,
59 OSPACECLOSE, 60 OEXECUTE, 61 OSPACECREATE, 63 USBOPEN, 64 USBLIST,
65 USBEPOPEN, 66 USBREAD, 67 USBWRITE, 68 USBIOCTL, 69 USBCLOSE,
70 USBEPNAME, 86 RTCGET, 87 RTCSET, 88 ENVGET, 89 ENVSET, 90 ENVDELETE,
91 ENVLIST, 93 OTERMINATE, 96 OBIND, 97 OLOOKUP, 98 OUNBIND,
99 OREVOKE, 101 ONSVIEW, 102 ONMOUNT, 103 ONUNMOUNT, 104 OBINDX,
105 ONSVISIBLE, 106 OQUERY, 107 SYSSNAPSHOT, 136 OBJCREATE,
137 OBJOPEN, 138 OBJCLOSE, 139 OBJBIND, 140 OBJINVOKE.`

### 1.2 Reserved numbers

`2 BIND, 3 CHDIR, 4 CLOSE, 5 DUP, 7 EXEC, 11 _FSTAT, 13 _MOUNT, 14 OPEN,
15 _READ, 16 OSEEK, 18 _STAT, 20 _WRITE, 21 PIPE, 22 CREATE, 23 FD2PATH,
25 REMOVE, 26 _WSTAT, 27 _FWSTAT, 35 UNMOUNT, 36 _WAIT, 39 SEEK,
42 STAT, 43 FSTAT, 44 WSTAT, 45 FWSTAT, 46 MOUNT, 47 AWAIT, 50 PREAD,
51 PWRITE, 62 IOWRITE, 71–85 PIPENEW…UARTCLOSE, 92 OSPAWN, 100 PIPEINHERIT,
108–113 TCP6LISTEN…TCP6CLOSE, 116–120 NET6ADDR…NET6CLOSE,
121–135 VOBJCREATE…VOBJACCEPT.`

Reserved numbers are not active implementations. `_WAIT(36)`,
`AWAIT(47)` and `OSPAWN(92)` are reserved because their supported paths are
`OBJINVOKE(ObjOpWait)` and `OBJINVOKE(ObjOpOspawn)` respectively
(`runtime/abi/sys.h:45-59,127-131`). Reserved numbers must not be conflated
with the active `OBJ*` syscalls or with an Object operation carried by
`OBJINVOKE`.

### 1.3 Historical count discrepancies

| Document | Stated table | Explanation |
|---|---|---|
| [T0 measurement](SYSCALL-T0-MEASUREMENT.md) (snapshot `1236e5b9…`, 2026-11-05) | 135 named / **65 active / 70 reserved** | T0 preceded the T6 retirement. Its active set included `_WAIT(36)`, `AWAIT(47)`, `OSPAWN(92)`, `NOTIFY(28)`, `NOTED(29)`, `RFORK(19)` and `OTERMINATE(93)`. |
| [T6 process report](SYSCALL-T6-PROCESS.md) | 135 / **58 / 77** | T6 retired those seven process-lifecycle numbers to typed `OBJINVOKE` operations. |
| [T7 restore report](SYSCALL-SIMPLE-RESTORE.md) | 135 / **62 / 73** | T7 restored four of the seven (`NOTIFY`, `NOTED`, `RFORK`, `OTERMINATE`). `_WAIT`, `AWAIT` and `OSPAWN` remain reserved. |
| Then-current stage roadmap header (since removed) | 135 / 62 / 73 at snapshot `01a7c7a4…` | This is an older inspected baseline. The ledger measures `2b3e6126…`; the dispatch counts happen to be unchanged. |
| [T8 report](SYSCALL-T8-MULTIPROCESS.md) | 135 / 62 / 73 | T8 changed only console byte accounting, not syscall dispatch. |

There is also one source-comment discrepancy: `kernel/core/sysproc.c:1520`
still says “NOTIFY(28) is reserved” in the `setnotify()` comment, while
`runtime/abi/sys.h:31-34` and the current `systab` make `NOTIFY` active.
The measured dispatch table and the source comment disagree; the table is
authoritative for behavior and the comment is historical text, not an ABI
claim. T9 does not edit source.

## 2. Active syscall entries

Unless stated otherwise, “return 0” means success; all failures use the common
`-1` + `errstr` convention from §0.2. “Handle” means a process-local
generation-checked handle, not an OID.

### 2.1 Process, identity, notes, time, memory and segments

#### 0 — `SYSR1`
- **Implementation:** `sysr1` (`kernel/core/sysproc.c:20`); `systab[SYSR1]`.
- **Arguments:** none. **Return:** 0. **Error:** `Eperm` if `!iseve()`.
- **Authorization/lifetime:** host owner (`eve`) only; no state.
- **Callers/evidence:** no source callsite; no direct call in
  `micropython.elf` or `obootoid.elf`. **NOT TESTED.**
- **Disposition:** truly unused in this tree (historical privilege probe);
  unresolved whether T10 should retain the number.

#### 1 — `_ERRSTR`
- **Implementation:** `sys_errstr` (`kernel/core/sysproc.c:1512`); calls
  `generrstr(buf, 64)`.
- **Arguments:** `char *buf`; `generrstr()` requires a writable buffer of up
  to `ERRMAX` bytes and returns `Ebadarg` for zero length.
- **Return/error:** 0 on success; `-1` + swapped errstr on failure.
- **Authorization/lifetime:** none; operates only on the caller's error
  buffers.
- **Callers/evidence:** no source caller; no direct binary call. The measured
  MicroPython libc uses `errstr()` over `OBJINVOKE(ObjOpErrstrExchange)`
  (`runtime/libc/9sys/errstr.c:1-24`). **NOT TESTED.**
- **Disposition:** temporary compatibility (deprecated 64-byte alias);
  unobserved.

#### 6 — `ALARM`
- **Implementation:** `sysalarm` → `procalarm()` (`kernel/core/sysproc.c:1384`;
  `kernel/core/alarm.c:68`).
- **Arguments:** `ulong ms` (32-bit transport). **Return:** remaining
  milliseconds of the previous alarm, as `procalarm()` computes; 0 clears.
- **Authorization/lifetime:** process-local alarm slot; positive values arm a
  kernel alarm note and do not survive process exit.
- **Callers/evidence:** STATIC `p9_alarm` (`micropython/modplan9.c:53-54`);
  **NOT OBSERVED** in the T7/T8 traces. The T7 `onnote` test used a test-only
  alarm starter, not `ALARM(6)` (`SYSCALL-SIMPLE-RESTORE.md`).
- **Disposition:** direct process-local; unobserved.

#### 8 — `EXITS`
- **Implementation:** `sysexits` (`kernel/core/sysproc.c:1425`); reads the
  optional status string, validates it and calls `pexit(status, 1)`.
- **Arguments:** `char *status` (optional; read up to `ERRMAX`, bounded copy
  if no NUL). **Return:** does not return on success.
- **Authorization/lifetime:** process terminates; resources and handles are
  released by the normal exit path.
- **Callers/evidence:** STATIC `_exits` ← `exits()` (`runtime/libc/port/exits.c:7-10`),
  `micropython/main.c` usage exits, `p9_exits`, `p9_start`, `p9_console_exit`,
  `obootoid._main` (`kernel/amd64/obootoid.c:39-53`). RUNTIME T7 trace saw
  `EXITS(8)` once; T8 children exited normally.
- **Disposition:** direct process-local.

#### 9 — `_FSESSION`
- **Implementation:** `sys_fsession` (`kernel/core/auth.c:36`).
- **Arguments:** `int fd`, `char *str`, `uint len`; requires `len != 0` and a
  writable `str` of `len` bytes.
- **Behavior/return:** writes a NUL to `str[0]`, ignores `fd`, returns 0.
- **Authorization/lifetime:** none; compatibility stub.
- **Callers/evidence:** no source caller; no direct binary call. **NOT TESTED.**
- **Disposition:** temporary compatibility; unobserved.

#### 10 — `FAUTH`
- **Implementation:** `sysfauth` (`kernel/core/auth.c:60`). It immediately
  raises `"fauth: 9P mount transport not supported"` without reading a fd or
  user pointer.
- **Arguments:** none consumed. **Return/error:** always error.
- **Authorization/lifetime:** no state; the canonical tree has no 9P mount
  transport.
- **Callers/evidence:** no source caller; no direct binary call. **NOT TESTED.**
- **Disposition:** temporary compatibility (explicitly unsupported).

#### 12 — `SEGBRK`
- **Implementation:** `syssegbrk` (`kernel/core/sysproc.c:1622`).
- **Arguments:** `uintptr addr` used to select a segment, then a second
  `uintptr` passed to `ibrk(..., segment index)`.
- **Validation:** `addr` must fall inside a non-text/data/stack/physical/
  fixed/sticky segment; otherwise `Ebadarg`. `ibrk()` performs the break
  validation.
- **Return/error:** `ibrk()` result; errors use the common convention.
- **Authorization/lifetime:** process-local segment table.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 17 — `SLEEP`
- **Implementation:** `syssleep` → `kernelsleep()` (`kernel/core/sysproc.c:1377`;
  core at 1364).
- **Arguments:** `long ms` (32-bit). `ms<=0` yields (EDF-preferred if
  admitted); positive waits up to `ms` milliseconds.
- **Return/error:** 0; no error path except interruption unwinding.
- **Authorization/lifetime:** process-local scheduler state.
- **Callers/evidence:** STATIC `p9_sleep`, `main`, `lock`
  (`micropython/modplan9.c:146-149`; `micropython/main.c:173` etc.;
  `runtime/libc/port/lock.c:17-27`). RUNTIME T7 trace saw `SLEEP(17)` four
  times; T4a functional sleep test is historical.
- **Disposition:** direct process-local.

#### 19 — `RFORK`
- **Implementation:** `sysrfork` → `kernrfork()` (`kernel/core/sysproc.c:1413`;
  core at 41).
- **Arguments:** `long flags` (32-bit). It validates incompatible
  `RFFDG|RFCFDG`, `RFNAMEG|RFCNAMEG`, `RFENVG|RFCENVG`, absent resources, and
  `RFMEM|RFNOWAIT` without `RFPROC`; it copies/duplicates the selected groups
  and segments.
- **Return/error:** child PID when `RFPROC` is set (parent path); 0 in the
  group-only form; `Ebadarg`/`Enovmem`/`Eperm` as applicable.
- **Authorization/lifetime:** process-local; child gets its own native handle
  table and no implicit output grant; only explicit Pipe endpoints cross.
- **Callers/evidence:** STATIC `p9_rfork` (`micropython/modplan9.c:132-133`).
  RUNTIME T7 trace saw `RFORK(19)` twice and the functional
  `RFORK`/`EXITS`/`WAIT` test passed; T6 negative flags also passed before T7.
- **Disposition:** direct process-local.

#### 24 — `BRK_`
- **Implementation:** `sysbrk_` → `ibrk(addr, BSEG)` (`kernel/core/sysproc.c:1750`).
- **Arguments:** `uintptr addr` (64-bit).
- **Return/error:** 0 on success; `-1` with errstr on invalid break or memory
  failure. Libc rounds to 8 bytes and preserves `bloc` only on success.
- **Authorization/lifetime:** process-local BSEG; no capability.
- **Callers/evidence:** STATIC `sbrk()`/`brk()` (`runtime/libc/9sys/sbrk.c:14-36`).
  RUNTIME T7 trace saw `BRK_(24)` eight times; T3/T4 and normal boot logs
  exercised allocation historically.
- **Disposition:** direct process-local.

#### 28 — `NOTIFY`
- **Implementation:** `sysnotify` → `setnotify()` (`kernel/core/sysproc.c:1399`;
  core at 1522).
- **Arguments:** `uintptr fn`; nil clears, otherwise the address must be a
  readable user function entry of at least pointer size.
- **Return/error:** 0; `Ebadarg`/fault on a bad address.
- **Authorization/lifetime:** process-local notify callback; no note queue or
  ureg change here.
- **Callers/evidence:** STATIC `atnotify()` (`runtime/libc/port/atnotify.c:23-53`)
  and MicroPython `onnote` path. RUNTIME T7 trace saw `NOTIFY(28)` three times;
  T7 functional `NOTIFY-REG`/`NOTIFY-NIL` passed.
- **Disposition:** direct process-local.

#### 29 — `NOTED`
- **Implementation:** `sysnoted` → `notecont()` (`kernel/core/sysproc.c:1406`;
  core at 1574). On success returns `NOTED_RET` only to `dosyscall()`, which
  restores the saved `Ureg` and skips the ordinary return write.
- **Arguments:** `int arg` (`NCONT`, `NDFLT`, `NSAVE`, `NRSTR`; see note ABI).
- **Validation/error:** requires an active note unless `NRSTR`; bad state or
  bad `Ureg` raises `Ebadarg`/suicide exactly as the historical path.
- **Authorization/lifetime:** process-local note state; successful return
  resumes the saved user context.
- **Callers/evidence:** STATIC `noted` ← `atnotify`, `p9_notify_probe`,
  `p9_notify_probe`/notifier (`micropython/modplan9.c:815`);
  `runtime/libc/port/atnotify.c:16-19`. RUNTIME T7 trace saw `NOTED(29)` twice;
  T7 `NOTE-COUNT` and the real `onnote` test passed.
- **Disposition:** direct process-local (special return ABI).

#### 30 — `SEGATTACH`
- **Implementation:** `syssegattach` (`kernel/core/sysproc.c:1649`); validates
  the name with `validaddr` + `validnamedup`, then calls
  `segattach(attr, name, va, len)` (`kernel/core/segment.c:485`).
- **Arguments:** `int attr`, `char *name`, `uintptr va`, `ulong len`.
- **Validation/error:** name must be readable; `segattach` rejects
  `va >= USTKTOP`, zero length, overlaps, no segment slot and memory budget
  failures (`Ebadarg`, `Esoverlap`, `Enovmem`).
- **Return/error:** segment base address; errors common convention.
- **Authorization/lifetime:** process-local segment table; the segment is
  released by `putseg()` on detach/exit.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 31 — `SEGDETACH`
- **Implementation:** `syssegdetach` (`kernel/core/sysproc.c:1673`).
- **Arguments:** `uintptr addr`.
- **Validation/error:** address must name a loaded segment; the initial stack
  segment (`SSEG`) is rejected (`Ebadarg`).
- **Return/error:** 0; frees/decrements the segment and flushes the MMU.
- **Authorization/lifetime:** process-local segment table.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 32 — `SEGFREE`
- **Implementation:** `syssegfree` (`kernel/core/sysproc.c:1719`).
- **Arguments:** `uintptr from`, `ulong len`; the handler computes `to`.
- **Validation/error:** rejects `to < from`, a missing segment, or `to` beyond
  the segment top (`Ebadarg`); rounds inward to pages.
- **Return/error:** 0 (including a fully rounded-empty range); frees pages and
  flushes the MMU.
- **Authorization/lifetime:** process-local segment.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 33 — `SEGFLUSH`
- **Implementation:** `syssegflush` (`kernel/core/segment.c:643`) → `segflush()`
  (same file, 596) and `flushmmu()`.
- **Arguments:** `void *va`, `ulong len`.
- **Validation/error:** `segflush()` rejects overflow and any address outside
  a segment (`Ebadarg`).
- **Return/error:** 0; marks pages for text flush and invalidates the MMU.
- **Authorization/lifetime:** process-local segment.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 34 — `RENDEZVOUS`
- **Implementation:** `sysrendezvous` (`kernel/core/sysproc.c:1756`).
- **Arguments:** `uintptr tag`, `uintptr new`.
- **Behavior:** looks up the caller's rendezvous group; if another process is
  waiting on `tag`, removes it, returns its old value and readies it;
  otherwise records `tag`/`new`, sleeps and later returns the peer's value.
- **Error:** `Ebadarg` if the process has no rendezvous group.
- **Authorization/lifetime:** process-local rendezvous group; entry is removed
  before return.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 37 — `SEMACQUIRE`
- **Implementation:** `syssemacquire` (`kernel/core/sysproc.c:2026`).
- **Arguments:** `long *addr`, `int block`.
- **Validation/error:** `addr` must be even, writable, 8 bytes, inside a
  non-read-only segment; value must be non-negative (`Ebadarg`).
- **Return/error:** 1 if acquired, 0 if `block==0` and unavailable; blocking
  and interruption follow `semacquire()`.
- **Authorization/lifetime:** process-local segment semaphore.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 38 — `SEMRELEASE`
- **Implementation:** `syssemrelease` (`kernel/core/sysproc.c:2066`).
- **Arguments:** `long *addr`, `long delta`.
- **Validation/error:** same alignment/writable/segment checks; `delta < 0`,
  negative current value (`Ebadarg`). `delta==0` is a no-op.
- **Return/error:** new semaphore value; wakes `delta` waiters.
- **Authorization/lifetime:** process-local segment semaphore.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 40 — `FVERSION`
- **Implementation:** `sysfversion` (`kernel/core/auth.c:29`). It immediately
  raises `"fversion: 9P mount transport not supported"` without reading
  arguments or taking a `Chan` reference.
- **Arguments:** none consumed. **Return/error:** always error.
- **Authorization/lifetime:** none; no 9P mount transport exists in the
  canonical tree.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** temporary compatibility (explicitly unsupported).

#### 41 — `ERRSTR`
- **Implementation:** `syserrstr` → `generrstr(buf, len)`
  (`kernel/core/sysproc.c:1500`; core at 1479).
- **Arguments:** `char *buf`, `uint len` (32-bit, clamped to `ERRMAX`; zero is
  `Ebadarg`).
- **Return/error:** 0; swaps the current and syscall error buffers, validating
  the output buffer as writable.
- **Authorization/lifetime:** caller's own error buffers only.
- **Callers/evidence:** the generated MicroPython stubs deliberately omit a
  trap stub for `ERRSTR`; libc `errstr()` uses `ObjOpErrstrExchange`
  (`tools/host/build-mpy-gcc.py:141-145`;
  `runtime/libc/9sys/errstr.c:1-24`). No direct `ERRSTR(41)` trap caller was
  found. T4b measured `ERRSTR=0` historically.
- **Disposition:** temporary compatibility; direct trap path unobserved.

#### 52 — `TSEMACQUIRE`
- **Implementation:** `systsemacquire` (`kernel/core/sysproc.c:2046`).
- **Arguments:** `long *addr`, `ulong ms` (32-bit).
- **Validation/error:** same address/alignment/writable/segment and
  non-negative checks as `SEMACQUIRE`; `ms==0` means no wait.
- **Return/error:** 1 acquired, 0 timeout/unavailable; errors common.
- **Authorization/lifetime:** process-local segment semaphore.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local; truly unused in this tree.

#### 53 — `_NSEC`
- **Implementation:** `sys_nsec` (`kernel/core/sysproc.c:2087`). On this
  64-bit target it returns `todget(nil,nil)` directly in the return register
  and reads no user pointer; the 32-bit branch would take a `vlong *` pointer.
- **Return/error:** 64-bit nanoseconds; no error path.
- **Authorization/lifetime:** none.
- **Callers/evidence (T9 snapshot):** no direct binary call to `_nsec`; libc
  `nsec()` used `OBJOPEN(ObjOpenProcessSelf)` + `OBJINVOKE(ObjOpTimeNow)` +
  `OBJCLOSE` (`runtime/libc/9sys/nsec.c:1-18`;
  `runtime/libc/port/oobject.c:68-86`). T4a measured `_NSEC=0` on the migrated
  path; the stub symbol remained linked.
- **T10 update:** libc `nsec()` and `plan9.time_ns()` now call `_NSEC(53)`
  directly; `obj_process_self_time()` was removed. The call is observed
  statically in the T10 `micropython.elf` (`nsec` jumps to `_nsec`, which uses
  syscall number 53), and the REPL time test passed.
- **Disposition after T10:** direct process-local time source.

### 2.2 GE/O Space and bootstrap paths

#### 54 — `OVOLOPEN`
- **Implementation:** `sysovolopen` (`ge/storage/sysgefs.c:288`).
- **Arguments:** `char *spec`, `int n`; `1 <= n <= 256`, readable spec.
- **Behavior/return:** the first successful call opens a `PARTUUID=` volume,
  initializes GE, records `bootowner`/`bootpid`, and returns 1; later calls
  succeed only for that boot owner (otherwise `Eperm`). Invalid spec,
  non-`PARTUUID=` form or malformed UUID bytes fail `Ebadarg`.
- **Authorization/lifetime:** first caller becomes bootstrap owner; volume
  state is global and remains open. No automatic per-process handle.
- **Callers/evidence:** STATIC `obootoid._main` (`kernel/amd64/obootoid.c:39`)
  and `p9_space_o` (`micropython/p9space.c:437`). RUNTIME T8 boot log shows
  `o: O volume ... opened without FS` and `OEXECUTE`; T7 trace saw
  `OVOLOPEN(54)` twice.
- **Disposition:** bootstrap dependency.

#### 55 — `OSPACEOPEN`
- **Implementation:** `sysospaceopen` (`ge/storage/sysgefs.c:529`) →
  `osopenspace()` (504) → `oinstall()` (393).
- **Arguments:** `int h`, `char *name`, `int n`; requires `h==1`,
  `0<n<256`, readable name with no embedded NUL, and `oboot()` authority.
- **Return/error:** installed positive handle; `Eperm` for non-bootstrap or
  missing volume, `Ebadarg` for bad name/handle.
- **Authorization/lifetime:** bootstrap-only Space open; handle is
  process-local and refcounted until `OSPACECLOSE`/exit.
- **Callers/evidence:** STATIC `obootoid` (`kernel/amd64/obootoid.c:41`),
  `ospace_open` (`micropython/p9space.c:728`), `space_open_handle` (1667).
  RUNTIME T7 trace saw `OSPACEOPEN(55)` twice; T8 boot succeeded.
- **Disposition:** bootstrap dependency.

#### 56 — `OSPACEGET`
- **Implementation:** `sysospaceget` (`ge/storage/sysgefs.c:546`).
- **Arguments:** `int h`, `uvlong oid`, `char *buf`, `long n`, `uvlong *ver`.
- **Validation:** `ogetspace(h)`; authorizes `Oread` on `oid`; `0<=n<=64 MiB`;
  `buf` writable when `n>0`; `ver` writable when non-nil.
- **Return/error:** bytes read; provider errors; `ver` receives the version.
  No implicit commit.
- **Authorization/lifetime:** process-local Space handle, read authority; no
  handle transfer.
- **Callers/evidence:** STATIC loader fallback `obootoid._main`
  (`kernel/amd64/obootoid.c:29,51`); current MicroPython read path uses
  `OBJOPEN(ObjOpenGeSpace)` + `OBJINVOKE(ObjOpRead)` instead
  (`micropython/p9space.c:1582-1605`). **NOT OBSERVED** in the T7 trace; T0
  observed the older direct path 52 times. T8's successful path did not need
  the loader fallback.
- **Disposition:** object/Space/identity (bootstrap fallback and retained
  direct Space API); unobserved on the current read path.

#### 57 — `OSPACEPUT`
- **Implementation:** `sysospaceput` (`ge/storage/sysgefs.c:583`).
- **Arguments:** `int h`, `uvlong oid`, `char *buf`, `long n`.
- **Validation:** authorizes `Owrite` on `oid`; `0<=n<=64 MiB`; `buf` readable
  when `n>0`.
- **Return/error:** 0; provider write errors. It stages into the Space; no
  commit.
- **Authorization/lifetime:** process-local Space write capability.
- **Callers/evidence:** STATIC `space_hput` (`micropython/p9space.c:1607-1612`);
  normal persistent writes use `OBJINVOKE(ObjOpPutOid)`.
  **NOT OBSERVED** in T7/T8 traces; T0 observed it before the T2 migration.
- **Disposition:** object/Space/identity (legacy direct Space API);
  unobserved.

#### 58 — `OSPACECOMMIT`
- **Implementation:** `sysospacecommit` (`ge/storage/sysgefs.c:653`).
- **Arguments:** `int h`, `uvlong *ver` (optional; writable if non-nil).
- **Validation:** authorizes `Ocommit` on the Space handle's own OID.
- **Return/error:** 0; `ver` receives the committed version; provider errors.
- **Authorization/lifetime:** process-local Space commit authority.
- **Callers/evidence:** STATIC `space_hcommit`
  (`micropython/p9space.c:1614-1618`); normal commit uses
  `OBJINVOKE(ObjOpCommit)`. **NOT OBSERVED** in T7/T8; T0 observed older
  direct commits.
- **Disposition:** object/Space/identity (legacy direct Space API);
  unobserved.

#### 59 — `OSPACECLOSE`
- **Implementation:** `sysospaceclose` (`ge/storage/sysgefs.c:681`).
- **Arguments:** `int h`; validates the slot and generation (`Ebadarg` for
  bad/stale handles).
- **Return/error:** 0; clears the slot and drops the handle reference.
- **Authorization/lifetime:** process-local handle lifetime; idempotence is
  not provided (second close is `Ebadarg`).
- **Callers/evidence:** STATIC `ospace_open` error paths and `space_hclose`
  (`micropython/p9space.c:734,741,1577-1579`). **NOT OBSERVED** in T7/T8;
  T0 recorded it as linked but unexecuted.
- **Disposition:** object/Space/identity; unobserved.

#### 60 — `OEXECUTE`
- **Implementation:** `sysoexecute` (`ge/storage/sysgefs.c:936`); reads a
  bounded published object via `osgetobject()` (max 8 MiB), then `execo()`
  (`kernel/core/sysproc.c:704`).
- **Arguments:** `int h`, `uvlong oid`, `char **argv`, `int argc`;
  `0<=argc<=32`, and `argv != nil` when `argc>0`.
- **Validation/error:** Space handle/`Oexecute` authority is checked by the
  read helper; bad arguments fail `Ebadarg`; missing OID/ELF errors surface
  from the provider/loader.
- **Return/error:** on success `execo()` replaces the process and does not
  return; failures use the common convention.
- **Authorization/lifetime:** bootstrap/loader path; the Space handle and
  loader state are consumed by in-place exec.
- **Callers/evidence:** STATIC `obootoid._main`
  (`kernel/amd64/obootoid.c:30,49`), `space_hexecute`
  (`micropython/p9space.c:1628-1631`). RUNTIME T7 trace and T8 boot log show
  `OEXECUTE oid=0x4f45584543000003`.
- **Disposition:** bootstrap dependency.

#### 61 — `OSPACECREATE`
- **Implementation:** `sysospacecreate` (`ge/storage/sysgefs.c:616`).
- **Arguments:** `int h`, `char *buf`, `long n`, `uvlong *oid`.
- **Validation:** authorizes `Ocreate` on the Space; `0<=n<=64 MiB`; `buf`
  readable when non-empty; `oid` required and writable.
- **Return/error:** 0 and the new OID; provider errors. No commit.
- **Authorization/lifetime:** process-local Space create authority.
- **Callers/evidence:** STATIC `space_hcreate`
  (`micropython/p9space.c:1620-1626`); normal creation uses
  `OBJINVOKE(ObjOpCreatePersist)`. **NOT OBSERVED** in T7/T8; T0 recorded it
  as class B.
- **Disposition:** object/Space/identity (legacy direct Space API);
  unobserved.

### 2.3 USB resource/device calls

The USB family is per-process handle based (`up->usbhandles`); service
handles are `USBSVC`, endpoint handles are `USBDATA`, and modes are
`USBOREAD/USBOWRITE/USBOrdwr` (`kernel/core/sysusb.c:13-27`,
`include/usbctl.h`).

#### 63 — `USBOPEN`
- **Implementation:** `sysusbopen` (`kernel/core/sysusb.c:204`).
- **Arguments:** none. **Return/error:** positive service handle; allocation
  failure raises `"too many USB handles"`/`Enomem`.
- **Authorization/lifetime:** no explicit user check in this handler; creates
  a process-local USB service handle that owns a reference.
- **Callers/evidence:** STATIC `p9_usbopen` (`micropython/modplan9.c:1011`).
  **NOT TESTED/OBSERVED** in T7/T8.
- **Disposition:** resource/device; unobserved.

#### 64 — `USBLIST`
- **Implementation:** `sysusblist` (`kernel/core/sysusb.c:226`).
- **Arguments:** `int h`, `void *buf`, `long n`.
- **Validation/error:** requires a `USBSVC` handle; `n<0` is `Ebadarg`;
  `n` is clamped to 256 records; `buf` writable for the copied records;
  count-only query is `buf==nil || n==0`.
- **Return/error:** total count for a size query, otherwise copied record
  count; provider `Eio` on failure.
- **Authorization/lifetime:** service handle; no endpoint handle created.
- **Callers/evidence:** STATIC `p9_usblist`, plus `p9_objusblist`
  (`micropython/modplan9.c:1013-1033`). **NOT TESTED/OBSERVED.**
- **Disposition:** resource/device; unobserved.

#### 65 — `USBEPOPEN`
- **Implementation:** `sysusbepopen` (`kernel/core/sysusb.c:277`).
- **Arguments:** `int h, devno, epno, kind, mode` (five ints).
- **Validation/error:** service handle; `kind` ∈ {`USBDATA`,`USBCTL`};
  `mode` ∈ {`USBOREAD`,`USBOWRITE`,`USBOrdwr`}; endpoint must exist
  (`Ebadarg`).
- **Return/error:** endpoint handle; allocation errors.
- **Authorization/lifetime:** service handle authority; returned endpoint
  handle is process-local and refcounted.
- **Callers/evidence:** STATIC `p9_usbepopen`
  (`micropython/modplan9.c:1034-1039`). **NOT TESTED/OBSERVED.**
- **Disposition:** resource/device; unobserved.

#### 66 — `USBREAD`
- **Implementation:** `sysusbread` (`kernel/core/sysusb.c:330`).
- **Arguments:** `int h`, `void *buf`, `long n`.
- **Validation/error:** requires a `USBDATA` handle with read/ordwr mode;
  `0<=n<=64 KiB`; `buf` writable; provider errors surface as errstr/`Eio`.
- **Return/error:** bytes read (bounded by `n`).
- **Authorization/lifetime:** endpoint handle; no mode change.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** resource/device; truly unused in this tree (the current
  endpoint provider has no data-path user).

#### 67 — `USBWRITE`
- **Implementation:** `sysusbwrite` (`kernel/core/sysusb.c:375`).
- **Arguments:** `int h`, `void *buf`, `long n`.
- **Validation/error:** `USBDATA` write/ordwr; `0<=n<=64 KiB`; `buf` readable;
  provider errors.
- **Return/error:** bytes written.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** resource/device; truly unused in this tree.

#### 68 — `USBIOCTL`
- **Implementation:** `sysusbioctl` (`kernel/core/sysusb.c:445`).
- **Arguments:** `int h`, `int req`, `void *arg`, `long narg`.
- **Validation/error:** handle kind/request matrix; `USBdebug..USBinfo`
  range; per-request minimum and `Usbioargmax=4096`; `arg` required when
  `narg>0`; buffer is copied in/out.
- **Return/error:** provider return value; errors as `Ebadarg`/errstr/`Eio`.
- **Authorization/lifetime:** service/control handle; no data endpoint.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** resource/device; truly unused in this tree.

#### 69 — `USBCLOSE`
- **Implementation:** `sysusbclose` (`kernel/core/sysusb.c:509`).
- **Arguments:** `int h`; requires positive slot/generation.
- **Return/error:** 0; clears the slot, bumps generation and drops the
  reference. Stale/double close is `Ebadarg`.
- **Authorization/lifetime:** process-local handle lifetime.
- **Callers/evidence:** STATIC `p9_usbclose`
  (`micropython/modplan9.c:1040-1041`). **NOT TESTED/OBSERVED.**
- **Disposition:** resource/device; unobserved.

#### 70 — `USBEPNAME`
- **Implementation:** `sysusbepname` (`kernel/core/sysusb.c:299`).
- **Arguments:** `int h`, `char *name`, `int kind`, `int mode`.
- **Validation/error:** service handle; kind/mode as `USBEPOPEN`; readable
  NUL-terminated name copied with `validnamedup`; endpoint lookup.
- **Return/error:** endpoint handle; `Ebadarg` for bad name/kind/mode or no
  match.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** resource/device; truly unused in this tree.

### 2.4 RTC, environment and diagnostic snapshot

#### 86 — `RTCGET`
- **Implementation:** `sysrtcget` (`kernel/core/sysn3.c:38`) →
  `rtcnativeget()` (`kernel/amd64/rtccore.c:295`).
- **Arguments:** `Rtcdate *u`; non-nil and writable for `sizeof(Rtcdate)`.
- **Return/error:** 0 and the copied calendar date; `Ebadarg` for a bad
  pointer, otherwise errstr/`Eio`.
- **Authorization/lifetime:** none beyond the clock hardware; no process
  state.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** resource/device; truly unused in this tree.

#### 87 — `RTCSET`
- **Implementation:** `sysrtcset` (`kernel/core/sysn3.c:52`) →
  `rtcnativeset()` (`kernel/amd64/rtccore.c:327`).
- **Arguments:** `Rtcdate *u`; non-nil and readable for `sizeof(Rtcdate)`.
- **Return/error:** 0 after a successful hardware set; `Eperm` unless
  `iseve()` (host owner), `Ebadarg` for bad pointer/date, or errstr.
- **Authorization/lifetime:** host owner only; changes the shared RTC.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** resource/device; truly unused in this tree.

#### 88 — `ENVGET`
- **Implementation:** `sysenvget` (`kernel/core/sysn3.c:66`) →
  `envnativeget()` (`kernel/core/env/envcore.c:271`).
- **Arguments:** `int nname`, `char *uname`, `void *buf`, `long n`,
  `long *ulen`.
- **Validation/error:** name readable, `0<nname<128`, no embedded NUL;
  `0<=n<=512 KiB`; `buf` writable and `ulen` writable when used; missing name
  `Enonexist`, short buffer `Eshort`, too-large `Etoobig`.
- **Return/error:** 0 with value bytes and length; a size query is
  `buf==nil || n==0`.
- **Authorization/lifetime:** requires `up->egrp` (`Eperm` otherwise); reads
  the process environment group.
- **Callers/evidence:** STATIC `p9_envget`
  (`micropython/modplan9.c:138-144`). **NOT OBSERVED** in T7/T8 traces.
- **Disposition:** direct process-local (environment); unobserved.

#### 89 — `ENVSET`
- **Implementation:** `sysenvset` (`kernel/core/sysn3.c:123`) →
  `envnativeset()` (`kernel/core/env/envcore.c:302`).
- **Arguments:** `int nname`, `char *uname`, `void *uval`, `long n`.
- **Validation/error:** name as above; `0<=n<=512 KiB`; value readable when
  `n>0`; environment-group presence; allocation failures `Enomem`.
- **Return/error:** 0; no return value beyond status.
- **Authorization/lifetime:** process environment group; value lifetime is
  the group.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local (environment); truly unused in this
  tree.

#### 90 — `ENVDELETE`
- **Implementation:** `sysenvdelete` (`kernel/core/sysn3.c:161`) →
  `envnativedelete()` (`kernel/core/env/envcore.c:317`).
- **Arguments:** `int nname`, `char *uname`.
- **Validation/error:** name as above; missing name `Enonexist`; requires
  `up->egrp`.
- **Return/error:** 0.
- **Authorization/lifetime:** process environment group.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local (environment); truly unused in this
  tree.

#### 91 — `ENVLIST`
- **Implementation:** `sysenvlist` (`kernel/core/sysn3.c:180`) →
  `envnativelist()` (`kernel/core/env/envcore.c:367`).
- **Arguments:** `void *buf`, `long n`, `long *uneed`.
- **Validation/error:** `0<=n<=1 MiB`; `buf` writable and `uneed` writable;
  required size >1 MiB `Etoobig`; short buffer `Eshort`.
- **Return/error:** 0 with encoded list; size query when `buf==nil || n==0`.
- **Authorization/lifetime:** process environment group.
- **Callers/evidence:** no source caller; no direct binary call.
  **NOT TESTED.**
- **Disposition:** direct process-local (environment); truly unused in this
  tree.

#### 107 — `SYSSNAPSHOT`
- **Implementation:** `syssyssnapshot` (`kernel/core/sysinfo.c:127`).
- **Arguments:** `uvlong *user`; writable for 7 `uvlong` values.
- **Behavior:** writes version=1, `conf.nmach`, physical bytes, user bytes,
  GE/O volume capacity, TCP6 endpoint count and free physical bytes; in a
  `PLAN2001_SYSCALL_TRACE` build it also triggers the trace dump.
- **Return/error:** 0; `Ebadarg`/fault for a bad pointer.
- **Authorization/lifetime:** read-only diagnostic snapshot; no handles.
- **Callers/evidence:** STATIC `p9_system`
  (`micropython/modplan9.c:845-847`) and the trace checkpoint in
  `micropython/main.c:43-46`. RUNTIME used as the T7 trace checkpoint and in
  T0 measurement; not counted as application dependency when used as a
  checkpoint.
- **Disposition:** direct process-local (diagnostic).

### 2.5 Process termination and O namespace/identity calls

#### 93 — `OTERMINATE`
- **Implementation:** `sysoterminate` → `kernoterminate()`
  (`kernel/core/sysproc.c:1419`; core at 1321).
- **Arguments:** `int pid` (positive).
- **Validation/error:** rejects self, missing PID, kernel process, non-child
  (`Eperm`), exiting process; posts the `"terminated"` note.
- **Return/error:** 0; `Ebadarg`/`Eperm` as above.
- **Authorization/lifetime:** direct, live child only; no implicit PID
  capability.
- **Callers/evidence:** STATIC `space_terminate`
  (`micropython/p9space.c:1314`). RUNTIME T7 trace saw `OTERMINATE(93)` once;
  T7 functional test reaped the terminated child.
- **Disposition:** direct process-local (process lifecycle).

#### 96 — `OBIND`
- **Implementation:** `sysobind` → `obinding(list,0)`
  (`ge/storage/sysgefs.c:1036,985`).
- **Arguments:** `int h`, `uvlong oid`, `char *role`, `int n`, `int rights`.
- **Validation/error:** device-mask extension check (`onscanextend`);
  role ASCII 1..31 bytes; `rights>0` and subset of `Oall & ~Ocreate`;
  Space handle `Odelegate` authority; bootstrap grant or same-rights
  delegated rebind (`Ebadarg`/`Eperm`/`"O: no such object"`).
- **Return/error:** 0; binds a grant in `up->pgrp->ocontext`.
- **Authorization/lifetime:** process namespace; grant is refcounted and
  revocable.
- **Callers/evidence:** STATIC `space_obind`
  (`micropython/p9space.c:1549-1555`). **NOT OBSERVED** in T7/T8; NS3
  historical test output reports `NS3 BIND/BIND UNION` PASS
  (`docs/NS-TEST-OUTPUT.txt`), not re-run by T9.
- **Disposition:** object/Space/identity; current-runtime unobserved.

#### 97 — `OLOOKUP`
- **Implementation:** `sysolookup` (`ge/storage/sysgefs.c:1103`).
- **Arguments:** `char *name`, `int n`, `uvlong *info`.
- **Validation/error:** role ASCII 1..31; requires `up->pgrp`; `info`
  writable for three `uvlong`; missing role/provider errors.
- **Return/error:** installed handle; `info` = {sid, oid, rights}. It does not
  read object bytes (`oauthorize(...,0)`).
- **Authorization/lifetime:** process namespace lookup; returns a new
  refcounted handle.
- **Callers/evidence:** STATIC `space_olookup`
  (`micropython/p9space.c:1556-1566`). **NOT OBSERVED**; NS2/NS3 historical
  tests cover lookup/binding.
- **Disposition:** object/Space/identity.

#### 98 — `OUNBIND`
- **Implementation:** `sysounbind` (`ge/storage/sysgefs.c:1135`).
- **Arguments:** `char *name`, `int n`.
- **Validation/error:** role ASCII; requires `up->pgrp`.
- **Return/error:** 0; removes the binding from the process context.
- **Authorization/lifetime:** process namespace mutation.
- **Callers/evidence:** STATIC `space_ounbind`
  (`micropython/p9space.c:1567-1572`). **NOT OBSERVED**; NS3 historical
  unmount/unbind tests.
- **Disposition:** object/Space/identity.

#### 99 — `OREVOKE`
- **Implementation:** `sysorevoke` (`ge/storage/sysgefs.c:1148`).
- **Arguments:** `int h`.
- **Validation/error:** requires a Space/grant handle; non-bootstrap
  `Odelegate` authority; bootstrap handles are `Eperm`; marks origin/self
  revoked and closes the native transaction.
- **Return/error:** 0; revoked handles remain allocated but unusable.
- **Authorization/lifetime:** revokes a delegated capability and its lineage;
  does not free the process-local slot.
- **Callers/evidence:** STATIC `space_orevoke`
  (`micropython/p9space.c:1573-1576`). **NOT OBSERVED**; NS3 historical
  `REVOCATION` PASS.
- **Disposition:** object/Space/identity.

#### 101 — `ONSVIEW`
- **Implementation:** `sysonsview` (`ge/storage/sysgefs.c:1045`).
- **Arguments:** `char (*roles)[32]`, `int nr`.
- **Validation/error:** device-mask extension; `1<=nr<=32`; roles readable,
  NUL-terminated ASCII, non-empty and unique; each role must be authorized in
  the current context; namespace epoch must not change (`Ebadarg`/`Eperm`/
  `"O: namespace changed; retry"`).
- **Return/error:** new view handle; freezes only authorized roles.
- **Authorization/lifetime:** process-local view; attachments retain grants.
- **Callers/evidence:** STATIC `space_view`
  (`micropython/p9space.c:1640-1648`). **NOT OBSERVED**; NS2/NS3 historical
  view tests.
- **Disposition:** object/Space/identity.

#### 102 — `ONMOUNT`
- **Implementation:** `sysonmount` (`ge/storage/sysgefs.c:1074`).
- **Arguments:** `int handle`, `int mode` (`0..2`).
- **Validation/error:** device-mask extension; view provider required;
  refused if any grant/origin is revoked (`Eperm`).
- **Return/error:** attachment generation id; composes the view into the
  current namespace at the requested COPY/EXCLUSIVE/SHARE mode.
- **Authorization/lifetime:** process namespace; attachment id is later used
  by `ONUNMOUNT`.
- **Callers/evidence:** STATIC `space_mount`
  (`micropython/p9space.c:1650-1653`). **NOT OBSERVED**; NS3 historical
  MOUNT/UNION tests.
- **Disposition:** object/Space/identity.

#### 103 — `ONUNMOUNT`
- **Implementation:** `sysonunmount` (`ge/storage/sysgefs.c:1096`).
- **Arguments:** `uvlong id`.
- **Validation/error:** requires `up->pgrp` (`Eperm`); `octxunmount()`.
- **Return/error:** 0; removes the attachment.
- **Callers/evidence:** STATIC `space_unmount`
  (`micropython/p9space.c:1654-1657`). **NOT OBSERVED**; NS3 historical
  UNMOUNT PASS.
- **Disposition:** object/Space/identity.

#### 104 — `OBINDX`
- **Implementation:** `sysobindx` → `obinding(list,1)`
  (`ge/storage/sysgefs.c:1039,985`).
- **Arguments:** `int h`, `uvlong oid`, `char *role`, `int n`, `int rights`;
  the high bits of `rights` carry `mode`, the low 8 bits the rights.
- **Validation/error:** `mode` 0..2; rest identical to `OBIND`.
- **Return/error:** 0; extended binding.
- **Callers/evidence:** STATIC `space_bindx`
  (`micropython/p9space.c:1659-1662`). **NOT OBSERVED**; NS3 historical
  union/bind tests.
- **Disposition:** object/Space/identity.

#### 105 — `ONSVISIBLE`
- **Implementation:** `sysonsvisible` (`ge/storage/sysgefs.c:1267`).
- **Arguments:** `NsOutput *out`.
- **Validation/error:** snapshots the current namespace under the relation
  lock; validates `out`, capacity ≤ `GR_MAX_DATA`, and output buffer
  writability through `nsout()`; retries `"O: namespace changed; retry"` if
  the epoch moved.
- **Return/error:** number of visible object records; writes `NsOutput`
  `{count,buffer,capacity}` plus records.
- **Authorization/lifetime:** read-only view; never grants handles.
- **Callers/evidence:** STATIC `identity_arg`
  (`micropython/modge.c:11,29`). **NOT OBSERVED**; NS4 historical
  `NAMESPACE FILTER` PASS.
- **Disposition:** object/Space/identity.

#### 106 — `OQUERY`
- **Implementation:** `sysoquery` (`ge/storage/sysgefs.c:1370`), with a
  bootstrap sub-path `nrbootstrap()` at 1320.
- **Arguments:** `int op`, `NsIdentity *selector`, `char *text`, `int len`,
  `NsOutput *output`.
- **Validation/error:** op must be `NS_FIND..NS_REMOVE` (or `NS_BOOT` flag);
  `0<=len<=4096`; selector required except `NS_FIND`; readable text/selector;
  `nsout()` output checks; missing authority `Eperm`; relation/GE errors are
  mapped to errstr.
- **Return/error:** 0; results are delivered as an `NsOutput` record (FIND,
  GET, TAGS, CREATE, ADD, REMOVE). `NS_BOOT` requires boot identity and a
  local bootstrap Space handle.
- **Authorization/lifetime:** relation operations use the process namespace
  or the explicit NS_BOOT bootstrap authority; no raw DB handle escapes.
- **Callers/evidence:** STATIC `micropython/modge.c:52,56,67,89,101`.
  **NOT OBSERVED**; NS4 historical `RELATION QUERY`/`DIRECT READ DENIED`
  tests (not re-run).
- **Disposition:** object/Space/identity.

### 2.6 Universal five-operation object ABI

The five calls share per-process Vhand tables and provider checks in
`kernel/core/sysvobj.c`; `OBJINVOKE` is a router, not a family of extra
syscalls. Retired `ObjOp` codes 26, 28, 30, 31, 32, 34 and 36 have no switch
case and are rejected as `obj: operation not supported`.

#### 136 — `OBJCREATE`
- **Implementation:** `sysobjcreate` (`kernel/core/sysobj.c:35`).
- **Arguments:** `uvlong space`, `uvlong flags`, `void *buf`, `long n`,
  `ObjId *id`.
- **Validation/error:** unknown flag bits `Ebadarg`; kind mask
  (Bytes/Fifo/Ge/Pipe); Pipe requires `buf==nil && n==0`; Ge requires a
  `space` handle ≤ `0x7fffffff`; `id` optional but writable; size/kind
  provider bounds in `vobjcreat()`/`vobjcreatege()`.
- **Return/error:** new handle; optional `ObjId` identity; provider errors.
- **Authorization/lifetime:** process-local handle; Ge derives rights from the
  Space handle; close/exit releases.
- **Callers/evidence:** STATIC `obj_create`, `obj_pipe_create`
  (`runtime/libc/port/oobject.c`), called by MicroPython adapters. T7 trace
  did not record `OBJCREATE`; T0 recorded 0. T8 uses Pipe/Console object
  paths but did not count syscall numbers. **NOT OBSERVED as a numbered
  call in the cited traces.**
- **Disposition:** object/Space/identity; statically referenced but runtime
  count unresolved.

#### 137 — `OBJOPEN`
- **Implementation:** `sysobjopen` (`kernel/core/sysobj.c:79`).
- **Arguments:** `uvlong provider`, `void *req`, `long len`, `uvlong rights`,
  `ObjId *id`.
- **Validation/error:** rights subset `Vall`; provider-specific exact request
  size/layout and reserved fields; Handle/Binding names bounded; ProcessSelf
  and Kbd require empty requests; UART/KBD/USB provider authorization;
  unknown provider `"obj: provider not supported"`.
- **Return/error:** new handle; optional identity; provider errors.
- **Authorization/lifetime:** provider-specific capability attenuation; all
  returned handles are process-local and refcounted.
- **Callers/evidence (T9 snapshot):** STATIC `obj_open`, `obj_kbd_open`,
  `obj_process_self_time`, `obj_uart_open`, `obj_udp6_open`,
  `obj_usb_open`, `obj_wait` (`runtime/libc/port/oobject.c`). T10 removed the
  `obj_process_self_time` caller; `obj_wait` still opens ProcessSelf. RUNTIME T7
  trace saw 96 `OBJOPEN(137)` records; T8 console/pipe flows use it.
- **Disposition:** object/Space/identity.

#### 138 — `OBJCLOSE`
- **Implementation:** `sysobjclose` → `vobjclosehandle()`
  (`kernel/core/sysobj.c:169`; provider at `kernel/core/sysvobj.c:979`).
- **Arguments:** `int handle`; positive slot/generation required.
- **Return/error:** 0; drops one reference and invalidates the slot;
  stale/double close `Ebadarg`.
- **Authorization/lifetime:** process-local handle lifetime only.
- **Callers/evidence (T9 snapshot):** STATIC `obj_close`,
  `obj_process_self_time`, `obj_wait` (`runtime/libc/port/oobject.c`). T10
  removed the `obj_process_self_time` caller. RUNTIME T7 trace saw 92
  `OBJCLOSE(138)` records; T8 closes console/pipe/endpoint handles.
- **Disposition:** object/Space/identity.

#### 139 — `OBJBIND`
- **Implementation:** `sysobjbind` → `vobjbind()`
  (`kernel/core/sysobj.c:175`; provider at `kernel/core/sysvobj.c:2877`).
- **Arguments:** `int handle`, `char *role`, `long rolelen`, `uvlong rights`,
  `uvlong mode`.
- **Validation/error:** role 1..31 bytes; `mode` ≤3 bits plus remove; mode
  ≤2; Handle must carry `Vdelegate`; requires a process group; device-mask
  extension restriction; attenuation cannot exceed the source handle.
- **Return/error:** 0; binds an object grant into the process context.
- **Authorization/lifetime:** process namespace; object handle remains owned
  by the caller.
- **Callers/evidence:** STATIC `obj_bind` (`runtime/libc/port/oobject.c`),
  called by MicroPython `p9_objbind`. **NOT OBSERVED** in T0/T7 traces.
- **Disposition:** object/Space/identity; runtime unobserved.

#### 140 — `OBJINVOKE`
- **Implementation:** `sysobjinvoke` (`kernel/core/sysobj.c:192`).
- **Arguments:** `int handle`, `ObjCall *ucall`, `long calllen`; calllen must
  equal `sizeof(ObjCall)`; the structure is copied in/out.
- **Validation/error:** version must be `OBJ_ABI_VERSION=1`; flags/rights/
  reserved must be zero; each operation validates `in/inlen`, `out/outlen`,
  `arg0/arg1` and its provider/Handle rights. Unknown/retired op is
  `"obj: operation not supported"`.
- **Return/error:** operation result in both `ObjCall.result` and the syscall
  return; `aux` carries a secondary value (GE version, FIFO length, received
  bytes, etc.); provider errors use the common convention.
- **Authorization/lifetime:** authority is derived from the Handle, not the
  `rights` field; operations may block and may consume or create handles
  (PipePeer, UsbEpOpen, Ospawn) or perform I/O.
- **Callers/evidence:** STATIC 30 libc wrappers in
  `runtime/libc/port/oobject.c` (`obj_read`, `obj_write`, `obj_info`,
  `obj_list`, `obj_ospawn`, `obj_wait`, UDP6/UART/USB/Pipe adapters, etc.).
  RUNTIME T7 trace saw 4,829 `OBJINVOKE(140)` records; T8 exercised
  `ObjOpOspawn`/`ObjOpWait`, console and Pipe I/O.
- **Disposition:** object/Space/identity (the general object boundary).

Active `ObjOp` routing in `sysobjinvoke()`:

| code | operation | provider/effect |
|---:|---|---|
| 1 | `ObjOpInfo` | object info snapshot |
| 2 | `ObjOpRead` | bounded read; GE publishes version in `aux` |
| 3 | `ObjOpWrite` | bounded write |
| 4 | `ObjOpStat` | ObjectFIFO stat |
| 5 | `ObjOpSend` | immutable-reference FIFO send |
| 6 | `ObjOpRecv` | FIFO receive; `aux` = received count |
| 7 | `ObjOpSubscribe` | FIFO subscription |
| 8 | `ObjOpAccept` | TCP listener accept |
| 9 | `ObjOpGrant` | derive an attenuated handle |
| 10 | `ObjOpRevoke` | revoke a FIFO/handle lineage |
| 11 | `ObjOpList` | handle-table list |
| 12 | `ObjOpCreatePersist` | create a committed GE object |
| 13 | `ObjOpCommit` | commit a Space/GE transaction |
| 14 | `ObjOpSendTo` | UDP6 datagram send |
| 15 | `ObjOpRecvFrom` | UDP6 datagram receive |
| 16 | `ObjOpPipePeer` | install the peer Pipe endpoint |
| 17 | `ObjOpUartCtl` | bounded UART configuration |
| 18 | `ObjOpUartStatus` | read UART status bytes |
| 19 | `ObjOpKbdLeds` | privileged keyboard LED update |
| 20 | `ObjOpUsbList` | USB endpoint snapshot |
| 21 | `ObjOpUsbEpOpen` | open a USB endpoint handle |
| 22 | `ObjOpPipeInherit` | arm one-shot RFORK Pipe selection |
| 23 | `ObjOpUartCount` | local UART port count |
| 24 | `ObjOpNet6Addr` | IPv6 link-local lookup |
| 25 | `ObjOpPutOid` | replace one named OID without commit |
| 27 | `ObjOpTimeNow` | 64-bit `todget()` time over ProcessSelf |
| 29 | `ObjOpErrstrExchange` | swap caller error buffers (handle 0) |
| 33 | `ObjOpOspawn` | spawn a static ELF child from a Space/OID |
| 35 | `ObjOpWait` | wait for one child, typed `ObjWait` record |

Codes 26, 28, 30, 31, 32, 34 and 36 are retired and have no dispatch case;
they are not syscalls and must not be counted as active `ObjOp` operations.

## 3. Audit summary

### 3.1 Counts and evidence coverage

- **Named:** 135. **Active:** 62. **Reserved:** 73. **Undefined gaps:** 6
  (`48,49,94,95,114,115`).
- **Categorized active calls (non-overlapping; totals 62):**
  - direct process-local: 23 (`SYSR1`, `ALARM`, `EXITS`, `SLEEP`, `RFORK`,
    `BRK_`, `NOTIFY`, `NOTED`, `SEGBRK`, `SEGATTACH`, `SEGDETACH`,
    `SEGFREE`, `SEGFLUSH`, `RENDEZVOUS`, `SEMACQUIRE`, `SEMRELEASE`,
    `TSEMACQUIRE`, `ENVGET`, `ENVSET`, `ENVDELETE`, `ENVLIST`,
    `SYSSNAPSHOT`, `OTERMINATE`);
  - object/Space/identity: 20 (`OSPACEGET`, `OSPACEPUT`, `OSPACECOMMIT`,
    `OSPACECLOSE`, `OSPACECREATE`, `OBIND`, `OLOOKUP`, `OUNBIND`,
    `OREVOKE`, `ONSVIEW`, `ONMOUNT`, `ONUNMOUNT`, `OBINDX`, `ONSVISIBLE`,
    `OQUERY`, `OBJCREATE`, `OBJOPEN`, `OBJCLOSE`, `OBJBIND`, `OBJINVOKE`);
  - resource/device: 10 (`USBOPEN`, `USBLIST`, `USBEPOPEN`, `USBREAD`,
    `USBWRITE`, `USBIOCTL`, `USBCLOSE`, `USBEPNAME`, `RTCGET`, `RTCSET`);
  - bootstrap dependency: 3 (`OVOLOPEN`, `OSPACEOPEN`, `OEXECUTE`;
    `EXITS` is counted once under direct process-local);
  - temporary compatibility: 6 (`_ERRSTR`, `_FSESSION`, `FAUTH`,
    `FVERSION`, `_NSEC`, `ERRSTR`; the latter two keep direct handlers but
    their libc users are migrated);
  - `truly unused in this tree` is a cross-cutting disposition, not an
    additive category: 25 numbers have no source callsite and no direct call
    in the measured binaries: `0,1,9,10,12,30,31,32,33,34,37,38,40,41,52,
    53,66,67,68,70,86,87,89,90,91`. These numbers are retained active ABI;
    the disposition is evidence, not a removal decision.
- **Static coverage:** every active number has a `systab` handler and a
  source implementation. 37 of the 62 have at least one direct `call`/`jmp`
  in `micropython.elf`; the loader adds direct callers for `OVOLOPEN`,
  `OSPACEOPEN`, `OSPACEGET`, `OEXECUTE` and `EXITS`.
- **Runtime coverage (existing logs, not re-run by T9):** the T7 trace
  `build/syscall-simple/trace3/serial-boot.log` predates the current T8
  snapshot but has the same 62/73 dispatch table; T8 changed only console
  byte accounting in `kernel/core/sysvobj.c`, not syscall dispatch or handler
  semantics. It records active calls for:
  `8` (1), `17` (4), `19` (2), `24` (8), `28` (3), `29` (2), `54` (2),
  `55` (2), `60` (1), `93` (1), `137` (96), `138` (92), `140` (4829);
  `dropped=0`, no reserved/unknown traps. T8's current-snapshot runs
  (`build/t8/final-multiproc/`) show `OEXECUTE`, `OSPAWN`/`ObjOpOspawn`,
  `ObjOpWait`, console and Pipe activity, but were not syscall-trace builds.
  T0's historical trace covers the older direct GE/Space path and is used
  only as clearly labeled history.
- **Unobserved is not unused:** the 25 “truly unused in this tree” numbers
  are the only numbers with neither a source callsite nor a direct binary
  call. The remaining 37 are either observed, statically linked, or both.
  A number with a static caller but no runtime observation is not called
  unused.

### 3.2 High-value questions for T10

1. Should the direct `OSPACEGET/PUT/COMMIT/CREATE/CLOSE` family survive as
   the legacy Space API now that MicroPython uses typed `OBJ*` operations for
   the main persistent path, while the loader still needs a bounded fallback?
2. Should libc `nsec()` continue on the existing
   `OBJOPEN(ProcessSelf)` + `OBJINVOKE(ObjOpTimeNow)` + `OBJCLOSE` path, or
   call `_NSEC(53)` directly through `sys_nsec()`/`todget()`? Both are open
   T10 possibilities: the object path carries ProcessSelf's typed result and
   its shared authorization/lifetime model, while the direct path would avoid
   a per-call open/invoke/close. This is an unresolved analysis question; it
   does not decide to remove `_NSEC` or retire `ObjOpTimeNow`.
3. For the separate `WAIT` decision, should `obj_wait()` continue on the
   `OBJOPEN(ProcessSelf)` + `OBJINVOKE(ObjOpWait)` + `OBJCLOSE` path, or use a
   direct wait based on the existing `pwait()`? Compare typed results, error
   mapping, authorization (ProcessSelf capability versus process relationship)
   and handle/lease lifetime; if a direct wait cannot preserve them, the
   object path stands. This is also unresolved T10 analysis, not an approved
   change.
4. Should `ERRSTR(41)` be removed or reclassified as a compatibility alias
   once its libc users are confirmed to be entirely on
   `ObjOpErrstrExchange`?
5. Are `SEMACQUIRE`, `SEMRELEASE`, `TSEMACQUIRE`, `RENDEZVOUS`, `SEGATTACH`,
   `SEGDETACH`, `SEGFREE` and `SEGFLUSH` genuinely required by any supported
   PoC program, or are they only historical ABI surface? Current evidence is
   “no callsite, no runtime observation,” not proof of safe removal.
6. Should USB data/I/O calls (`USBREAD`, `USBWRITE`, `USBIOCTL`, `USBEPNAME`)
   remain active while the current userland exposes only service/endpoint
   metadata? Their providers are implemented, but no measured consumer uses
   them.
7. Should `SYSR1`, `_ERRSTR`, `_FSESSION`, `FAUTH` and `FVERSION` be retained
   as explicit compatibility/unsupported entry points, or should a later ABI
   round document them as permanently dead?
8. Should `OBJCREATE`/`OBJBIND` get a direct runtime count? T0 measured zero;
   T7 did not record them; T8 exercises them indirectly but has no
   per-syscall trace. Absence from those runs is not evidence of non-use.

### 3.3 Acceptance

Every active syscall at the measured revision appears in §2 with source
references, argument/return/permission details, actual or explicitly absent
callers, observed/not-observed test status and a disposition. The active,
named and reserved counts were re-derived and reconciled against older
documents. No source, build script, test or ABI was changed. Therefore T9
satisfies its documentation/measurement acceptance conditions on
`2b3e6126…`; T10–T16 remain PLANNED and are not claimed by this document.

## 4. Reproduction and artifact references

```sh
# Inventory and gaps (see §1)
python3 - <<'PY'
import re
h=open('runtime/abi/sys.h').read()
named={int(n):k for k,n in re.findall(r'#define\s+(\w+)\s+(\d+)',h)}
t=open('build/gcc/gen/systab-gcc.h').read()
st=t[t.index('Syscall *systab[]'):t.index('char *sysctab[]')]
active=[k for k,v in re.findall(r'\[(\w+)\]\s*(\w+),',st) if v!='nil']
rs=t[t.index('char sysreserved[]'):t.index('int nsyscall')]
reserved=re.findall(r'\[(\w+)\]\s*1,',rs)
print(len(named),len(active),len(reserved))
PY

# Static caller scan used for the entries
objdump -d --no-show-raw-insn build/gcc/micropython.elf
objdump -d --no-show-raw-insn build/gcc/gen/obootoid.elf

# Existing runtime evidence cited above (not re-run by T9)
sed -n '1,220p' build/syscall-simple/trace3/serial-boot.log
sed -n '1,260p' build/t8/final-multiproc/serial-boot.log
cat build/t8/final-multiproc/report.txt
```

Related documents: [STATUS](STATUS.md),
[T0 measurement](SYSCALL-T0-MEASUREMENT.md),
[T6 process report](SYSCALL-T6-PROCESS.md),
[T7 restore](SYSCALL-SIMPLE-RESTORE.md),
[T8 multi-process](SYSCALL-T8-MULTIPROCESS.md).
