# BusyBox ash / Newlib POSIX compatibility on Plan2001

This document distinguishes a reproducible **upstream ash source compilation**
from a functional Unix shell. The former is tested; the latter also requires
full multicall linkage and integration beyond these compilation checks. The kernel capability ABI is
unchanged.

## Pinned sources and build

- BusyBox **1.36.1**, pinned in config/sources.json at
  `1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4`.
- Genuine upstream **Newlib 4.6.0**,
  `8ba4275b83ec27529f67e0d477611fa6d8d6e6bd`.
- Current kernel `main` is fetched afresh in Actions, as required by source
  policy. Nothing from Debian glibc, the Linux startup files, or the
  Plan2001 legacy `libc-user.a` is linked into the Newlib target.

`tools/ci/probe_busybox_ash_newlib.sh` generates the original BusyBox
`include/autoconf.h` with its own Kconfig tooling. It disables unsupported
job control, Bash compatibility, Linux utmp/wtmp and glibc-only feature
probes *in the disposable CI checkout*, keeping upstream `shell/ash.c`,
`shell/shell_common.c` and `shell/ash_ptr_hack.c` unchanged. The original
three C files are compiled with `-ffreestanding -nostdinc`, the genuine
installed Newlib headers, target-only overlays, and
`-Werror=implicit-function-declaration -Werror=int-conversion`.

The old stage-5 test failed at `sys/times.h` simply because it did **not**
use Newlib's sysroot. Genuine Newlib already ships this header; the replacement
CI checks it, rather than inventing a dummy `struct tms`.

## POSIX implementations that work

In `ports/newlib/full/plan2001_syscalls.c` and `posix_forward.c`:

- `open`, `read`, `write`, `close`, `lseek`, `stat`, `fstat`, `lstat`,
  `unlink` use the **explicitly attached** `UxService` and per-process
  `UxFdTable` through private backend identifiers. `fd_table.c` maps public
  descriptors to reference-counted open descriptions; standard descriptors
  initially refer to supervisor-delegated UXPIPE.
- `dup`, `dup2`, `F_DUPFD`, `F_GETFD`, `F_SETFD` and `F_GETFL` share backend
  offsets while keeping descriptor flags independent. The lowest unused
  descriptor is reused, including 0/1/2 after close. `F_SETFL` rejects
  changes to append/nonblocking status with `ENOSYS`; opening a nonblocking
  description is also unsupported. CLOEXEC is applied by the exec adapter.
- `pipe` allocates two native regions and exposes UXPIPE reader/writer
  descriptions. The last writer close publishes EOF; duplicated writers
  retain the endpoint. The last reader close disconnects; the pair's maps
  and Handles are released after both endpoints close. Process exit closes
  all descriptions. RFORKEX/SPAWN inheritance uses explicit grants and
  transient writer-mapping leases. A disconnected write raises SIGPIPE and
  reports `EPIPE` if the signal disposition permits return.
- `chdir`, `getcwd` and `mkdir` operate on the same PVD and UXFS instance.
  `getcwd(NULL,0)` allocates a copy using Newlib `malloc`.
- `rename` is bounded to UXFS regular-file moves; `access(F_OK)` tests
  existence. Permission queries that would infer rights from synthetic Unix
  modes fail with `ENOSYS`.
- `opendir`, `readdir`, `closedir`, `rewinddir`, `telldir` and
  `seekdir` enumerate a bounded snapshot through the authorized UXFS view.
  `dirfd` fails with `ENOSYS`: no kernel pathname/FD authority is minted.
- `mempcpy` and `strchrnul` are implemented as genuine byte/string
  operations. The target headers resolve BusyBox's incompatible
  `itoa`/`utoa` names without editing upstream ash.
- `uname` describes the native Plan2001 userland target.
  `getrlimit(RLIMIT_NOFILE)` reports the current **1024** descriptor slots
  (`3 + UXFD_SLOTS`, with `UXFD_SLOTS=1021`);
  unsupported budget queries and `setrlimit` fail with `ENOSYS`, never
  silently changing SPAWN budgets.

Newlib is a C library, **not a substitute for kernel-owned POSIX services**.
Target headers for sockets, poll, termios and mmap only make the types visible.
No socket or tty service is conjured into existence. `poll` observes readiness of regular files and UXPIPE descriptors. `mmap` and
terminal control requests fail with `ENOSYS`/`ENOTTY`; fake success would
allow shell code to corrupt descriptor state or infer false privileges.

## CI evidence

- [Stage 5 upstream ash source, strict Newlib compile PASS][ash-ci]:
  the three original upstream ash modules compile to AMD64 objects.
  Stages 4/6/7 of the same workflow also PASS.
- [Native Plan2001 Newlib QEMU PASS][newlib-ci]: stdout/stderr/stdin,
  metadata and PVD paths, directory snapshot operations, and the
  fail-closed resource/tty/poll adapters.
- [Shared GE/O service and same-disk QEMU cold reboot PASS][geo-ci]
  is an independent userland service result, **not** evidence that
  ash itself has been launched.

[ash-ci]: https://github.com/plan2001-kernel/userland/actions/runs/43625250507
[newlib-ci]: https://github.com/plan2001-kernel/userland/actions/runs/43625250523
[geo-ci]: https://github.com/plan2001-kernel/userland/actions/runs/43619613723

## Process and signal adapter

The bounded [Unix process projection](UNIX-PROCESS.md) implements `fork`,
`execve`, `waitpid`, descriptor delegation and signal delivery over RFORKEX,
SPAWN, Process Handles and region capabilities. Its native probe exercises
real fork/exec children, three-stream stdio, CLOEXEC, argv/environment, shared
volatile files and offsets, asynchronous signals, stop/continue, and repeated
signal-handler `siglongjmp` returns. The native alarm service must run in normal kernel boots; kernel PR #64,
merged into `main` on 2026-11-15, supplies that startup. The standalone
ash integration workflow continues to pin an older dependency SHA, whereas
the multicall workflow resolves kernel `main`.

The ash-object undefined-symbol report is an inventory of references before
linking, not a list of currently unimplemented APIs. A reduced upstream ash plus its required libbb modules is now statically
linked by `tools/ci/build_ash.py`. The [hardware boot builder](HARDWARE-BOOT.md)
tests a standalone genuine ash profile with keyboard/VGA, arithmetic,
redirection and a forked pipeline. A separate
[native 30-name BusyBox multicall profile](BUSYBOX-MULTICALL.md) also builds
and tests upstream applets; neither profile is a full BusyBox distribution.
The older F5 stage fixture still has a bounded native builtin evaluator.
Persistent in-process `UxService` inheritance, process groups, terminal job
control, ARM64 and physical hardware remain outside this adapter's validated
profile. The multicall file adapter instead uses delegated persistent
ObjectSpace project capabilities.
