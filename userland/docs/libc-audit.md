# Plan2001 libc ABI audit (GitHub Actions)

This audit is an executable, evidence-producing **diagnostic**, not a POSIX
compatibility certification. It complements the existing native link and QEMU
workflow without modifying the kernel. Its input revisions are exclusively
the source policy in [../config/sources.json](../config/sources.json):
BusyBox is pinned by immutable SHA, while kernel `main` is resolved and
recorded as a concrete SHA on each source-fetching run.

## Run the audit

Workflow: [../.github/workflows/libc-audit.yml](../../.github/workflows/libc-audit.yml)

- Automatically on PRs that change the audit, CI scripts or source pins.
- Automatically on pushes to `main`.
- Manually with GitHub Actions > **Plan2001 libc ABI audit** > **Run workflow**.

The GitHub Actions run builds the pinned Plan2001 MicroPython user runtime
via its canonical `tools/build-mpy-gcc.sh` and obtains the actual
`build/gcc/mpy/lib/libc-user.a`, startup objects and syscall stubs. It also
builds the pinned upstream BusyBox in a **Linux-host static control**
configuration (only `true`, `false`, `echo`). It then links the *actual*
upstream `coreutils/true.c` as a native AMD64 ELF using the kernel's
native runtime and a minimal diagnostic header/driver.

Finally `tools/ci/libc_audit.py` performs the symbol investigation:

1. Read exported global symbols from the **built** `libc-user.a` and native
   startup/syscall objects using `nm`.
2. Read defined and unresolved symbols from BusyBox's **Linux-host-built**
   applet/library object files. Subtract references satisfied internally by
   BusyBox to get a candidate external dependency set.
3. Compare candidate references with exported native names, group missing
   names by possible work area, and enumerate POSIX header filenames.
4. Read the canonical kernel build's `LIBC_SOURCES` list (not just the file
   tree), compare it against C sources under `runtime/libc`, and verify that
   the produced archive has the expected member count.
5. Inspect the native single-applet ELF for AMD64 ET_EXEC, absent dynamic
   interpreter/dependencies, and absent undefined symbols.

**Important:** BusyBox host objects were compiled against Linux headers and
may reference Linux-specific ABI symbols and implementation details. The
symbol set difference is a *starting point* for porting, not a list of
functions guaranteed to be missing from a native BusyBox build. Likewise,
a symbol with the same name in both sets may have incompatible prototypes
or semantics. Static linking also does not prove the program can run.

The initial native `true` link uses a tiny purpose-built replacement
for the BusyBox `libbb.h` header and a diagnostic launcher. It is **not**
a target multicall BusyBox link. The existing separate native QEMU workflow
is responsible for guest execution evidence.

## Reports

Each run uploads a 14-day artifact called `plan2001-libc-abi-audit`:

| Report | Contents |
| --- | --- |
| `libc-audit.json` | Machine-readable source SHAs, compiled libc manifest, actual archive counts, header inventory, categorized candidate external symbols |
| `libc-audit.md` | Human-readable scope, status, notable sources omitted from the native archive, representative gaps |
| `plan2001-libc-defined.nm.txt` | Global symbols exported from the real native archive |
| `plan2001-startup-defined.nm.txt` | Global symbols from actual target startup/stubs |
| `busybox-host-defined.nm.txt` | Global definitions in the Linux-host BusyBox control objects |
| `busybox-host-undefined.nm.txt` | Their unresolved references before BusyBox-internal resolution |
| `native-link.md`, `native-link-elf.txt`, `native-link.log` | Actual Plan2001 single-applet link results and ELF inspection |
| `host-control.md`, `host-build.log` | Separate Linux host control evidence |

The workflow job summary shows the human report if all prerequisites complete.
A failed prerequisite is a **red CI failure**, not an invented successful
audit. The artifact upload still runs on failure for diagnosis.

## Local reproduction on Debian

Requirements: Python 3, Git, GCC 14, GNU binutils, GNU Make, and `file`.
Use throwaway source/check-out and report directories:

```sh
python3 -m unittest discover -s tools/ci -p 'test_libc_audit.py' -v
python3 tools/ci/fetch_sources.py \
    --lock config/sources.json --dest /tmp/plan2001-audit-src \
    --reports /tmp/plan2001-audit-reports

(cd /tmp/plan2001-audit-src/kernel && \
    CC=gcc-14 tools/build-mpy-gcc.sh -j 4)
python3 tools/ci/host_control.py \
    --busybox /tmp/plan2001-audit-src/busybox \
    --reports /tmp/plan2001-audit-reports
python3 tools/ci/native_probe.py \
    --kernel /tmp/plan2001-audit-src/kernel \
    --busybox /tmp/plan2001-audit-src/busybox \
    --reports /tmp/plan2001-audit-reports --cc gcc-14
python3 tools/ci/libc_audit.py \
    --kernel /tmp/plan2001-audit-src/kernel \
    --busybox /tmp/plan2001-audit-src/busybox \
    --reports /tmp/plan2001-audit-reports
```

The source fetcher deliberately refuses an existing checkout. These
`/tmp` paths are examples of scratch space, **not** Plan2001 GE/O volumes.

## Follow-on work

**Historical follow-up (now implemented in a separate native workflow):**
For genuine BusyBox `echo`, the acceptance test had to compile the
target's real `echo_main` and its actual required BusyBox library code
against native headers and native C runtime. Add adapters only in userland:
PVD, descriptors, environment, exit/stdio. Preserve native capability
enforcement and do not introduce Linux syscalls or pathname operations
into the kernel. A separate Plan2001 QEMU test must then verify console
output and process completion. The native single-applet `true` link and
the Linux host-control `echo` are not that test. The later native target,
ash and multicall workflows now provide separate guest evidence; consult
the exact workflow run and kernel SHA before claiming compatibility.
