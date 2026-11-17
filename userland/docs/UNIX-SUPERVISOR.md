# Trusted Unix supervisor and F5 capability delegation

> **Supervisor contract.** This document focuses
> on capability-safe console delegation, not the child program's complete
> Unix feature set. Host and guest regressions also exercise the small builtin child.
> See [HARDWARE-BOOT.md](HARDWARE-BOOT.md) and
> [BUSYBOX-MULTICALL.md](BUSYBOX-MULTICALL.md).

The F5 console slot is owned by a **trusted userland supervisor**, not by a
Unix kernel service or an ambient POSIX process. The supervisor is launched by
the existing kernel boot manifest (the explicitly trusted `up->boot` process)
from an authorized GE/O executable OID. The kernel remains responsible for
the keyboard, display routing, session lifetime and ObjectSpace admission.

## Why a console proxy is mandatory

The kernel installs exactly three boot-session regions: stdin
`Vread|Vmeta`, stdout `Vwrite|Vmeta`, and diagnostics
`Vwrite|Vmeta`. **None has Vdelegate.** SPAWN's Oinheritv requires
Vdelegate on the parent Handle, and userland cannot manufacture that right.
Do **not** add Vdelegate to kernel console grants to work around this. The
supervisor keeps those three Handles and bridges them to six NEW regions,
created with `obj_region_create`, whose source Handles do have Vdelegate.

Each stream uses the existing `uxpipe` two-region SPSC protocol:

| Stream | Supervisor maps | SPAWNed F5 child maps |
| --- | --- | --- |
| stdin | data RW, ACK RO | data RO, ACK RW |
| stdout | data RO, ACK RW | data RW, ACK RO |
| stderr | data RO, ACK RW | data RW, ACK RO |

The supervisor reads the kernel's input ring nonblockingly and writes the
stdin UXPIPE producer. It receives stdout/stderr through UXPIPE consumer
channels and writes the original authorized kernel output rings. Only the
kernel owns the original hardware and framebuffer routes. Neither child nor
supervisor can bypass the kernel's one-RW-mapping rule.

The six freshly created regions are formatted *before* the child is
published, and the supervisor unmaps its temporary writer mappings before
delegating a complementary RW Handle to the child. The child verifies all
six ordered grant kinds, exact rights and stream magic/version before
accepting the session.

## Executable and storage authority are separate

The supervisor acquires a `VkindGeVolume` Handle using
`OBJOPEN(ObjOpenVolume)`. This succeeds **only** for the explicit
boot-manifest process, never from its inherited OwnerID, an OID or a name.
The volume must already be activated by the trusted boot loader.

The supervisor opens an **executable source** with
`Vexecute|Vdelegate`, and passes the Unix child exactly `Vexecute`.
It uses the same authorized source to `SPAWN` the Unix child from the
trusted, explicitly configured executable OID. In the child, this grant
permits `uxexternal_ls` to spawn a separate ls child without obtaining
volume rights from ambient process identity.

Persistent Unix storage is **opt-in**. With an explicit *previously
provisioned and checksummed* manifest OID, the supervisor opens a second
volume source with `Vread|Vwrite|Vcreate|Vcommit|Vmeta|Vdelegate`
and passes exactly `Vread|Vwrite|Vcreate|Vcommit|Vmeta` to the child.
Without the manifest selector it does not even open this storage Handle.
Neither source or child has a delegated `Vcontrol` or `Vdelegate` right.

This is a **volume-wide** grant, not a scoped grant to just the manifest
OID. It is only appropriate for a trusted F5 Unix session. Granting storage
to an untrusted shell would allow it to modify other accessible OIDs on
that GE/O volume; a future restricted writable subtree or dedicated volume
would require an explicit further capability design. No claim of per-file
isolation or ambient OID authority is made.

The userland-provided OIDs are selectors only: opening/executing still
requires an authorized volume Handle. The supervisor refuses null OIDs,
malformed selectors, equal executable/manifest OIDs and incomplete inherited
grant sets. The F5 child classifies inherited GE/O Handles by exact rights;
unknown or excessive GE/O rights fail closed.

## Boot and process tree for the supervisor fixture

In a trusted boot manifest the supervisor is configured at the F5 slot:

```ini
[process.unixSupervisorF5]
source=geo:PARTUUID=<configured-partition>:OID=0x4f45584543000005
owner=unixSupervisorF5
args=0x4f45584543000006
required=true
```

This fixture seeds the supervisor ELF at OID `...05` and the independent
Unix-init/F5 child ELF at OID `...06`. With an existing Unix manifest,
add its **trusted** selector as one more argument. The child starts
`p2001_unix_init` and its limited shell evaluator in its own address
space; the supervisor retains the kernel console and observes the child's
process Handle. Invalid completion or bridge errors fail closed and
request child termination instead of silently granting more authority.

The kernel inserts the boot-manifest process name as argv[0], so the
manifest's args field must contain the child OID without an extra
program-name argument. If a manifest contains a redundant name, the
supervisor rejects it and exits rather than attempting to guess authority.
The new supervisor path is tested by the F5 two-boot QEMU acceptance,
the native static ELF checks and the host grant-policy tests. The separate
kernel console and the three old MicroPython sessions remain independent.

## Out of scope

The supervisor does not implement POSIX fork/exec, kernel pipes, TTY/job
control, BusyBox ash or a generic file descriptor namespace. Memory stream
buffers are bounded. Storage QEMU restart requires the caller to provision
a durable manifest OID and intentionally grant write authority; the default
F5 fixture only delegates execute rights. The supervisor does not silently
format GE/O or obtain any kernel volume access through a guessed OID.

## Independent F5 and F6 project roots

A trusted manifest may start the same supervisor ELF in slots 4 (Win+F5)
and 5 (Win+F6). Each supervisor retains its own kernel console grants and
creates separate proxy regions. Its child receives only that session's
proxy grants, executable volume and selected ObjectSpace root.

The explicit supervisor argument form is `CHILD_OID project ROOT_OID`.
The existing one-argument form selects the F5 root at `0x4f45584543000007`;
the two-OID form selects the separate UXFS manifest protocol. A project
selector is interpreted only by the trusted supervisor, using its existing
boot-volume authority. An OID alone grants no access to a child.

`seed_busybox_spawn.py --project-root --f6-project` provisions two empty,
framed durable roots at OIDs `0x4f45584543000007` and
`0x4f45584543000008` in a fresh disposable image. It uses the formatter's
existing single-object and raw-record seed inputs without changing the
kernel ABI or disk format. Existing seed inputs and disk images are never
overwritten. `guest_busybox_f5.sh ... f6` boots both sessions and checks
same-name file isolation and persistence over a cold reboot of the same disk.
This separates the child file views; trusted boot supervisors and boot
MicroPython processes still hold their explicitly configured authority.
