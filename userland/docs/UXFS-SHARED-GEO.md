# Shared GE/O-backed Unix file service over UXPIPE

> **Historical integration profile, not the current multicall filesystem.**
> This document records the bounded `UxService`/UXPIPE RPC fixture. The
> native BusyBox multicall profile uses persistent ObjectSpace project
> Handles directly; see [BUSYBOX-MULTICALL.md](BUSYBOX-MULTICALL.md) and
> [ARCHITECTURE.md](ARCHITECTURE.md). Limits and statements about missing
> upstream ash below refer only to this earlier test configuration.

## Implemented topology

The trusted native F5 Unix supervisor retains the kernel session authority.
It starts an independent Unix-init F5 process with six attenuated console
regions, one execute-only GE/O capability for launching known executables,
and (only when a pre-provisioned manifest is selected) a separate
`Vread|Vwrite|Vcreate|Vcommit|Vmeta` GE/O storage capability.

Within Unix-init, `UxService` is the **single authoritative UXFS/PVD owner**.
The shell's builtins, redirects, and the trusted `UxFdTable` use this same
view and the same `uxgeo_bind` backend. Durability uses
`uxservice_sync() -> uxfs_save() -> obj_commit()` and publishes the
checksummed manifest only after staging the file objects. At boot the owner
calls `uxservice_bind(..., restore=1)` with its already-delegated volume
Handle and an explicitly provisioned manifest OID. An OID alone never
authorizes GE/O operations, and no code automatically opens a privileged
volume.

The new `uxrpc.[ch]` maps that **same service** into a bounded client/server
protocol over two UXPIPE channels: one for requests, one for responses. Each
channel uses two native regions with a single RW producer and RO observer.
Each RPC session gets its own 16-slot userland FD table (fd numbers 3+),
without a copy of the filesystem. Every `open/read/write/seek/close/stat/
unlink` request is performed by the trusted owner. `write` and `unlink`
therefore use the same GE/O commit path as the shell. Version, sequence,
packet length, and path checks fail closed.

`uxrpc_external_run` spawns a **separate** `uxrpc-client` process in the
same native F5 executable using the already-authorized Vexecute launch
Handle. The child receives only these four attenuated `Oinheritv` grants:

| Region | Child rights | Purpose |
| --- | --- | --- |
| request data | `Vwrite` | Publish bounded file requests |
| request ACK | `Vread` | Read server acknowledgement |
| response data | `Vread` | Read response payload |
| response ACK | `Vwrite` | Publish consumed response cursor |

**No GE/O volume, Vexecute, boot console or storage Handle is inherited
by the file client.** The child checks that its own Handle list contains
exactly those four region grants in that order. The server constrains all
client pathname operations to normalized `/tmp` paths, even when `..`
appears in a request. The launch OID is never authority.

## F5 examples

```text
unix:/$ echo shell > /tmp/shared
unix:/$ uxfile get /tmp/shared
shell
unix:/$ uxfile put /tmp/fromclient remote
unix:/$ cat /tmp/fromclient
remote
unix:/$ uxfile put /etc/forbidden secret
uxfile: output failed
```

These `uxfile` commands are a small **client harness** for proving the
shared backend, not a claim that BusyBox ash or arbitrary POSIX
`open(2)` from every spawned process already speaks this protocol.

## Acceptance evidence

- **Real Plan2001 QEMU, two cold boots and one durable GE/O disk: PASS.**
  After the shell writes `/tmp/persisted`, a separate native UXPIPE
  client reads it. The client writes `/tmp/fromclient`, and the shell
  immediately reads it. A denied `/etc` write creates no file.
  The guest reboots using the **same** writable GE/O disk, then both the
  shell and another new native client successfully read the client's
  persisted file.
  [GitHub Actions evidence](https://github.com/plan2001-kernel/userland/actions/runs/43619067049).
- **Host Stage 7: PASS.** The production RPC, file-descriptor and UXPIPE
  code runs across separate pthread endpoints over a mocked kernel
  WAIT/WAKE and GE/O disk. It checks normalized path denial, shell-to-client
  and client-to-shell visibility, and checked manifest restore.
- **Native Newlib and normal F5 regressions: PASS** in independent Actions
  jobs on the same integration branch. Newlib's standard descriptors 0–2
  remain mapped through the trusted supervisor's UXPIPE grants.

## Limits and security invariants

The current service runs within Unix-init and handles **one RPC client at a
time** while the shell waits for completion. It is not a separately
scheduled service daemon, a concurrent multi-client transaction coordinator,
or a general network filesystem. POSIX fd 3+ in Newlib still requires
explicit in-process `p9_full_bind_files(UxService*)`; automatic remote
Newlib fd dispatch is **not yet implemented**. The RPC client is an ordinary
Plan2001-native executable, not a Newlib program.

The original UXFS v1 acceptance profile had 64 nodes and 1024 bytes per
file. Those are **historical test limits**, not the current source limits:
`ports/unix/uxfs.h` defines `UXFS_VERSION=4`, `UXFS_NODES=PVD_MAX_NODES`
(2048 in `pvd.h`) and an 8 MiB shared content arena. The newer persistent
ObjectSpace project adapter does not use this legacy file-content arena.
One RPC request and one response remain protocol-bounded. `uxfile` exposes only
`get`, `put`, `append` and `rm` in this milestone, with the
`/tmp` prefix policy. The original `uxfile` RPC fixture alone does not demonstrate upstream
BusyBox ash, arbitrary POSIX remote-file descriptor inheritance, terminal
control, physical hardware or crash-recovery certification. Upstream ash
and the separate multicall filesystem have newer native tests.

The kernel's existing ObjectSpace/GE/O capability model and
REGIONWAIT/REGIONWAKE implementation are unchanged. All new path and fd
semantics live in `plan2001-kernel/userland`.
