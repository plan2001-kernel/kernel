# Stage 3: Unix init and F5 boot handoff

## Boot model

On AMD64, the Plan2001 kernel remains the boot-session and keyboard owner.
Its v1 boot manifest launches independent processes in console slots, and
provides each process with explicitly authorized stdin/stdout/stderr rings.
The manifest F5 slot starts the native **unix-init-f5-plan2001.elf** from a
verified GE/O executable object. This requires no new kernel syscall, Unix
VFS, guessed OID or elevated ObjectSpace rights.

The ELF has its own native startup and application main(). After checking
that the kernel provided a valid console-session grant triple, main() calls
p2001_unix_init(). Init creates the bounded, memory-only Unix PVD from scratch,
checks that cwd is exactly "/", then hands that same PVD to the F5 shell
entry point. The first prompt is **unix:/$**. Further prompts show the
current directory. F5 starts in Unix root even after an earlier session
changed cwd, because PVD state is never persisted between boots.

The interim shell accepts pwd, cd PATH and ls [PATH]. It retains the
upstream BusyBox echo_main console diagnostic, but the command reader is
NOT BusyBox ash or a general POSIX shell. This is a **single native process**:
unix-init calls its statically linked shell entry in the same address space.
There is no independent shell process, generic POSIX exec, descriptor
inheritance, job control, fork, pipe or external applet launch yet. Those
depend on later stages. The kernel does not rely on unix-init to keep
SystemConsole or other boot sessions alive.

## Validation and limits

- Host test: init calls the shell only after resetting the PVD root, and a
  second invocation rebuilds root after cwd had changed. It reuses the real
  builtin pwd/cd/ls C code.
- Native target: build an AMD64 static ET_EXEC without PT_INTERP or unresolved
  symbols, link Plan2001 libc, and verify init, PVD and shell symbols.
- QEMU: use the latest kernel main, real boot manifest, a disposable GE/O
  image, QEMU PS/2 key events, and actual VGA framebuffer decoding. Verify
  boot at unix:/$, pwd output /, cwd changes, other-session isolation.
- Reboot QEMU with **the same disk image**, while the previous shell cwd was
  /usr/bin. Re-select F5 and verify a fresh init handoff and pwd = /.
  The disk is a scratch regular file. This tests volatile startup state,
  **not** GE/O directory persistence or crash recovery.

The PVD nodes are still only in RAM: /bin and /usr/bin contain no executable
objects and do not imply ObjectSpace or GE/O capabilities. There is no
global userland init hierarchy or service supervision. Boot manifest process
failure recovery remains governed by the existing kernel contract.

This stage does not implement persistent files, POSIX path I/O, upstream
BusyBox ash/sh, physical hardware testing or service management.
