# T10 — narrow semantic cleanup using the T9 ledger

**Status: PASS (2026-11-06), observed on `kuha`.** This round implements only
T10. T11–T16 remain PLANNED and are not started. No syscall number, Object
operation value or generated dispatch entry was added, removed, renumbered or
reused.

## Summary

The only semantic change is the time path. libc `nsec()` now calls direct
`_NSEC(53)` instead of opening `ProcessSelf`, invoking
`OBJINVOKE(ObjOpTimeNow=27)` and closing it. `plan9.time_ns()` uses the same
direct libc call. The redundant libc adapter `obj_process_self_time()` was
removed; the kernel `ObjOpTimeNow` provider is deliberately retained for raw
`OBJINVOKE` users.

`WAIT` remains on the typed object path. No other T9 candidate was changed.

## Measured ABI counts

| Measure | Before T10 | After T10 |
|---|---:|---:|
| Named syscalls | 135 | 135 |
| Active syscalls | 62 | 62 |
| Reserved syscalls | 73 | 73 |
| Active `ObjOp` providers | unchanged set | unchanged set; `ObjOpTimeNow=27` retained |
| Syscalls per libc `nsec()` call | 3 (`OBJOPEN` + `OBJINVOKE` + `OBJCLOSE`) | 1 (`_NSEC(53)`) |

The table was re-derived from `runtime/abi/sys.h` and the generated
`build/gcc/gen/systab-gcc.h` after the rebuild. The per-call reduction is
confirmed statically: in `build/gcc/micropython.elf`, `nsec` is a jump to
`_nsec`, and `_nsec` sets `%rbp` to `0x35` (53) before `syscall`. The symbol
`obj_process_self_time` is absent. No runtime syscall trace was built for T10;
the REPL test below observes the resulting time behavior, not per-entry
counters.

## Candidate decisions

### 1. Time / `_NSEC(53)` — changed

**Before:** `nsec()` (`runtime/libc/9sys/nsec.c`) called
`obj_process_self_time()` (`runtime/libc/port/oobject.c`), which opened the
caller's `VkindProcessSelf` handle with `Vmeta`, invoked `ObjOpTimeNow=27`,
and closed the handle. The kernel provider `vobjtimenow()` checked the handle
kind and read `todget(nil, nil)`. A failed open or invoke made the adapter
return 0.

**After:** `nsec()` calls direct `_NSEC(53)`. On this 64-bit target
`sys_nsec()` returns `todget(nil, nil)` in the return register and reads no
user pointer. The result is the same 64-bit nanosecond source, epoch and
precision; the public `vlong nsec(void)` signature is unchanged.
`micropython/modplan9.c` `plan9.time_ns()` now calls `nsec()` directly.

**Deliberate failure re-specification:** the old adapter returned 0 if the
`ProcessSelf` open or invoke failed (for example on Handle exhaustion). Direct
`_NSEC(53)` has no error path and always returns the current `todget()` count.
This is the only changed failure behavior and is not claimed as perfect
equivalence.

**Retirement scope:** `obj_process_self_time()` had no remaining source
caller after the two users were moved, so its implementation and declaration
were removed. The kernel `ObjOpTimeNow=27` dispatch case and `vobjtimenow()`
are retained: raw `OBJINVOKE` is exposed to the supported MicroPython
userspace, and removing a still-reachable object operation would be an
unnecessary ABI break. It is now unused by libc. Its historical numeric value
27 is unchanged.

### 2. `WAIT` — retained, not made direct

**Current path:** `obj_wait()` opens `ProcessSelf` with `Vcontrol`, invokes
`ObjOpWait=35`, and closes it. `vobjwait()` checks `VkindProcessSelf`, then
calls the one `pwait()` core and fills the typed `ObjWait` record: PID, three
time counters and a bounded status string.

**Direct comparison:** a direct call could also invoke `pwait()` and copy the
same record, preserving `Enochild`/`Einuse` and blocking/teardown behavior.
But there is no suitable existing number. `_WAIT(36)` and `AWAIT(47)` are
RESERVED historical interfaces: `_WAIT` returned only a PID and `AWAIT`
formatted a string, so either would revive an obsolete convention rather than
carry the typed record. Adding a new number is outside T10's narrow scope.
The object path already reuses `pwait()` as the sole core, so a direct wrapper
would remove one open/invoke/close sequence without simplifying the kernel.

**Decision:** keep the object path. `ProcessSelf` authorization, typed
`ObjWait` data, no-child/concurrent-wait errors and handle lifetime are
unchanged. This was exercised by the T8 multiprocess regression below.

### 3. Other T9 candidates — examined and deferred

| Candidate | Decision | Reason |
|---|---|---|
| `ERRSTR(41)` | Retained | Its direct handler remains; libc `errstr()` uses `ObjOpErrstrExchange`. A missing direct trap caller is not proof that the number may be removed, and the number stays stable. |
| `OSPACEGET/PUT/COMMIT/CREATE/CLOSE` | Retained | Bootstrap/legacy Space fallback dependency; `obootoid` still has an `OSPACEGET` failure-path caller. |
| `SEMACQUIRE`, `SEMRELEASE`, `TSEMACQUIRE`, `RENDEZVOUS`, `SEGATTACH`, `SEGDETACH`, `SEGFREE`, `SEGFLUSH` | Retained | No in-tree caller or runtime observation, but absence of evidence is not proof of safe removal; all numbers stay allocated. |
| USB data/I/O entries (`USBREAD`, `USBWRITE`, `USBIOCTL`, `USBEPNAME`) | Retained | Providers exist; current userland exposes only service/endpoint metadata, but no measured consumer is not permission for speculative removal. |
| Unsupported/legacy entries (`SYSR1`, `_ERRSTR`, `_FSESSION`, `FAUTH`, `FVERSION`) | Retained | Explicit compatibility/unsupported surface; no T10 evidence justifies changing active interfaces. |
| `_WAIT(36)`, `AWAIT(47)`, `OSPAWN(92)` | Unchanged, reserved | No revival of obsolete conventions; supported paths use typed `ObjOpWait`/`ObjOpOspawn`. |

No objects, permissions, error encoding, handle semantics, ObjectFIFO,
network/security, boot, USB, device drivers or MicroPython-only scope were
otherwise changed.

## Build

| Artifact | SHA-256 |
|---|---|
| `build/gcc/9pc64.elf` | `24963750604bf615ad0776a64ef0465ce47e4ea46f532c267872cc2ea648d480` |
| `build/gcc/bootx64.efi` | `22b702bbb71423cd0a6faf68151a15bbdfa85fad6fb81ac9c8665f472013e5c5` |
| `build/gcc/micropython.elf` | `589a048560c3952671e5ffdf6dc6d5b65337ba78619de821fe1667128840b6aa` |
| `build/format-o/format-o` | `64df144d46371833f14b2056b8d4265cd69f57fb07058dfa00c5b926e5bf2732` |
| `build/mpy-cross/mpy-cross` | `8cd838fd6314157541d80e7fb8eaf5524a1899816cdbb866ac2037b58993f6e0` |

Kernel build PASS: 132/132 selected, 134/134 closure, ABI and link PASS.
MicroPython build PASS: 247 units, 0 FAIL, no undefined symbols. Host build
PASS.

## Tests actually run

| Test | Result | Evidence |
|---|---|---|
| Direct time REPL: two `plan9.time_ns()` calls around `plan9.sleep(20)` | PASS | `build/t10/time/report.txt`, `boot-boot.log`: `T10-TIME1 1793994205594411785`, `T10-TIME2 1793994208569408052`, `T10-TIME-MONO True`, `T10-TIME-DELTA 2974996267`, `T10-TIME-DONE` |
| Normal parent REPL plus `space.spawn()`/`space.wait()` success, isolation, failed child, missing/bad ELF and denied child writer | PASS | `build/t10/multiproc/report.txt`: PIDs 10/11/12/13 vs parent 3; `T8-OK-WAIT 10 ''`; `T8-ISO-WAIT 11 ''`; `T8-FAIL-WAIT 12 'micropython 12: micropython'`; `T8-MISSING-ERR oexec: no such object`; `T8-BADELF-ERR ospawn: not a static x86-64 ELF`; `T8-DENY-EPERM True`; `T8-REPL-STILL-ALIVE` |
| GE/O two-boot persistence with a REPL-assigned global | PASS | `build/t10/persist/report.txt`: boot1 final commit 2, boot2 restore 2 and final commit 4, `T8-PERSIST 4242` |

Commands:

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-efi-gcc.sh -j 8
tools/build-mpy-gcc.sh -j 8
tools/build.sh

tools/qemu-o-boot.sh --no-build --output-dir build/t10/time \
  --obootexec 0x4f45584543000003 --repl-input build/t10/repl-time.py \
  --expect 'T10-TIME-DONE' --timeout 180

tools/qemu-o-boot.sh --no-build --output-dir build/t10/multiproc \
  --obootexec 0x4f45584543000003 --repl-input build/t8/repl-multiproc.py \
  --expect 'T8-REPL-STILL-ALIVE' --timeout 180

QOB_INI_BOOTEXEC=0x4f45584543000003 tools/qemu-o-persist.sh --no-build \
  --output-dir build/t10/persist --repl \
  --boot1-input build/t8/persist-set.py \
  --boot2-input build/t8/persist-get.py \
  --boot2-expect 'T8-PERSIST 4242' --timeout 180
```

Test inputs and logs under `build/t10/` are disposable evidence, not build
inputs.

## Not run

- Runtime syscall-trace instrumentation and per-number counters for T10.
- Physical hardware, TCG, multi-CPU stress and long-blocking wait cases.
- Handle-exhaustion comparison for the old `nsec()` failure path (the old
  behavior is documented from source, not re-observed).
- Any T11–T16 validation.

## Files changed

| File | Change |
|---|---|
| `runtime/libc/9sys/nsec.c` | `nsec()` calls direct `_NSEC(53)`; removed the OBJ adapter path |
| `runtime/libc/port/oobject.c` | removed the unused `obj_process_self_time()` adapter |
| `runtime/abi/obj.h` | removed the adapter declaration; documented the retained `ObjOpTimeNow` provider and direct libc path |
| `micropython/modplan9.c` | `plan9.time_ns()` uses direct libc `nsec()` |
| `docs/ROADMAP-T8-T16.md`, `docs/STATUS.md`, `docs/SYSCALL-LEDGER.md` | T10 status and supersession notes |
| `docs/SYSCALL-T10-CLEANUP.md`, `docs/SYSCALL-T10-CLEANUP.json` | this report and machine-readable result |
