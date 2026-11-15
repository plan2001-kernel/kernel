# Newlib Unix process projection

The AMD64 adapter in `ports/newlib/full/process.c` implements bounded Unix
process identities, fork/exec/wait, descriptor inheritance and signals over
the native capability ABI. It does not add a kernel Unix process tree or
pathname syscall. `process.h` describes the versioned context image.

## Creation, replacement and completion

`fork` calls RFORKEX without RFMEM. Ordinary application memory is copied;
only explicitly listed Handles cross the boundary. The child resolves the
generation-tagged destination Handles using its own SYSCTL Handle list.
It remaps descriptor resources instead of dereferencing parent mappings.
The parent retains the returned Process Handle and projects its observed PID
as a Unix PID. No operation opens a capability using a supplied PID.

`p9_process_bind_executable(path, volume, oid)` registers a process-local
executable projection. A registration does not create authority: the native
loader checks the actual volume Handle and Vexecute, and inheritance checks
Vdelegate. A filename, Unix mode or OID alone cannot authorize execution.

Native OEXECUTE clears the Handle table. Consequently `execve` uses SPAWN
with a context region and an explicit descriptor/authority plan. The outgoing
process supervises the replacement and retains the native observation identity;
the replacement exposes the same **userspace** PID and PPID. Its native SYSCTL
PID is different. The proxy forwards completion and parent-directed signals.
This is a Unix runtime protocol, not native exec-in-place or Linux binary
compatibility. Mixing its Unix PIDs with native PID-only interfaces is invalid.

An unsuccessful launch returns without closing descriptors. Successful exec
omits CLOEXEC descriptors, restores argv/environment, retains pending/blocked
signals and ignored dispositions, and resets caught handlers. The context
also preserves executable registrations and outstanding child capabilities,
subject to the native grant bound.

`waitpid` projects repeatable native Process Handle observation into a
once-consumed child result. It supports a specific child or -1, WNOHANG and
WUNTRACED. A stop notification does not reap the child. Exit status is encoded
in `UXEXIT1:hh`; signal death uses `UXSIGN1:dd`. Unmodified native programs
retain their native success/failure distinction. Raw native completion remains
observable independently of this Unix registry.

## Descriptors and shared files

Duplicates share an open description and keep descriptor flags separate.
UXPIPE version 3 retains process endpoint reference counts and adds ordered,
one-read canonical EOF tokens. Tokens occupy cursor slots with a separate bit
map, so all 256 byte values remain ordinary data and REGIONWAIT observes their
publication. The F5 supervisor publishes a token for Ctrl+D on an empty edit
buffer; a nonempty buffer is flushed without a newline or EOF. Consuming a token
returns 0 once without closing the channel, allowing ash to resume input after
cat exits. All endpoints must be rebuilt together for this userspace protocol
version; no kernel ABI changes are involved.

The endpoint reference counts work as follows. Fork/exec increments
one reference per inherited open description, rather than per duplicated fd;
the final process reference publishes EOF or reader disconnect.

After inheritance, the runtime acquires the endpoint's unique writer mapping
for each operation and maps the peer region read-only. It releases writable
mapping leases **before** waiting for data or space. Otherwise a child closing
an unused inherited endpoint could deadlock against its parent's blocked I/O.
Publication uses the existing WAIT/WAKE mechanism. Bounded waits recheck close
state and signals; disappearing transient writers are not interpreted as EOF.

An explicitly attached volatile UxService can be promoted to a capability
region containing the shared UXFS topology, content, open slots and offsets.
An exclusive writer mapping serializes updates. Each process retains its own
cwd selection. The child reconstructs local pointers and code references;
serialized parent pointers are never used for authorization or execution.

Persistent UxService callbacks remain process-local native authority. Fork or
exec of an attached persistent service currently returns ENOTSUP; this runtime
does not silently copy that service or invent a remote GE/O capability. A
separately provisioned persistent file-service RPC adapter is still required.

The trusted F5 supervisor delegates Vdelegate on its newly created UXPIPE
regions and executable Handle so its Unix child can explicitly delegate them
again. The original nondelegable kernel console grants remain in the supervisor.
Standalone native console descriptors cannot cross fork: close or replace them
with explicitly delegated streams first.

## Signals

A process capability mailbox stores pending signals. The native alarm service
delivers periodic notes, permitting delivery even while application code runs a
CPU-only loop. The kernel must start its existing `alarmkproc` in normal boots;
an ALARM syscall with an unstarted service cannot provide asynchronous delivery.
Masks, dispositions, pending bits and stop/continue behavior are userland state.

The adapter supplies sigaction, signal, sigprocmask, sigpending, sigsuspend,
raise, kill and alarm. SIGKILL/SIGSTOP cannot be caught, ignored or blocked.
Default stop signals suspend application execution in a mailbox wait loop;
SIGCONT resumes it and SIGKILL can terminate it there. SIGCHLD observes native
completion and runtime stop transitions. SA_NOCLDSTOP is supported; other
sigaction flags return ENOTSUP. Process-group selectors are not implemented.

Signal-aware longjmp is part of the target link (`--wrap=longjmp`). When a
handler jumps out of a native note, the adapter first returns through NOTED,
then enters genuine Newlib longjmp with the AMD64 SysV register convention.
Otherwise the kernel would retain an outstanding note and suppress subsequent
delivery. The POSIX setjmp overlay saves/restores masks over Newlib jmp_buf.
Private scheduling notes do not by themselves interrupt Unix sleep or pipe I/O.

## Bounds and scope

The profile has 19 descriptors, eight tracked children, four executable
registrations, 64 arguments and 4096 environment bytes. Native RFORKEX permits
16 grants; SPAWN permits eight. Repeated resources are coalesced, and excess
inheritance returns E2BIG. All three independent stdio streams fit a normal
exec together with one context and one executable volume. Additional distinct
file services, volumes or child capabilities can exceed that native bound.

This adapter is single-threaded and AMD64-specific. It does not implement
process groups, terminal job control, Linux syscalls or a persistent Unix file
server. The process probe exercises these APIs with real native executables;
it does not establish that the complete upstream ash/libbb binary is linked.
