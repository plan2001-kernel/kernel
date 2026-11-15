# Integrated Plan2001 Unix userland: Newlib, GE/O and UXPIPE

Integration branch: `feature/unix-newlib-geo-integration`. Parent is the Unix
supervisor/stages branch. Kernel unchanged; GitHub Actions builds and boots
the REAL Plan2001 kernel rather than emulating Linux syscalls.

## Implemented

- Full upstream Newlib 4.6.0 `libc.a`/`libm.a` links into static native
  AMD64 Plan2001 ELF without kernel `libc-user.a`. Existing supervisor and
  BusyBox child still use the kernel's own libc. Never mix two libc archives
  into the same ELF.
- `p9_full_bind_uxpipe()` attaches explicit native region capabilities to
  Newlib FD 0/1/2, using the same UXPIPE SPSC data/ACK protocol as supervisor.
  Native QEMU tests verify actual region maps, bytes, EOF, fstat, and lseek.
- `p9_full_bind_files()` attaches descriptors 3+ to an explicitly initialized
  **in-process** trusted `UxService` with canonical UXFS/PVD view. Newlib
  `_open/_read/_write/_close/_lseek/_stat/_fstat/_unlink` use this descriptor
  table. No unbound ambient pathname open; an independent Newlib probe ELF
  uses this code and tests the real kernel. This is not cross-process RPC.
- F5 shell uses `uxservice_command()`, `uxservice_sync()` and a delegated
  GE/O volume capability to persist mutations. CI provisions a checked
  manifest on a NEW disposable disk with the existing kernel formatter.
  Two separate real QEMU boots of that SAME disk verify file restore.
- A separately SPAWNed `ls` receives read-only listing and write-only output
  frames. `echo ... | cat` uses TWO distinct native SPAWN programs and TWO
  attenuated UXPIPE streams, with explicit EOF, reader backpressure and
  Process Handle waits. The F5 QEMU test verifies extra kernel SPAWN log
  entries and real PS/2 input + VGA output.
- Host tests verify two descriptor views sharing one in-process UxService,
  path confinement, redirection, and shell invocation.

## Boundaries / unfinished

UXFS remains a 64-node, 1024-byte-file bounded prototype. The service
has a single owner per process; separate Unix client processes do not yet
have file-server RPC. The process-local FD table is not inherited by SPAWN.
Generic pathname-to-executable resolution and ObjectSpace graph traversal
are not implemented. Full concurrent multi-stage shell pipeline, arbitrary
external commands, POSIX fork/exec/wait, signals, termios, rename/unlink open
FD lifetime, and upstream BusyBox ash are not implemented. Only
`echo ... | cat` is routed to the concurrent SPAWN backend; the other
supported simple pipeline expressions retain a bounded sequential buffer.
No kernel Unix VFS, Pipe, or syscall API has been introduced. No real
hardware tests have run.

## CI

- `.github/workflows/newlib-libc.yml` native Newlib ELF + UXPIPE + files.
- `.github/workflows/unix-geo-persistence.yml` two real kernel cold boots
  of one disposable persistent GE/O disk.
- `.github/workflows/busybox-f5.yml` PS/2, VGA and shell native SPAWN pipe.
- `.github/workflows/unix-stages-4-7.yml` host tests; independent stage5
  upstream ash compile dependency audit still **FAIL** (no sys/times.h).

The integration PR is a draft: CI must distinguish implemented tests from
missing POSIX features and no merge is implied.
