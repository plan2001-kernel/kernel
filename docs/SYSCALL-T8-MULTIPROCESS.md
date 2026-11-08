# T8 — first complete multi-process MicroPython PoC

**Status: PASS (2026-11-06), observed on `kuha`.** This is the T8 hard-gate
result. T9–T16 remain PLANNED and are not implemented or claimed here.

This round implemented only T8. It adds no syscall, no object operation, no
process/Space ABI change, no implicit handle or PID inheritance, no pathname
or fd layer, no ObjectFIFO variant and no network/userland daemon. The
dispatch table stays **135 named / 62 active / 73 reserved**.

## What changed

- `kernel/core/sysvobj.c`, `vobjconsoleopen()`: the native console object is
  now allocated with `cr.stream` reserved bytes instead of zero.

This is a real T8 blocker, not a cosmetic change. `VkindConsole` stores the
stream number in `Vobj.size`; `vobjput()` releases exactly `o->size` bytes
through `vrelease()`. The old allocation reserved 0 bytes but the stream
number was then stored in `o->size`, so the first child that opened its
native stdout console Handle and exited released 1–2 unreserved bytes. The
global byte counter underflowed to `UINT64_MAX`, after which every later
`vobjalloc()` failed with `Enomem`. In the failing case the parent could
spawn the piped child and read its output, but the following `space.wait()`
and every later Handle/pipe allocation failed with `kernel allocate failed`.

The fix reserves the same small stream-sized byte count that release will
account. `Vobj.size` and the existing console/stream semantics are unchanged.
The change is one line and reuses the existing allocator accounting.

## Acceptance matrix (observed)

| # | T8 acceptance point | Result | Evidence |
|---|---|---|---|
| 1 | Normal boot reaches the parent MicroPython REPL without a test-only boot selector | PASS | Canonical `--obootexec 0x4f45584543000003` boot: `BOOTEXEC ... mode=mpy`, `OEXECUTE oid=0x4f45584543000003`, `PLAN2001-O: REPL READY`, `>>>` prompt |
| 2 | GE/O persistence survives the established two-boot check | PASS | `build/t8/final-persist/report.txt`: boot1 final commit 2, boot2 restore 2 and final commit 4, Counter 1 → 2; REPL global `X8` survives and prints `T8-PERSIST 4242` |
| 3 | Parent stores/references a genuine MicroPython ELF by GE/O OID | PASS | The boot loads OID `0x4f45584543000003`; `o_mp_sha256` equals the built `micropython.elf` SHA-256 `bc9df483…e11d653` |
| 4 | Child starts through `space.spawn()` → `OBJINVOKE(ObjOpOspawn)` | PASS | `o: OSPAWN pid=10 ...`, `n13: firstuser pid=10`; child `getpid()` printed as `PID10` and matched the returned PID |
| 5 | Child is an independent MicroPython process, not a coroutine/thread | PASS | Separate kernel PID (`10` vs parent `3`); child has its own interpreter and reports `space.info() is None` |
| 6 | Child runs a simple Python program and exits with observable output | PASS | Child `-c 'print("PID" + str(...getpid()))'` output read from an explicit Pipe as `b'PID10'` |
| 7 | `space.wait()` returns the correct PID and status | PASS | Success: `T8-OK-WAIT 10 ''`; second child: `T8-ISO-WAIT 11 ''`; failure: `T8-FAIL-WAIT 12 'micropython 12: micropython'` |
| 8 | Non-zero/failed child | PASS | Child `-c 'raise ValueError("boom")'`: PID 12 reaped with non-empty failure status |
| 9 | Missing executable | PASS | Missing OID: `T8-MISSING-ERR oexec: no such object` |
| 10 | Invalid executable | PASS | `space.oregister(b'not-an-elf')` then spawn: `T8-BADELF-ERR ospawn: not a static x86-64 ELF` |
| 11 | Child isolation | PASS | Child does not see parent global `PARENT_ONLY`; `space.info()` is `None`; distinct PID |
| 12 | Single-writer Space constraints | PASS | Child `space.open_handle('init')` is denied: `T8-DENY-EPERM True`, traceback `OSError: permission denied`; parent remains writer (`space.oput` succeeds) and the Space advances to version 56 in that run |
| 13 | Parent REPL remains interactive after success, failure, missing/bad ELF and denial | PASS | `T8-AFTER-FAIL alive`, `T8-AFTER-MISSING alive`, `T8-AFTER-BADELF alive`, final `T8-REPL-STILL-ALIVE` and prompt |
| 14 | Existing T7 OSPAWN/WAIT regression | PASS | `build/t8/final-reg-ospawn`: `OSPAWN-WAIT 10 ospawn 4f45584543000003 10: usage`, `OSPAWN-DONE` |
| 15 | Existing negative syscall/spawn matrix | PASS | `build/t8/final-reg-negative`: self-OTERMINATE, missing OID, bad RFORK flags and memory flag all rejected; `NEG-DONE` |

No acceptance point is marked PARTIAL in this round. The stronger cases listed
under “Not run” were not exercised and are not implied by these PASSes.

## Build (canonical artifacts used by the runs)

| Artifact | SHA-256 |
|---|---|
| `build/gcc/9pc64.elf` | `072d2c5ab267ac7375cb8685de0aee5b04018184bfe4668dadb060dbe9081261` |
| `build/gcc/bootx64.efi` | `d37c1ca2f1edb938fb74b8a43a9d82d1ca357561185b6739a4e295803ee6ddc2` |
| `build/gcc/micropython.elf` | `bc9df483ab82a6c59a3b3d706f04cf574ef22ec13719b65ec6d096694e11d653` |
| `build/format-o/format-o` | `64df144d46371833f14b2056b8d4265cd69f57fb07058dfa00c5b926e5bf2732` |
| `build/mpy-cross/mpy-cross` | `8cd838fd6314157541d80e7fb8eaf5524a1899816cdbb866ac2037b58993f6e0` |

Kernel build PASS: 132/132 selected, 134/134 closure, ABI and link PASS, no
undefined symbols. MicroPython build PASS: 247 units, 0 FAIL, no undefined
symbols. Host build PASS.

## Commands

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-efi-gcc.sh -j 8
tools/build-mpy-gcc.sh -j 8
tools/build.sh

# T8 multi-process matrix
tools/qemu-o-boot.sh --no-build \
  --output-dir build/t8/final-multiproc \
  --obootexec 0x4f45584543000003 \
  --repl-input build/t8/repl-multiproc.py \
  --expect 'T8-REPL-STILL-ALIVE' --timeout 180

# Two-boot GE/O persistence, including a REPL-assigned global
QOB_INI_BOOTEXEC=0x4f45584543000003 tools/qemu-o-persist.sh --no-build \
  --output-dir build/t8/final-persist --repl \
  --boot1-input build/t8/persist-set.py \
  --boot2-input build/t8/persist-get.py \
  --boot2-expect 'T8-PERSIST 4242' --timeout 180

# Existing regressions
tools/qemu-o-boot.sh --no-build --output-dir build/t8/final-reg-ospawn \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-simple/inputs/repl-ospawn.py \
  --expect 'OSPAWN-DONE' --timeout 120
tools/qemu-o-boot.sh --no-build --output-dir build/t8/final-reg-negative \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-simple/inputs/repl-negative.py \
  --expect 'NEG-DONE' --timeout 120
```

`build/t8/repl-multiproc.py`, `persist-set.py`, `persist-get.py` and the logs
under `build/t8/` are disposable test inputs/evidence, not build inputs. The
REPL input avoids bracketed syntax solely because the QEMU PS/2 injection map
does not provide those keys; it is ordinary MicroPython code.

## Observed output (final runs)

From `build/t8/final-multiproc/boot-boot.log`:

```text
o: OEXECUTE oid=0x4f45584543000003 len=538792 ver=0x1
PLAN2001-O: REPL READY
T8-PARENT-PID 3
o: OSPAWN pid=10 oid=0x4f45584543000003 cpu=4294967295
T8-CHILD-OUT b'PID10'
T8-CHILD-PID-MATCH True
T8-CHILD-DISTINCT True
T8-OK-WAIT 10 ''
o: OSPAWN pid=11 ...
T8-ISO-OUT b'ISO'
T8-ISO-WAIT 11 ''
o: OSPAWN pid=12 ...
T8-FAIL-WAIT 12 'micropython 12: micropython'
T8-MISSING-ERR oexec: no such object
T8-BADELF-ERR ospawn: not a static x86-64 ELF
o: OSPAWN pid=13 ...
T8-DENY-OUT b'Traceback (most recent call last):\n  File "<command>", line 1, in <module>\nOSError: permission denied\n'
T8-DENY-EPERM True
T8-PARENT-WRITER ok
T8-SPACE-INFO ('init', 56, 'e1e308d5...')
T8-REPL-STILL-ALIVE
```

From `build/t8/final-persist/report.txt`:

```text
result=PASS
boot1_final_commit_version=2
boot2_restore_version=2
boot2_final_commit_version=4
boot1_counter_n1=PASS
boot2_counter_n2=PASS
o_mp_sha256=bc9df483ab82a6c59a3b3d706f04cf574ef22ec13719b65ec6d096694e11d653
```

and from `build/t8/final-persist/boot-boot2.log`: `T8-PERSIST 4242`.

## Security and architecture notes

- The child cannot acquire the parent Space implicitly. `space.open_handle`
  reaches `osopenspace()`, which requires `oboot()` (`up == bootowner && up->pid == bootpid`).
  The child is not the boot owner and receives `Eperm`. The observed traceback
  text is `OSError: permission denied`.
- The child was granted only the explicit stdout Pipe endpoint needed to
  observe its output. There is no implicit handle or PID inheritance.
- The parent remained the writer after every child: `space.oput` succeeded and
  the Space version advanced; the child had `space.info() is None`.
- No concurrent writer into the parent Space was authorized or observed.
- T7 direct calls and the five universal `OBJ*` operations are unchanged.
- No T9–T16 stage was started.

## Not run / remaining limitations

- Physical hardware, TCG, and multi-CPU stress were not run.
- More than one concurrent unauthorized writer, long-blocking children and
  child heap/budget exhaustion were not stressed.
- ObjectFIFO multi-process validation belongs to T11 and was not started.
- The child test programs are deliberately single-expression `-c` programs;
  the PS/2 host injector does not provide bracket keys, so the parent test
  avoids list/dict literals.
- The console accounting fix was validated by the T8 matrix, the two-boot
  persistence check and the T7 OSPAWN/negative regressions; a dedicated
  kernel-accounting unit test is not added in this round.

## Related

- [STATUS](STATUS.md) — T8 summary and current state.
- The then-current stage roadmap (since removed) recorded T9–T16 as PLANNED.
- [SYSCALL-T8-MULTIPROCESS.json](SYSCALL-T8-MULTIPROCESS.json) — machine-readable
  result records.
