# T6 — process lifecycle over five typed OBJINVOKE operations

> **Historical report (T0–T6 phase).** Current direct-syscall state: [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md).

**Result: PARTIAL.** The five process-lifecycle operations are implemented and
the named legacy numbers are reserved, but not all authorization/edge tests
listed in the task were executed. The tree builds, boots the normal
MicroPython REPL, and passes real `rfork`/`wait`/`exit`, `oterminate` and
`ospawn` guest tests. The sections below name exactly what was and was not
run. `EXITS(8)` deliberately remains active for the separate `obootoid`
bootstrap image; its concrete call is documented and is a T10 dependency.

Base publication: source snapshot
`a77e5111d51336bd0f94efce058d07075a3b9c45451d8bb3748a234be6410a1d`.
Host `kuha`, kernel `6.12.111+deb13-amd64`, GCC 14.2.0, QEMU 10.0.13 (KVM).
No new syscall number was added. No GE/O, tick/HZ, ALARM, network or note-ABI
change was made. The T5b note/noteret fix is untouched.

## 1. Dependency table before the change

T0 measured the normal boot/persistence path; it never exercises a child.
Runtime counts below are from `docs/SYSCALL-T0-MEASUREMENT.json`. Static
callers are from that file's disassembly analysis, re-checked here.

| nr | name | linked MPY call site | boot loader | T0 runtime | child path tested by normal boot |
| --- | --- | --- | --- | --- | --- |
| 8 | `EXITS` | libc `exits()` -> `_exits` stub | **yes**: `obootoid` own `exits` stub, `mov $0x8,%rbp` | 0 | error path never taken |
| 19 | `RFORK` | `p9_rfork` -> `rfork` stub | no | 0 | no |
| 36 | `_WAIT` | linked but no direct caller | no | 0 | no |
| 47 | `AWAIT` | libc `wait()` -> `await` stub | no | 0 | no |
| 92 | `OSPAWN` | `space_spawn` -> `ospawn` stub | no | 0 | no |
| 93 | `OTERMINATE` | `space_terminate` -> `oterminate` stub | no | 0 | no |

Concrete bootstrap dependencies remain `OVOLOPEN(54)`, `OSPACEOPEN(55)`,
`OEXECUTE(60)` and the `OSPACEGET(56)` error fallback. Those were not changed;
`OEXECUTE` migration remains T10. The loader's error `exits()` is the one real
`EXITS(8)` user today and is why that number stays active.

The important negative fact: a successful normal boot exercises **none** of
the five retired numbers and no child process. It therefore cannot prove
either the legacy removal or the child RFORK/OSPAWN paths. That is why
separate guest tests were run below.

## 2. ABI additions (no new syscall number)

`runtime/abi/obj.h` adds the next free ObjOp codes:

| ObjOp | name | provider Handle | arguments and result |
| --- | --- | --- | --- |
| 32 | `ObjOpRfork` | own `VkindProcessSelf`, `Vcontrol` | `arg0` = RFORK flags; no buffers; child returns 0, parent the PID |
| 33 | `ObjOpOspawn` | `VkindSpace`, `Vexecute` | `in` = `ObjOspawnReq{oid, argv, argc, flags=0, reserved=0, options}`; result child PID |
| 34 | `ObjOpOterminate` | own `VkindProcessSelf`, `Vcontrol` | `arg0` = positive direct-child PID; no buffers |
| 35 | `ObjOpWait` | own `VkindProcessSelf`, `Vcontrol` | `out` = typed `ObjWait`; result 0 |
| 36 | `ObjOpExit` | 0 or own `VkindProcessSelf`, `Vcontrol` | `in` = bounded status text; no-return on success |

`ObjWait` is a bounded typed record: `pid`, the three `Waitmsg` times and
`msglen` + `msg[128]`. The kernel does not format the wait result as text;
`libc await()` formats the same five historical fields only if a caller still
uses `await()`. The exit status string is preserved, not replaced by a number.

`Vexecute` (`1<<13`) was added to `runtime/abi/vobj.h` so the Ohand
`Oexecute` right survives the Vhand capability layer. Before this, the
generic `vgecaplimit()` mapping dropped `Oexecute` from a `VkindSpace`
capability, which would have made an OBJ-path OSPAWN fail `Eperm`. The mapping
is now bidirectional (`Vexecute` <-> `Oexecute`) and remains an attenuation:
no right is amplified.

`ObjOspawnReq.options` points at the existing `struct Ospawn` and optional
`Oinherit`/`Oinheritv` extension block. The legacy layout, limits and
validation are therefore reused rather than copied.

## 3. Kernel implementation: one core per operation

The direct handlers were renamed to non-dispatch cores and the typed
operations call them:

- `sysrfork` -> `kernrfork(ulong)`; `vobjrfork()` validates ProcessSelf and
  calls it. `forkchild`, `RFPROC` vs group-only behavior, child return 0,
  parent PID, rollback, locks and parent link are unchanged.
- `sysospawn` -> `kernospawn(Ohand *space, uvlong oid, char **argv, int argc,
  struct Ospawn *opt)`. The only change is how the Space is supplied:
  `vobjospawn()` passes the pinned Ohand from a `VkindSpace` Handle instead of
  a native int. `ge/storage/sysgefs.c` adds `osgetobjecthand()`, which shares
  the exact `oauthorize(..., Oexecute)` + `onativeget()` core with
  `osgetobject()`. ELF parsing, argv copy, CPU affinity, memory budget,
  segments, stack, view/Pipe/env/network/Vhand inheritance and limits are
  unchanged.
- `sysoterminate` -> `kernoterminate(int)`; `vobjoterminate()` calls it. The
  same PID/parent/parentpid/kp/exiting checks apply; no process is taken over
  by numeric PID alone.
- `sys_wait`/`sysawait` are deleted. `vobjwait()` calls the existing
  `pwait()` and fills the typed record.
- `vobjexit()` bounds the status text with the same `validaddr`/`vmemchr`
  rule as the old EXITS handler and calls the same `pexit(status, 1)`.
  Handle 0 is caller-only (like `ObjOpNoted`/`ObjOpErrstrExchange`), so an
  exhausted Handle table cannot prevent exit; no other process can be named.

The OBJINVOKE router validates operation, direction, exact record length and
nested user pointers before calling these cores. It never re-enters a raw
legacy syscall.

## 4. Userland migration

- `runtime/libc/9sys/rfork.c` (new): `rfork()` over `OBJINVOKE(ObjOpRfork)`.
  The child returns 0 and does not close the parent's Handle (newproc gives
  the child an empty Vhand table anyway).
- `runtime/libc/9sys/wait.c`: `wait()` and `await()` over
  `OBJINVOKE(ObjOpWait)`; `Waitmsg` allocation and fields are unchanged.
- `runtime/libc/port/exits.c`: `exits()` still runs `_onexit` exactly once,
  then `_exits()` -> `OBJINVOKE(ObjOpExit)`. No retired EXITS stub remains in
  the MPY ELF.
- `runtime/libc/port/oobject.c`: `obj_rfork`, `obj_wait`, `obj_ospawn`,
  `obj_oterminate`, `obj_exit`.
- `micropython/p9space.c`: `space.spawn` uses the already-open `VkindSpace`
  `spread` Handle (or an explicit Vhand) and `obj_ospawn`; `space.terminate`
  uses `obj_oterminate`. Python method names, parameter meanings and error
  texts are unchanged.
- `micropython/modplan9.c` needed no change: `plan9.rfork`, `plan9.wait` and
  `plan9.exits` now reach the new libc adapters.

The `obootoid` image is deliberately different: it keeps its own `exits`
stub and therefore `EXITS(8)` remains an active kernel handler. Its SHA-256
is unchanged (`10c63c33...`), and `objdump -d` shows `mov $0x8,%rbp` in that
stub. Moving it to OBJ would require the loader to use the five-call ABI
before `OEXECUTE`; that is T10 work, not silently bundled here.

## 5. Retired numbers

`RFORK(19)`, `_WAIT(36)`, `AWAIT(47)`, `OSPAWN(92)` and `OTERMINATE(93)` now
have `nil` dispatch and `sysreserved=1`. `runtime/abi/sys.h` keeps the numbers
allocated forever with a comment; no number was renumbered or reused.

The generated table is **135 named / 58 active / 77 reserved**. No
`sysrfork`, `sysospawn`, `sysoterminate`, `sys_wait` or `sysawait` symbol is
linked; only `kern*` cores exist. `EXITS(8)` remains active and dispatches to
`sysexits`.

## 6. Verification

### A. Build, normal boot, persistence

| Check | Result | Evidence |
| --- | --- | --- |
| Kernel build (132/132 selected, 134/134 closure, ABI, link) | **PASS** | `/tmp/opencode/build-kernel-final.log`; `build/gcc/9pc64.elf` SHA-256 `d6f9029fe0e3938298c342ffe043900127ba42180a8cfdc9579a747b1bb2bb20`, 1,154,600 bytes |
| MicroPython build (250 units, 0 FAIL, no undefined) | **PASS** | `build/gcc/micropython.elf` SHA-256 `ca2e140256896b2ae1a54f46a40bc6f2b5439e7ae2beaad03b894c7ceaefb484`, 543,128 bytes |
| EFI image | **PASS** | `build/gcc/bootx64.efi`, SHA-256 `8454ba0dc545e9132d03fe843233c6ecef6158ae6301d301abe4070023384c76` |
| Normal QEMU boot -> REPL -> `6*7=42` | **PASS** | `build/qemu-o/t6-final2/serial-boot.log` |
| Two-boot GE/O persistence (Counter 1 -> 2, versions 1 -> 2) | **PASS** | `build/qemu-o/t6-persist2/report.txt` |
| T1-T5b measurements retained | **RETAINED, NOT RERUN** | previous reports unchanged |

### B. Real MicroPython lifecycle tests

| Check | Result | Evidence |
| --- | --- | --- |
| `rfork(RFPROC)`: child sees 0, parent sees PID | **PASS** | `t6-final2`: `(10, 'micropython 10: child-ok')` |
| group change without `RFPROC` (`RFENVG=2`) | **PASS** | `print(plan9.rfork(2))` -> `0`, process continues |
| child `exit('child-ok')` and parent `plan9.wait()` status | **PASS** | same log; status string preserved |
| `OSPAWN` from a real OID (`0x4f45584543000003`) to an MPY ELF child | **PASS** | `build/qemu-o/t6-spawn2/serial-boot.log`, `o: OSPAWN pid=10` |
| parent `space.wait()` reaps the spawned child | **PASS** | status `(10, 'ospawn 4f45584543000003 10: usage')` |
| missing OID | **PASS** | `build/qemu-o/t6-err`: `SPAWN-MISSING-OK` |
| too-large argv | **PASS** | `build/qemu-o/t6-err`: `SPAWN-ARGV-OK` |
| bad/invalid Space Handle | **PASS** | `build/qemu-o/t6-err`: `SPAWN-BADHAND-OK` |
| `OTERMINATE` direct child | **PASS** | `build/qemu-o/t6-term`: `(10, 'micropython 10: terminated')` |
| wrong PID / non-child / indirect child | **NOT RUN** | no safe guest fixture was added |
| explicit OSPAWN `vhandles` and another inheritance option | **NOT RUN** | code path reused unchanged; not exercised |
| explicit OSPAWN `pipes`/`view`/`env`/CPU/membudget combinations | **NOT RUN** | reused unchanged; not exercised |
| child resource release, no panic/parent crash | **PASS** for the runs above | serial logs show no panic/bad-disk/flush error |

### C. Authorization and edge cases

| Check | Result |
| --- | --- |
| bad Handle on OSPAWN (numeric 123, not a VkindSpace) | **PASS** (`Ebadarg`) |
| wrong Vkind (e.g. ProcessSelf passed to OSPAWN) | **NOT RUN** |
| missing rights / read-only Space attenuation | **NOT RUN** |
| forbidden delegation / no-inherit / nested-pointer faults | **NOT RUN** |
| very long status or argv beyond the normal limits | **NOT RUN** (only the Python argv pre-limit was tested) |
| heap/budget boundary cases | **NOT RUN** |
| synchronization error paths | **NOT RUN** |

### D. Instrumented process test

Two disposable trace boots (`PLAN2001_SYSCALL_TRACE=1` kernel and
MicroPython) were run. A spawned MPY child creates its own checkpoint and can
interleave with PS/2 input, so two runs were used rather than one long script.

| Op / number | Count | Log |
| --- | --- | --- |
| `ObjOpRfork=32` | 1 | both runs |
| `ObjOpOspawn=33` | 1 | `build/qemu-o/t6-trace/serial-boot.log` |
| `ObjOpOterminate=34` | 1 | `build/qemu-o/t6-trace2/serial-boot.log` |
| `ObjOpWait=35` | 2 / 1 | both runs |
| `ObjOpExit=36` | 1 | `t6-trace` |
| `RFORK=19` | **0** | both runs |
| `_WAIT=36` | **0** | both runs |
| `AWAIT=47` | **0** | both runs |
| `OSPAWN=92` | **0** | both runs |
| `OTERMINATE=93` | **0** | both runs |
| dropped trace records | **0** | all dumps |

Raw calls `plan9.sysraw(19/36/47/92/93)` each returned `Ebadarg`
(`OSError: bad arg in system call`) with no fatal note and no panic; see
`build/qemu-o/t6-raw/serial-boot.log`. The `EXITS`/`OEXECUTE` boot-loader
error fallback was **NOT RUN**; `EXITS` therefore deliberately remains
active.

### E. Static symbol evidence

- `runtime/libc/9sys/rfork.c` -> `rfork`, `obj_rfork`;
  `runtime/libc/9sys/wait.c` -> `wait`, `obj_wait`;
  `runtime/libc/port/exits.c` -> `exits`, `_exits`, `obj_exit`;
  `oobject.c` -> `obj_ospawn`, `obj_oterminate`.
- `nm -u build/gcc/micropython.elf` lists none of `rfork`, `ospawn`,
  `oterminate`, `_wait`, `await`, `_exits`.
- `objdump -d` shows calls `p9_rfork -> rfork`, `space_wait -> wait`,
  `space_spawn -> obj_ospawn`, `space_terminate -> obj_oterminate`,
  `exits -> _exits -> obj_exit`.
- No `mov $0x13/%rbp`, `$0x24`, `$0x2f`, `$0x5c`, `$0x5d` or `$0x8` stub
  remains in the MPY ELF. The separate `obootoid` ELF still has its
  `exits: mov $0x8,%rbp` stub.

## 7. What remains before T6 can be called complete

- Explicit OSPAWN `vhandles`/`pipes`/`view`/`env`/CPU/membudget combinations
  and their failure paths.
- Wrong-Vkind, missing-rights, wrong-PID/non-child and forbidden-delegation
  tests for the new operations.
- Very long status/argv, heap/budget boundaries and synchronization faults.
- A fresh full T1-T5b instrumented rerun was not performed; those retained
  reports are referenced, not re-measured.
- The `obootoid` error-path `EXITS(8)` and `OEXECUTE` fallback were not
  triggered. `EXITS` remains active on purpose.

Because of these gaps this round is published as `incomplete`, not completed.
