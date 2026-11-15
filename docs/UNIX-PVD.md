# Stage 2: userspace PVD and volatile Unix tree

The Present View Descriptor (PVD) is a **process-local, bounded projection**
of Unix directory names. It is not a kernel VFS, not a native ObjectSpace
namespace, and not a source of authority. This first implementation contains
only memory-resident directory nodes. Later stages will attach explicitly
delegated ObjectSpace Handles and GE/O-backed file metadata; *never* infer
object access from the pathname, a raw OID, or matching OwnerID.

## Bootstrapped memory view

The Unix-init invoked by the F5 boot manifest calls pvd_bootstrap() to create:

    /
    ├── bin/
    ├── dev/
    ├── etc/
    ├── home/
    ├── tmp/
    ├── usr/
    │   └── bin/
    └── var/

These entries are directories only, including /bin. They are **not**
persistent or backed by GE/O objects. They exist solely inside the F5
process. Kernel F1 and the MicroPython F2/F3/F4 sessions remain independent.

PVD maintains an explicit selected traversal stack rather than relying on a
native single parent. Absolute paths start at its root; relative paths
start at its current directory. Both "." and ".." are supported; ".." at
the delegated root stays at that root. Failed chdir leaves cwd unchanged.
Unknown names fail with ENOENT-style diagnostics; multiple equal labels
under one parent fail closed as ambiguous rather than picking a BindingID.
Limits are 64 memory nodes, 16 selected directory levels, 47 bytes per
label, and 255 bytes per input path. Directory creation is currently only
the memory-bootstrap API (pvd_mkdir_memory), **not** a user-facing mkdir.

The stage-2 F5 launcher recognizes pwd, cd PATH and ls [PATH]. These are
small **userspace builtins**, not the corresponding upstream BusyBox applets,
not ash/sh and not POSIX fd/path syscalls. They use the existing F5
capability-region console adapter. The prior native BusyBox echo_main still
runs as the F5 startup I/O proof. Stage 3 moved the PVD setup into
unix-init, which hands the initialized view to the F5 builtin shell.

## Tests

Run on a Debian development machine:

    bash tools/ci/test_pvd.sh

This builds the same C view and builtin handler used in the native executable
with host GCC and AddressSanitizer/UBSan. Tests cover root enumeration,
relative and absolute paths, dot and dotdot, confinement, failed lookups,
too-long labels, duplicate names, and the F5 builtin outputs.

The [BusyBox F5 workflow](../.github/workflows/busybox-f5.yml) additionally
builds the freestanding native AMD64 ELF against Plan2001's libc from the
latest kernel main and executes the actual commands on the real kernel in
QEMU, using injected PS/2 keyboard events and decoded VGA framebuffer text.
The workflow archives diagnostic screenshots and source SHAs. Physical
hardware, GE/O file persistence, upstream ash and external
command execution remain **NOT RUN**. Stage 3 adds unix-init and cold-boot tests.

## Stage-3 boot integration

The boot manifest starts the native unix-init ELF on F5. Init initializes
the PVD from scratch and passes it to the linked builtin shell, whose prompt
shows `unix:/$` initially. Two QEMU boots with the same scratch GE/O disk
prove that cwd resets to `/`. See [UNIX-INIT.md](UNIX-INIT.md).
