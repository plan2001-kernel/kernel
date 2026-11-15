# Native BusyBox F5 console adapter

CI: [BusyBox F5](../.github/workflows/busybox-f5.yml).
The kernel and userland are separate repositories: CI **always fetches the
current kernel `main` branch** at the time of each run, records that
resolved commit in the uploaded `sources.json`, and uses that detached
checkout for the entire job. BusyBox 1.36.1 remains immutable at the
upstream commit declared in [sources.json](../config/sources.json).
Results are tied to a specific resolved kernel commit, not to an evergreen
promise that the next `main` will work.

## Native session transport

The kernel creates a permanent SystemConsole on F1, and boot manifest slots
1..8 correspond to Ctrl+Alt+F2..F9. In this fixture, three MicroPython REPLs
occupy F2, F3, F4; the native unix-init F5 ELF is manifest slot 4. The console
regions are installed **after OEXECUTE**, and the C adapter discovers them
by `obj_list` rather than raw OIDs or ambient I/O grants.

Three attenuated region Handles are required:

| Console stream | Authorized Handle | Mapping | Backend |
| --- | --- | --- | --- |
| stdin (fd 0) | `VkindRegion`, `Vread` | RO | Kernel keyboard publisher |
| stdout (fd 1) | `VkindRegion`, `Vwrite` | RW | Userland producer, kernel VGA consumer |
| stderr (fd 2) | `VkindRegion`, `Vwrite` | RW | Userland producer, kernel VGA consumer |

All three are checked for `ObjRingHeader` version/magic/geometry. Output
publishes complete fixed-size slots with release ordering, notifies readers
with `REGIONWAKE`, and respects the kernel's acknowledgement sequence.
Input validates immutable published slots with acquire ordering and sleeps
on `REGIONWAIT`, avoiding busy-poll loops. Invalid or absent grants fail
instead of pretending input was read. No direct framebuffer mapping, PS/2
claim, kernel Unix tty, or new syscall is introduced.

The ordinary MicroPython SPAWN test still uses its separate, explicitly
delegated 4096-byte output region, because such children do not receive a
boot manifest console. This is not conflated with the three session rings.

## What is actually tested

The native builder compiles unmodified upstream `true.c`, `false.c`
and `echo.c`, linking them with Plan2001's freestanding libc and a
small userland dispatcher. A third build, `unix-init-f5-plan2001.elf`,
uses the same upstream applet objects plus a native **Unix-init boot entry
and linked builtin F5 shell**, which:

1. Calls the *real upstream* `echo_main` to print
   `PLAN2001-BUSYBOX-F5-READY`.
2. Bootstraps the userspace PVD at `/`, then starts the linked builtin shell
   at `unix:/$ ` and echoes native stdin into stdout. The prompt tracks cwd.
3. On Return, prints `PLAN2001-BUSYBOX-F5-READ:hello` when the user typed
   `hello` and Return.

The test builds the actual latest kernel, UEFI loader, MicroPython,
native libc and GE/O tools. It installs MicroPython at OID
`0x4f45584543000003` and the F5 BusyBox executable at
`0x4f45584543000005` into a **disposable regular-file image**, checking
both ELFs' SHA-256 via the kernel's GE/O inspector. A boot manifest
installs four independent process consoles. QEMU's monitor sends
`Ctrl+Alt+F5` and actual emulated PS/2 key events; the acceptance
checker decodes the real framebuffer using the kernel's bitmap font,
verifies F5 output, checks F1 isolation and returns to F5 to verify history.

Run locally on Debian using the same native and QEMU packages as the
[BusyBox target](BUSYBOX-TARGET.md), plus `python3-pil`. Build the target
first, then use `seed_busybox_spawn.py` with
`--elf .../unix-init-f5-plan2001.elf` and
`--init tests/guest/busybox_spawn_init.py`, and run
`bash tools/ci/guest_busybox_f5.sh KERNEL IMAGE REPORTS`.

**Scope limits:** No BusyBox `ash`/`sh`, no generic POSIX file descriptor
table, pipes, termios, job control, *persistent or native ObjectSpace-backed*
filesystem hierarchy or full `libbb`. A volatile PVD directory tree is now
provided by the stage-2 userspace extension described below.
The read loop is *not* presented as an upstream shell applet. It proves
that the BusyBox-bearing native ELF can output to and read from F5 using
real kernel-granted console regions. Physical hardware is NOT RUN.

## Stage-2 PVD extension

The phase-2 F5 launcher originally constructed a **process-local memory-only**
Unix directory projection with a bounded PVD. Stage 3 moves that construction
into the explicit Unix-init entry. Its small pwd/cd/ls builtins
are tested in the same genuine PS/2 keyboard and VGA QEMU acceptance. No
native ObjectSpace/GE/O Handle is created from a directory name. See
[UNIX-PVD.md](UNIX-PVD.md) for semantics and limits. The F5 launcher is
still not BusyBox ash, and persistent paths remain absent.

## Stage-3 boot and restart verification

The F5 boot manifest loads unix-init, which checks authorized session
regions, initializes a fresh PVD, then invokes the linked shell. The test
boots the **same disposable GE/O disk twice** and verifies that cwd `/usr/bin`
in the previous boot cannot survive the next boot. Shell is a same-process
builtin reader until the later process-launch and descriptor stage. See
[UNIX-INIT](UNIX-INIT.md).
