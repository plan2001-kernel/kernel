# T7 — seven simple syscalls restored to direct historical numbers

**Result: PASS for the seven requested direct paths.** The tree builds, boots
the normal MicroPython REPL, persists GE/O state over two boots, and passes
focused guest tests for every restored call. Broad regression campaigns,
physical hardware and TCG were not run. This report records exactly what was
and was not executed.

Base publication: source snapshot
`629a7b5020e3216627251d172c8d25c3154e3401a5e8699fcaee689300e7a559`.
Host `kuha`, kernel `6.12.111+deb13-amd64`, GCC 14.2.0, QEMU 10.0.13 (KVM).
No new syscall number was added. GE/O format, tick/HZ, ALARM, network and the
T5b note ABI/lentry.S note-return path were not changed.

## 1. What changed

Seven caller-local scalar/no-parameter operations are direct historical
syscalls again instead of an `OBJOPEN`/`OBJINVOKE`/`OBJCLOSE` sequence:

| nr | syscall | before (T6 tree) | after (this change) |
| ---: | --- | --- | --- |
| 24 | `BRK_` | `ObjOpenProcessSelf` + `ObjOpHeapBreak=26` | direct `BRK_`; libc `brk()`/`sbrk()` |
| 17 | `SLEEP` | `ObjOpenProcessSelf` + `ObjOpSleep=28` | direct `SLEEP`; one millisecond argument |
| 28 | `NOTIFY` | `ObjOpNotifyRegister=30` on ProcessSelf | direct `NOTIFY`; `setnotify()` core |
| 29 | `NOTED` | `ObjOpNoted=31` | direct `NOTED`; `notecont()` core |
| 19 | `RFORK` | `ObjOpRfork=32` on ProcessSelf | direct `RFORK`; `kernrfork()` core |
| 93 | `OTERMINATE` | `ObjOpOterminate=34` on ProcessSelf | direct `OTERMINATE`; `kernoterminate()` core |
| 8 | `EXITS` | libc `_exits()` over `ObjOpExit=36` | direct `EXITS`; libc `exits() -> _onexit -> _exits` |

The kernel still has the shared cores: `ibrk(addr, BSEG)`, `kernelsleep()`,
`setnotify()`, `notecont()`, `kernrfork()`, `kernoterminate()` and
`pexit(status, 1)`. The restored handlers call those cores; no second
implementation exists.

`NOTED` keeps the critical T5b amd64 SysV `RDI/RSI` note ABI and the
`lentry.S` `noteret`/`iretq` special return unchanged.  The successful
`sysnoted()` handler returns the private `NOTED_RET` marker only to
`dosyscall()`, which then returns `1` without writing the ordinary syscall
result over the restored `Ureg`.  A failing `NOTED` raises the normal error
and never takes that path.  The old GCC-incompatible
`((void**)&ureg)[-1]` technique is not reintroduced.

The seven former `OBJINVOKE` variants are retired.  `ObjOpHeapBreak=26`,
`ObjOpSleep=28`, `ObjOpNotifyRegister=30`, `ObjOpNoted=31`,
`ObjOpRfork=32`, `ObjOpOterminate=34` and `ObjOpExit=36` stay allocated in
`runtime/abi/obj.h` as RETIRED codes with no dispatch case; a raw
`OBJINVOKE` with one of them is rejected as `obj: operation not supported`.
The corresponding `vobj*` kernel providers and libc `obj_rfork`,
`obj_oterminate`, `obj_exit` and `obj_process_self_sleep` adapters were
removed.

`ProcessSelf` remains: `ObjOpTimeNow=27` still uses it, and `ObjOpWait=35`
still opens a `Vcontrol` ProcessSelf Handle.  `OSPAWN(92)`, `_WAIT(36)` and
`AWAIT(47)` were not changed; OSPAWN remains the typed `ObjOpOspawn=33`
operation.

## 2. Userland call sites

- `runtime/libc/9sys/sbrk.c` calls the generated direct `brk_` stub; the
  historical 8-byte rounding, `0` success return and `-1` error/errstr are
  unchanged.
- `micropython/modplan9.c` `plan9.sleep()` calls the generated `sleep` stub.
  `micropython/main.c` and `runtime/libc/port/lock.c` polling loops use the
  same direct `sleep`.
- `runtime/libc/port/atnotify.c` keeps its normal `notify()`/`noted()` calls;
  those names now resolve to the generated `NOTIFY`/`NOTED` stubs.
- `runtime/libc/9sys/rfork.c`, `9sys/notify.c` and `9sys/noted.c` (the T5/T6
  OBJ adapters) were removed from the MicroPython build; the generated direct
  stubs replace them.
- `runtime/libc/port/exits.c` keeps `_onexit` and calls `_exits`, which is
  now the generated direct `EXITS(8)` stub.  `obootoid.elf` still contains
  its own unchanged `mov $0x8,%rbp` exits stub.
- `micropython/p9space.c` `space.terminate()` calls the direct
  `oterminate()` stub with one positive PID.  `space.spawn()` continues to
  use `obj_ospawn`.

## 3. Build and boot

| Check | Result | Evidence |
| --- | --- | --- |
| Kernel: 132/132 selected, 134/134 full closure, ABI PASS, link PASS | **PASS** | `/tmp/opencode/final-k.log`; `build/gcc/9pc64.elf`, 1,154,480 bytes, SHA-256 `8fd572b985fd57d2b348d946827ccb13b15b9a931e4e2613db7dbcb4c6bf8ead` |
| MicroPython: 247 units, 0 FAIL, no undefined symbols, direct-stub checks PASS | **PASS** | `/tmp/opencode/final-m.log`; `build/gcc/micropython.elf`, 538,792 bytes, SHA-256 `bc9df483ab82a6c59a3b3d706f04cf574ef22ec13719b65ec6d096694e11d653` |
| EFI loader | **PASS** | `build/gcc/bootx64.efi`, 22,016 bytes, SHA-256 `06cddbfc7e34fcde8d38a16f01f299af018c25e526abe0131540fae77e3ee96c` |
| Host build (`format-o`, `mpy-cross`) | **PASS** | `/tmp/opencode/final-h.log` |
| Normal O boot -> REPL | **PASS** | `build/syscall-simple/final-boot2/report.txt` |
| Two-boot GE/O persistence (Counter 1 -> 2, versions 1 -> 2) | **PASS** | `build/syscall-simple/final-persist2/report.txt` |
| `obootoid` still uses `EXITS(8)` | **PASS** | `objdump -d build/gcc/gen/obootoid.elf`: `mov $0x8,%rbp`; SHA-256 `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769` |

The generated dispatch table is now **135 named / 62 active / 73 reserved**
(before: 135 / 58 / 77).  `NOTIFY`, `NOTED`, `RFORK` and `OTERMINATE` are
active again; `BRK_`, `SLEEP` and `EXITS` were already active.

## 4. Functional guest tests

All runs used the disposable KVM QEMU/O image and the normal final artifacts
unless noted.

| Test | Result | Evidence |
| --- | --- | --- |
| `BRK_(24)`: `sysraw(24, 0)` and an invalid break | **PASS** | `BRK-BASE 2543616`; invalid break -> `virtual memory allocation failed`; `build/syscall-simple/repl3/serial-boot.log` |
| libc allocation through the direct break | **PASS** | normal boot and every REPL commit (libc `malloc` in `p9image.c`) |
| `SLEEP(17)`: `plan9.sleep(0)` and `plan9.sleep(1)` | **PASS** | `SLEEP-OK`; trace shows `nr=17` active |
| `RFORK(19)` + child `EXITS(8)` + parent `WAIT` | **PASS** | `RFORK-EXIT 10 10 micropython 10: child-ok` |
| `OTERMINATE(93)` direct child | **PASS** | `TERM-WAIT 11 11 micropython 11: terminated` |
| `OSPAWN` + `WAIT` regression | **PASS** | `build/syscall-simple/ospawn`: `OSPAWN-WAIT 10 ospawn 4f45584543000003 10: usage` |
| `NOTIFY(28)` registration and `NOTIFY(nil)` | **PASS** | `NOTIFY-REG 0`, `NOTIFY-NIL 0`; `build/syscall-simple/note/serial-boot.log` |
| `NOTED(29)` NCONT through the real probe callback | **PASS** | `NOTE-COUNT 1`; frame `(0, 0, ..., 140737488349192)` where the fourth value is the user `Ureg*` in SysV RDI; note text in RSI |
| `NOTED(29)` error when no note is active | **PASS** | `NOTED-NOACT bad arg in system call` |
| Real MicroPython `onnote()` + `atnotify()` + `NOTED(NCONT)` | **PASS** | `PLAN2001_ALARM_NOTETEST=1` kernel build (test-only alarm starter; `alarm.c` and `ALARM(6)` unchanged): `ONNOTE-EXC interrupted`, `ONNOTE-MS 14`, `ONNOTE-ALIVE`, REPL committed; `build/syscall-simple/onnote` |
| Raw retired `ObjOp` calls rejected | **PASS** | ops 26/28/30/31/32/34/36 each `obj: operation not supported`; `build/syscall-simple/note` |
| Negative process cases | **PASS** | self `OTERMINATE` -> `bad arg`; missing OSPAWN OID -> `oexec: no such object`; `RFORK(RFFDG|RFCFDG)` -> `bad arg`; `RFORK(RFMEM)` without RFPROC -> `bad arg`; `build/syscall-simple/negative` |
| Kernel panic in tested paths | **none** | serial logs above |

The `onnote` test uses the existing test-only alarm starter because the normal
kernel does not start `alarmkproc()`.  This task does **not** claim the
ordinary alarm service or tick/HZ timing is fixed or changed.

## 5. Instrumented trace

A measurement-only `PLAN2001_SYSCALL_TRACE=1` kernel and MicroPython were
built and run once.  Checkpoint 4 (`plan9.system()` after the scripted
operations) completed with 4,985 records, **dropped=0**:

| syscall | ACTIVE records | RESERVED records |
| --- | ---: | ---: |
| `EXITS(8)` | 1 | 0 |
| `SLEEP(17)` | 4 | 0 |
| `RFORK(19)` | 2 | 0 |
| `BRK_(24)` | 8 | 0 |
| `NOTIFY(28)` | 3 | 0 |
| `NOTED(29)` | 2 | 0 |
| `OTERMINATE(93)` | 1 | 0 |

The seven retired `ObjOp` codes (26, 28, 30, 31, 32, 34, 36) have **no**
`P2K-OBJOP checkpoint=4` count; the raw calls that name them are rejected.
Retained operations still show their real use (`ObjOpRead=2`, `Write=3`,
`CreatePersist=12`, `Commit=13`, `PutOid=25`, `TimeNow=27`,
`ErrstrExchange=29`, `Wait=35`).

Evidence: `build/syscall-simple/trace3/serial-boot.log`.  The trace run uses
the sanctioned boot script; the dump itself is longer than the script's
normal 15-second post-input prompt wait, so that run was invoked with a local,
reverted 180-second wait solely to let the dump finish.  The final QEMU result
line is PASS and the trace checkpoint has `dropped=0`.

Static confirmation:

- `objdump -d build/gcc/micropython.elf` contains direct stubs
  `_exits` (`RBP=0x8`), `sleep` (`0x11`), `rfork` (`0x13`), `brk_` (`0x18`),
  `notify` (`0x1c`), `noted` (`0x1d`) and `oterminate` (`0x5d`).
- The same dump has real `wait`, `obj_wait` and `obj_ospawn` symbols and no
  `obj_rfork`, `obj_oterminate`, `obj_exit` or `obj_process_self_sleep`.
- `build/gcc/gen/obootoid.elf` still has `mov $0x8,%rbp`.

## 6. Not run / limits

- Physical hardware and TCG.
- The ordinary (non-test) alarm service and tick/HZ timing were not exercised
  or changed.
- `_onexit` was not functionally exercised: `exits()` still calls it exactly
  once before `_exits`, but no userspace test registers a libc `_onexit`
  hook.  This is a source-inspection result, not an executed test.
- The full T6 authorization matrix (wrong Vkind, wrong-PID combinations,
  explicit OSPAWN vhandles/pipes/env/cpu/membudget, oversized argv/status)
  was not re-run.  This task re-ran the OSPAWN/WAIT success path, direct
  OTERMINATE, RFORK/EXIT, and the negative cases listed above.
- T1–T5b were not re-measured; their historical reports are retained with a
  banner pointing here.
