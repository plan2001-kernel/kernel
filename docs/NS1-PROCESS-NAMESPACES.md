# NS1: process namespace foundation

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

NS1 audits and tests the existing Plan 9-style `Pgrp` lifecycle with native
object bindings. It does not introduce another namespace structure or alter
the GE/O format. The inspected working tree includes the published baseline
`a5a35323c8488557761a6dfca13933d1728681c1` plus the local IKEv2, TinyCC,
OID-collection and GE relations R1 work. The audit uses current source,
not the older snapshot's claims alone.

## Ownership

```text
Proc --one reference--> Pgrp --one owned context--> OContext
                                                    |
                                                    +-- named binding --> Ohand grant
                                                    +-- named binding --> Ohand grant
```

`Pgrp.ref` counts processes and any explicit anchor/temporary owners.
`newpgrp()` returns one owned reference with an empty context. Sharing takes
another reference to that same group. The context has the existing reference
field, but this profile gives each Pgrp its own context; it does not share a
mutable context pointer independently of the group.

Each of the at most **32** bindings owns a grant reference. A local O-handle
slot owns another grant reference. `octxlookup()` acquires a reference before
releasing the map lock. Successful handle installation transfers that
reference to the slot; lookup failure/unwind releases it. Map copies acquire
references to the same grants, never copies of GE object contents.

On final group release, `closepgrp()` releases the context exactly once.
Its final release drops every binding's grant reference and frees the map.
Final grant release closes its native Ospace, aborting any unpublished
transaction; it does not delete the persistent object. Kernel-group anchoring
is intentionally different: one permanent reference remains for the boot's
shared kernel-process group, and each kernel process owns one more.

## Inheritance and replacement

| Operation | Group/map behavior |
| --- | --- |
| `rfork(RFPROC)` | Share the same Pgrp; increment its reference count. |
| `rfork(RFPROC \| RFNAMEG)` | New group and copied binding map; same grants. |
| `rfork(RFPROC \| RFCNAMEG)` | New group and empty binding map. |
| `rfork(RFNAMEG)` | Replace only the caller's group with an independent copy. |
| `rfork(RFCNAMEG)` | Replace only the caller's group with an empty map. |

Both new-group cases preserve the source device mask. No-`RFPROC`
replacement retains the original reference until all fallible resource
preparation succeeds. The existing error handler closes the replacement and
restores the original pointer on failure. Incompatible flags are rejected
before resource changes. Namespace replacement does not itself close local
O handles, but restricted handles still require membership in the **current**
map: clearing the map therefore removes their authority.

`pexit()` detaches the process's group under its debug lock, closes native
handle tables, then releases its group reference. Other process references
keep shared groups alive. A successful executable replacement preserves the
Pgrp and closes process-local O and Pipe handles using the existing exec
hooks. Grant bindings can be resolved to fresh local handles afterward.

## Locks and copy safety

- A published Pgrp's context pointer is stable. Copy/replacement builds an
  unpublished group and swaps the Proc's group pointer; it does not replace
  the context pointer of a group that other processes use.
- `OContext.lk` serializes bind, unbind, lookup and map snapshots. Copying
  the binding array and incrementing grant references happen under that
  one lock, without allocation or native storage calls inside it.
- `Pgrp.ns` protects the inherited device mask, not object-map contents.
  `pgrpcpy()` requires an unpublished, singly owned destination. It allocates
  the copied/empty context first, snapshots the source mask under a read
  lock, then replaces the private destination context. Allocation failure
  leaves its previous context and all source references intact.
- Lookup releases the map lock before the caller takes `Ohand.gate`.
  Bind/unbind release the map lock before dropping an old grant reference.
  Grant authorization/revocation can take `gate` then check membership;
  there is no reverse path holding the map lock while waiting on that gate.

The previous `pgrpcpy()` already allocated before releasing the old map.
NS1 preserves that safety, documents/enforces the private-destination
contract, and removes the unnecessary pair of exclusive mask locks. The
object-context implementation did not require replacement or new locks.

## Demonstrated kernel-process initialization defect

Previously `kproc()` tested and assigned a static `kpgrp` without a lock.
Concurrent first callers could publish different groups and strand the
losing group's anchor reference. Namespace allocation also occurred after
reserving a process descriptor and taking its debug lock.

The new internal `kprocpgrp()` retains the existing shared group policy:

1. Look up the anchored group and acquire a process reference under a short
   publication lock.
2. If absent, allocate a complete candidate **outside** the lock.
3. Publish one candidate as the anchor, acquire the caller's reference,
   and release any losing candidate after unlocking.

No allocation or context destruction occurs under the publication lock.
Allocation failure leaves no initialization lock held. It works during
early boot before `up` exists. `kproc()` acquires the group and prepares
fallible name allocations before reserving a Proc. With a process error
context, preparation failure releases temporary names and the group
reference. Early-boot fatal allocation behavior remains unchanged. The
group reference transfers to the new process and is released by `pexit()`.

Only `kernel/core/pgrp.c`, `proc.c` and `portfns.h` needed production changes.
Pgrp/Proc layouts, RFORK flags, syscall numbers, OContext size and grant
authorization rules remain unchanged.

## Authorization and OSPAWN

Knowing an OID is insufficient. Restricted operations still require a local
handle, current namespace membership, the exact OID, required rights and an
unrevoked grant. Copies retain the same grant/revocation lineage. Sharing
does not issue new grants. A closed or foreign numeric handle cannot become
valid through shared Pgrp membership. Bootstrap authority remains bound to
the original process pointer/PID; neither group sharing nor copying conveys
it to children. The kernel-process group starts empty and is not a user
bootstrap authority source.

OSPAWN intentionally differs from ordinary RFORK:

- Default/EMPTY creates an empty object view.
- COPY snapshots only explicitly selected authorized roles.
- SHARE requires the entire current role set to be explicitly enumerated
  and checked against an atomic map snapshot; it then references the parent
  group. Later map changes are intentionally visible to both.
- Pipe endpoint rights and stdout remain explicit. No O-handle table is
  inherited. A failed view selection or later Pipe delegation failure runs
  the existing aborted-child cleanup path, including group release.

The existing `sysproc.c`/`sysgefs.c` implementations of these policies are
unchanged. NS1 does not add attenuation, creation rights or delegation modes.

## Reproduce and verification scope

```sh
CC=gcc-14 bash tools/test-ns1.sh
```

This runs canonical kernel/MicroPython builds, standalone fault/concurrency
tests, the NS1 VM proof, the original documented N13 proof, and the existing
GE relations R1 two-boot regression, in sequence. It requires the normal
formatter, loader, mpy-cross, QEMU/OVMF and mtools artifacts. All VM disks
are disposable regular files. Native namespace proofs use four TCG vCPUs.

Individual commands:

```sh
python3 tools/host/test-ns1.py
bash tools/qemu-ns1.sh
bash tools/qemu-ns1.sh --n13
bash tools/test-ge-relations-r1.sh
```

The host test compiles the **actual** pgrp and OContext function bodies,
substituting host allocation/error/lock primitives. It injects failures in
Pgrp/context allocation and copy, races first kernel-group acquisition,
checks masks and surviving references, and performs 10,500 map copies
concurrently with rebinding. ASan/UBSan check memory and lifetime errors.
It is not evidence of injected kernel allocator failures in a live VM.

The seeded NS1 Python proof and freestanding OSPAWN/exec observer verify:

- Shared edits, independent copies, empty views, current-process namespace
  replacement, old-handle membership checks, and invalid combinations.
- Parent exit before six shared/copied/empty children; repeated lifetimes;
  shared rebinding while siblings copy/resolve the map on SMP.
- Note-driven termination of blocked children, Pipe EOF, and surviving views.
- The 32-binding limit without partial replacement or widened permissions.
- Exact OID/rights, no bootstrap or foreign-handle inheritance, stale handles,
  shared revocation, and unbind leaving GE bytes intact.
- Default EMPTY and selected COPY/SHARE OSPAWN; rejected incomplete role
  sets; cleanup on failed role selection and on a later Pipe error.
- Successful OEXECUTE preserving the view while closing old local handles.
- Explicit Pipe selection/rights and EOF.

`--n13` extracts and runs the original `N13-OBJECT-CONTEXT.md` proof unchanged
as a seeded module, avoiding minutes of keyboard input. It additionally
checks the documented write/revoke transaction behavior and Pipe transfer
larger than queue capacity, blocked readers, backpressure and EOF. Its
success marker is `N13 PROOF PASS`; NS1's is `NS1 PROCESS NAMESPACES PASS`.

R1's unchanged regression exercises the native REPL and the same disk
across reboot, checking relation OIDs/content plus normal Python persistence.
The native `ge` format and object engine are not modified by NS1.

## Limits

Executed verification on 2026-11-01 is recorded in
[NS1-VERIFICATION.json](NS1-VERIFICATION.json). Canonical kernel and
MicroPython builds passed. The final NS1 proof (`ns1-vm.Ukk8iQ`), original
N13 proof (`ns1-vm.6dvUos`), and R1 reboot test (`ge-relations-r1.QshoR1`)
all used the identical final kernel, verified against each VM's ESP.
All fourteen NS1 phase markers passed, as did host ASan/UBSan checks.
R1 returned OID 64 with unchanged content before and after reboot.
The production kernel delta is 73 added and 15 removed lines in three files.

The namespace is an in-memory 32-binding map, not persistent storage, a
directory hierarchy, a distributed namespace or a complete sandbox. Existing
GE concurrency/durability and finite handle-generation limits still apply.
There is no new network or hardware authorization policy. Kernel-group
concurrency and allocation rollback are stress-tested with host primitives;
a special native kernel allocator fault injector and dedicated returning
kernel-worker test are not added. Native boot, common pexit paths and
kernel-group ownership are separately audited/exercised as described above.

Early bring-up runs (including a corrected use of the process error stack
before `up` existed) are retained as failed logs, not counted as passes.
One R1 run under simultaneous VM load lost keyboard characters; the strict
checker rejected it and the test is rerun with no concurrent VM workload.
