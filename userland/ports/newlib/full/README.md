# Native Newlib 4.6.0 libc for Plan 2001 AMD64

This is a **working upstream Newlib libc.a/libm.a port** for the
native Plan 2001 AMD64 OEXECUTE ABI. No Plan 2001 libc-user.a
is linked into the target Newlib applications.

Verified pipeline: [GitHub Actions #43586391276](https://github.com/plan2001-kernel/userland/actions/runs/43586391276).
The **newlib-full-libc-amd64** artifact contains full libraries,
native ELFs, symbol audits and real QEMU serial logs (14-day retention).

## Reproducible inputs

- Upstream Newlib 4.6.0 commit: 8ba4275b83ec27529f67e0d477611fa6d8d6e6bd.
- Pinned Plan 2001 kernel: 82814608dff31f2b33e34689821c468b69d7641e.
- GitHub Ubuntu 24.04, GCC 14, Clang/LLD, QEMU/OVMF.
- Newlib built for x86_64-unknown-elf as freestanding, single-threaded,
  with external native syscall adapters and no host Linux libc.
- Actual commands: .github/workflows/newlib-libc.yml.
  Use Actions workflow_dispatch on the experimental branch.

## Verified CI stages

1. Sparse-checkout Newlib tag and check its exact commit SHA.
2. Compile complete upstream libc.a, libm.a and public headers.
   Audit printf, snprintf, malloc, free and reentrancy symbols.
3. Compile the same-checkout native kernel, UEFI loader, MicroPython and ABI stubs.
4. Compile native startup and target syscall adapters against real
   Newlib headers, not Linux headers.
5. Link two AMD64 ET_EXEC images without Plan9 libc-user.a; audit exactly
   two PT_LOAD segments, no dynamic interpreter/dependencies, and all
   required symbols resolved.
6. Boot newlib-boot.elf in actual Plan 2001 QEMU; check Newlib
   printf/snprintf, malloc/free, strtol, errno, libm sin, stdin fgets
   from capability Region, stdout via ObjOpenConsole, gettimeofday
   from native _NSEC, and POSIX sleep(1) taking seconds, not milliseconds.
7. Boot MicroPython; spawn newlib-exit.elf as two independent ordinary
   processes, and observe Newlib exit(0)/exit(7) via Process Handles.

All images are newly created disposable regular-file GE/O volumes.
Never pass a real block device to the CI volume creation scripts.

## Architecture and native ABI

Plan 2001 starts the ELF at its existing AMD64 startup.S entry:
RAX initially holds Tos and RSP contains argc/argv.
p9_newlib_start.c calls main() and then **Newlib exit()**.

The native boundary is in plan2001_syscalls.c and posix_forward.c.
Only the generated syscall stubs from the kernel are linked.
The native sleep/alarm stub symbols are renamed before linking
because they take milliseconds, unlike POSIX sleep(seconds).

| API | Implemented behavior |
| --- | --- |
| read(0) | Authorized read-only capability Region ring + WAIT |
| write(1/2) | ObjOpenConsole and ObjOpWrite via OBJINVOKE |
| sbrk | Shared native BRK_ heap break, no second allocator |
| exit | Native EXITS: empty on success, nonempty on failure |
| gettimeofday | Native _NSEC nanosecond real-time clock |
| sleep | Seconds converted to bounded native milliseconds |
| fstat/isatty/close | Dispatch through userspace open descriptions |
| dup/dup2/fcntl | Shared offsets, independent descriptor flags, bounded allocation |
| pipe | Two native-region UXPIPE endpoints; reference-counted close and EOF |
| lseek | ESPIPE for streams |
| getpid | Unix projected PID, native SYSCTL for standalone processes |
| fork/execve/waitpid/signals | Bounded [userspace process projection](../../../docs/UNIX-PROCESS.md) |

## Deliberate limitations

This is a **real full libc port**, not a claim of complete POSIX support.
The library is configured single-threaded; TLS, concurrency and all
optional APIs are not certified. stdin needs an explicit granted Region
and stdout/stderr native console authority; children do not inherit
rights automatically. Explicitly attached UXFS services supply file backends;
the bounded descriptor table supports redirection, UXPIPE inheritance, and
shared volatile file descriptions across fork/exec. Persistent UxService
callbacks and Unix TTY semantics remain outside this inheritance profile.
The Unix runtime encodes exact exit codes in native EXITS messages; foreign
native applications retain their success/failure distinction. sbrk does not
shrink. Process-group selectors and terminal job control are not implemented.

No kernel ABI changes, physical hardware tests or full GNU/BusyBox
Newlib linking were made. The prior Newlib-subset/libgloss QEMU tests
remain independent regression coverage. Future Unix semantics belong
in Userland adapters, not new kernel syscalls.
