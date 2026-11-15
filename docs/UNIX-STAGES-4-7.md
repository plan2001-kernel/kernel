# Unix stages 4–7: capability-aware file projection and stream boundary

This branch builds on the Stage 3 F5 Unix init (PR #9). The implementation
is intentionally incremental; CI separates **implemented code**, a **host
compatibility/mocked-storage test**, and **native QEMU acceptance**. A green
compiler is not equivalent to a working BusyBox ash or persistent Unix root.

## Stage 4: GE/O persistence contract

`ports/unix/uxfs.[ch]` implements a small in-memory directory/file view.
It normalizes relative paths against the existing PVD and bounds `..` at
the root. Initial limits are 64 total nodes, 16 PVD path levels, 47-byte
labels, and 1024 bytes per regular file. These are not POSIX limits.

Commands in the F5 evaluator are `mkdir`, `cat`, `cp`, `mv`, `rm`, `ls`,
`pwd`, `cd` and `echo`. Files appear in the memory-only view **now**.
`ports/unix/uxgeo.[ch]` provides a separate native persistence backend
requiring an explicitly delegated, authorized GE/O `VkindGeVolume` Handle.
It never opens the boot volume implicitly or authorizes operations from
a raw OID. Calling `uxfs_save(fs, &store, known_manifest_oid)` writes each
file as its own GE/O OID, writes a versioned checksummed manifest, and
commits. `uxfs_restore` reopens every object using that *same explicit*
volume authority, checks lengths/duplicates/checksum, and reconstructs the
in-memory view. The caller must create/provision the durable manifest OID.

**Important unfinished boot integration:** Kernel boot-manifest F5 console
processes have session grants, not ambient GE/O write authority. Consequently
the F5 reader does **not** automatically save files to disk. The stage 4
CI exercises a transactional GE/O *mock*, then constructs a fresh RAM view
and restores from the same mock committed storage. **Real QEMU GE/O file
persistence across reboot: NOT RUN.** Real hardware: NOT RUN.

## Stage 5: upstream BusyBox ash (blocked)

The pinned BusyBox 1.36.1 is compiled by the existing native build for
upstream `true.c`, `false.c`, and `echo.c` plus a Plan2001-specific native
`ls` applet. `ports/unix/fnmatch.[ch]` and host/native tests provide the
first freestanding libc filename-pattern matching dependency (no locales or
named POSIX character classes). The stage 5 CI job
separately tries compiling the original `shell/ash.c` with
`-ffreestanding -nostdinc` and the current userland headers, and
publishes a compiler diagnostic. An unsuccessful compile is **FAIL** for
that job, not a reason to suppress stage 6/7 jobs. Full BusyBox ash,
`libbb`, POSIX libc, job control, `fork/exec`, and QEMU ash tests are
**NOT IMPLEMENTED**. The small F5 command evaluator is not called ash.

## Stage 6: native SPAWN/descriptor grant boundary

`ports/unix/uxspawn.[ch]` constructs the current kernel SPAWN ABI with
`Ospawn.flags = 2` and an explicit `Oinheritv` grant array, then waits
on the returned `VkindProcess` Handle. The caller must provide an
authorized executable-volume Handle and a real executable OID.
Descriptors are *userland mappings* of delegated region handles, not raw
integer UNIX FDs inherited by the kernel. The three grant slots are limited
to attenuated read/write/meta rights and reject guessed/nonexistent handles
at the kernel call boundary.

The host ABI test uses a deliberately mocked SPAWN and ProcessWait, and
compiles the native external ls applet. Separately, the real Plan2001 QEMU
BusyBox-SPAWN fixture starts `busybox ls /` as an independent native
process, delegates a RO directory-name region and RW stdout region, waits
on its Process Handle and verifies the bytes. That IS a real external ls
process. It is NOT yet an external `ls` started by the F5 shell. External
execution is enabled only by explicitly installing the
`UxShellIO.external` callback. The normal F5 evaluator keeps `ls`
in-process until a real volume-root launch and stream receiver is wired.
**Independent `ls` in QEMU: PASS in MicroPython SPAWN fixture; F5 hookup: NOT RUN.**

## Stage 7: redirection and stream experiments

`ports/unix/uxshell.[ch]` parses short commands, simple quotes, escaped
characters, one `|` separator, and terminal `>` or `>>`. Examples:

```sh
echo test > /tmp/file
cat /tmp/file
echo more >> /tmp/file
cat /tmp/file | cat
cat /tmp/file | cat > /tmp/copy
```

Redirection stores bytes in the bounded file view. The prototype pipe
runs its stages **sequentially** through a bounded byte buffer. It is not
concurrent `pipe(2)`, supports neither multiple pipe stages nor backpressure,
and does not implement `dup2`. Reject unsupported syntax rather than
returning a fake successful POSIX operation.

## Independent CI / acceptance

Workflow: `.github/workflows/unix-stages-4-7.yml`. Each stage uses its
own job, with independent success/failure and explicit NOT RUN reporting.
Stages 4/6/7 can run locally with:

```sh
bash tools/ci/test_unix_stages.sh 4 /path/to/kernel
bash tools/ci/test_unix_stages.sh 6 /path/to/kernel
bash tools/ci/test_unix_stages.sh 7 /path/to/kernel
```

The existing F5 workflow separately compiles the *same* native F5 program
with the new modules and runs the real console test with two QEMU boots
against a disposable GE/O image. That verifies F5's unchanged root startup
and console I/O, **not** durability of new files or upstream ash.

Security invariants: no pathname authority or owner-equality shortcut, no
silent formatting, no added kernel Unix filesystem or pipe, no hidden
Linux CRT, no unvalidated `ObjOpenVolume` call in userland.

## Follow-on stages 8–11 (same branch and PR)

Stage 8 hardens GE/O publication: modified file contents receive fresh OIDs
and the durable manifest is published by the final commit. A failed commit
leaves the caller's committed file OIDs untouched; restore validates a
temporary complete file/PVD image and only swaps into the live view after
all authorized reads succeed. Host regression covers an old snapshot after
commit rejection, no phantom file after oversize write, and a failed
restore preserving the current cwd. Real multi-boot file persistence remains
blocked on an explicitly delegated volume Handle + provisioned manifest OID
for F5; the bootstrap must decide where to install this grant.

Stage 9 provides an opt-in native F5 external `ls` executor when an
authorized executable volume Handle and executable OID are supplied by
the caller. It serializes the parent-owned PVD listing to a RO delegated
region, launches a native child with Oinheritv, waits on its Process Handle,
and receives stdout from a separate RW output region. No execution from
a pathname or guessed OID occurs. The normal F5 boot has no executable
volume grant, so its fallback remains in-process. Host adapter tests
cover missing grants and child failure, while existing QEMU SPAWN tests
prove the independent `ls` ELF is executable.

Stage 10 adds `uxpipe.[ch]`, an explicitly authorized single-producer,
single-consumer ring over TWO separate 4096-byte regions. The producer
holds writable data and read-only ACK; the consumer holds read-only data
and writable ACK. Both use release/acquire cursors, REGIONWAIT/WAKE,
bounded buffering, backpressure, explicit EOF, and peer disconnect status.
The host regression runs concurrent producer/consumer threads across
10,000 bytes. It does not replace the F5 evaluator's legacy sequential
`|` yet.

Stage 11 adds two Plan2001-specific applets, `uxpipe-send` and
`uxpipe-recv`, for the real QEMU BusyBox SPAWN fixture. The bootstrap
formats the regions, drops its RW mappings, delegates opposite rights
to two independent native processes, and verifies produced/consumed
byte counts (10,000), EOF and consumer acknowledgement after both exit.
This validates native REGIONWAIT/WAKE and separate child processes, not
a full shell pipeline. The actual QEMU result must be read from the
corresponding Actions run; compiling the fixture is not a guest PASS.

Unresolved acceptance gates: GE/O read/write grants for an F5 boot session,
upstream BusyBox ash's `sys/times.h` plus further libc/libbb requirements,
and bridging F5 `|` syntax to two concurrently spawned processes.

## Stage 12: trusted Unix supervisor in F5 (native QEMU PASS)

See [UNIX-SUPERVISOR.md](UNIX-SUPERVISOR.md). The F5 boot-manifest slot
now starts a separate trusted userland supervisor ELF. It retains the
kernel's nondelegable stdin/stdout/stderr session grants and constructs
three independent two-region SPSC proxies (six delegable grants). The Unix
init/BusyBox F5 ELF is now an independently spawned process. The
supervisor delegates a Vexecute-only GE/O Handle to the child, never a
boot-owner Handle, raw OID, synthetic owner or console-region Vdelegate.
It only delegates an independent Vread|Vwrite|Vcreate|Vcommit|Vmeta GE/O
volume capability when explicitly configured with an existing manifest OID;
this storage path remains **NOT RUN in QEMU** and would be volume-wide.

The real F5 dual-boot QEMU acceptance passed in
[Actions run 43585614593](https://github.com/plan2001-kernel/userland/actions/runs/43585614593).
The host grant-policy and native target compilation checks pass in the
independent stage-6 job. Upstream ash compilation remains a deliberately
failing independent stage-5 job.
