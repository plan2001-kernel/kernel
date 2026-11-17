# Minimal POSIX and Unix userland profile

**Design goals, not a conformance or implementation claim.**

The reference is the familiar *user experience* of Alpine Linux minimal base with static BusyBox. OpenBSD is a secondary reference for command behavior. No Linux or OpenBSD binary ABI is required.

## Interface plan

| POSIX function | Proposed mapping |
| --- | --- |
| `open`, `openat` | resolve PVD path and verify Handle rights; allocate local descriptor |
| `read`, `write`, `close` | descriptor-based authorized backend operations |
| `lseek` | per-open-description position for seekable content |
| `stat`, `fstat`, `lstat` | synthesize metadata from authorized ObjectSpace/GE/O objects |
| `opendir`, `readdir` | paginate bindings with consistent ObjectSpace epoch |
| `chdir`, `getcwd` | move/report process-local PVD view |
| `mkdir`, `rmdir` | projected space creation/removal with explicit lifetime checks |
| `unlink`, `rename`, `link` | binding operations with specified atomicity |
| `getenv`, `setenv` | userspace environment and explicit inheritance |
| `fork`, `execve`, `waitpid` | bounded adapter over SPAWN, RFORKEX and process Handles |
| `pipe`, `dup2` | userspace streams and descriptor mapping |
| `signal`, `termios`, `isatty` | asynchronous signals via the native alarm service; limited terminal adaptation |

A missing interface must report a correct error instead of a fake successful stub. Merely linking an API symbol does not prove its POSIX semantics.

## Required corner cases

Root confinement, `.`/`..`, aliases and duplicate labels, cycles/multi-parent traversal, stale or revoked capabilities, unbind while fd remains open, concurrent enumeration epochs, persistent updates across reboot, interrupted publication, and denied guessed-OID access.

The first profile does **not** include Linux `/proc`/`/sys`, kernel Unix permissions, Linux syscall numbers, dynamic linking, generic networking, package management or full shell job control.
