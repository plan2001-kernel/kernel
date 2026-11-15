# Newlib stdin/stdout/stderr over trusted Unix supervisor UXPIPE

The Plan2001 Unix supervisor retains the kernel's three **non-delegable**
console grants and creates three SPSC UXPIPE streams, each backed by two
independently mapped capability regions. A Newlib executable spawned with
the supervisor's `Oinheritv` grants receives an ordinary process-local
descriptor projection: `read(0)`, `write(1)`, `write(2)`, `close()`, and
`fstat()` dispatch through the shared UXPIPE implementation.

## Automatic startup binding

`ports/newlib/full/p9_newlib_start.c` runs
`p9_full_auto_bind_uxpipe()` before the application's `main()`. This uses
the kernel's own `SYSCTL(SysctlHandleList)` on the **calling process**, not
raw Handle guesses or a Unix kernel syscall. An inherited stream set is
accepted only if it contains exactly these six region grants in order,
plus one independent executable GE/O volume Handle:

| Region index | Stream | Child rights |
| --- | --- | --- |
| 0 | stdin data | `Vread`, `Vdelegate` |
| 1 | stdin ACK | `Vwrite`, `Vdelegate` |
| 2 | stdout data | `Vwrite`, `Vdelegate` |
| 3 | stdout ACK | `Vread`, `Vdelegate` |
| 4 | stderr data | `Vwrite`, `Vdelegate` |
| 5 | stderr ACK | `Vread`, `Vdelegate` |
| other | executable volume | `Vexecute`, `Vdelegate` |

The supervisor grants Vdelegate only on these newly created streams and its
explicit executable capability, allowing Unix children to delegate them again.
The kernel console grants remain nondelegable. Attenuated streams without
Vdelegate still bind for I/O but cannot cross a later fork.

No inherited regions means *standalone* Newlib startup: its pre-existing
explicit stdin-ring API and authorized native console probes remain usable.
Any partial or excessive region-grant set fails closed before `main()`,
rather than selecting an arbitrary writable region or implicitly acquiring
the supervisor's console capabilities. `ObjOpenConsole` is never called
from Newlib's `_write` when supervisor UXPIPE is bound.

The public Newlib `read()` and `write()` forward through
`ports/newlib/full/posix_forward.c` into the native `_read` and `_write`
adapters in `plan2001_syscalls.c`. File descriptors 3+ independently use
the explicitly bound trusted `UxService` and `UxFdTable`. These are
userspace descriptors, **not** kernel Handle numbers.

The UXPIPE protocol uses bounded buffer cursors, acquire/release ordering,
peer-mapping startup rendezvous, backpressure, and explicit EOF. `close()`
publishes a stream close, and `_exit()` also publishes close on still-open
stream endpoints after Newlib's normal exit-time stdio flushing.
When Newlib links `uxpipe.c`, the native millisecond sleep stub is used,
not POSIX `sleep(seconds)`.

## Actual-kernel acceptance

`.github/workflows/newlib-libc.yml` builds pinned upstream Newlib 4.6.0,
a trusted native F5 supervisor, and a separate Newlib child ELF; **no**
legacy Plan2001 `libc-user.a` is linked into the Newlib child. The
disposable GE/O image contains these two ELFs. The real Plan2001 kernel
launches the child using attenuated `Oinheritv` capabilities.

`tools/ci/f5_console_accept.py` in `newlib-supervised` mode injects
physical QEMU PS/2 input and decodes F5 VGA output. It verifies independent
stdout/stderr markers, an input echo produced using Newlib's public
`read(0)`, F1/F5 console isolation, and eventual supervisor session
completion. The original full-Newlib stdio/heap/libm probe and
exit(0)/exit(7) Process Handle tests still run independently.

An initial successful real guest run:
https://github.com/plan2001-kernel/userland/actions/runs/43615955994

**Not claimed:** POSIX terminal/job control, full ash, general persistent Unix
file-service RPC between processes, multi-threaded Newlib, or physical
hardware. The existing kernel ABI and capability model are unchanged.
