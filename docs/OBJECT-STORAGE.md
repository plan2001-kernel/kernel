# GE/O persistent object storage

Plan 2001-kernel uses the GE/O object profile on the modified GE volume.
Persistent objects are addressed by a pair `(SpaceID, OID)`. These are local
identities inside a volume, not hierarchical paths. A stored *olio* is an
uninterpreted occurrence; a contextual object is its interpretation or role.

## On-volume contract

- GE's superblock retains the version string `gefs9.00`.
- Superblock flag bit 63 (`Ovolflag`) marks the O profile. The low eight bits
  (`Ovolvermask`) carry format version **1**. The inherited FS profile does
  not set the O bit.
- The flat `Ko | name_bytes` catalog binds names to Space identifiers.
  `init` maps to `SpaceID = 1` in the current seeded volume.
  Catalog names are **flat labels**, not directory components.
- Native object records use `Kobj` records, SpaceIDs, OIDs, versions and
  chunks. Values are opaque bytes, not POSIX file metadata or application
  class instances.
- Immutable owner metadata is a separate versioned `Kowner` record keyed by
  `store[8] oid[8] ~ver[8]` with a small magic plus the 64-bit `OwnerID`. It
  does not wrap or reinterpret the object payload. The owner record, the
  initial payload, and the allocator high-water mark are staged in the same
  transaction and become visible with its one published version. A record
  above the published `N` is not authority; aborted or interrupted creations
  are ignored and swept by the next transaction. The owner is creation-only
  and is never rewritten by content writes, commit, reopen, fork/spawn/exec
  or restart, and there is no independent ownership-mutation call.
- A Space that predates owner metadata is upgraded once, in the first
  owner-aware write transaction: every object published at that point is
  recorded as owned by the seeded volume owner (`root`), and the Space
  header records that owner metadata is now required. Before that upgrade a
  missing record means the seeded legacy owner; afterwards a missing record
  for a live object is corruption, never an implicit `root` grant. A first
  write to an unused OID is a creation and records the writer's trust-domain
  owner atomically; it cannot select or attach an arbitrary owner number.
- The native O opener rejects an FS-profile volume. Opening a volume must
  not implicitly reformat it.

## ObjectSpace target and storage identity

The [ObjectSpace architecture](OBJECTSPACE.md) treats GE/O `SpaceID`
as a private storage allocation/transaction scope, not a graph node or
namespace parent. Persistent ObjectSpaces will be ordinary native GE/O
objects containing versioned BindingID/target/relation metadata. Their
reconstructed capabilities require trusted authorization; persisting a
target OID and rights field alone never mints authority.

The first implementation is volatile. Persistent relations, atomic graph
updates, verified reboot reconstruction and an ObjectSpace-root process
context are not implemented. The v1 storage layout may be replaced without
compatibility or migration; do not silently reformat hardware volumes.

## Historical stored definitions versus runtime namespaces

A target namespace is a lookup/data view independent of process ancestry,
not a source of ownership. Objects have one named owner from creation until
destruction; an OID or namespace label grants no ownership. The current
AMD64 implementation uses capability-based reference-counted `Pgrp/OContext`;
that is an implementation fact, not the new ObjectSpace target. GE/O may store
namespace definitions as ordinary object data for future userspace
interpretation; the format and loader are not implemented, and loading
such data cannot grant access to objects it references.

The `(SpaceID, OID)` pair locates an object but never selects or changes its
owner. The kernel integrity-checks the persistent owner record on GE/O open and
read/write and exposes provenance through `ObjOpOwnerInfo`. OwnerID does not
authorize or forbid an otherwise valid GE/O capability. Existing-object writes
preserve the initial `Kowner` even when a different OwnerID holds `Vwrite`.
A read-only publication mapping does not itself grant access to the stored
object.

Persistent GE/O objects may also represent object sets and OID membership
relations as separate data. Those are not process namespaces and are not
currently an implemented general set/relation engine. Stored object identity,
versions and lifetime are independent of volatile namespace views, role
removal, or last-Handle close.

## Writes and visibility

Published versions are visible to readers; an in-progress Space write is not
published until commit. Closing with an unfinished transaction aborts it.
Do **not** assume that a successful commit guarantees power-loss durability:
device flush errors and recovery behavior must be checked in source and tests.

The exact binary layout, allocator rules, error handling, and callable ABI
come from `ge/storage/o.c`, `ge/storage/obj.c`,
`ge/storage/gdat.h` and `ge/storage/sysgefs.c`, not from historical
migration reports.

Object storage does not require a hierarchical filesystem, 9P service, a
pathname-based open, or a synthetic directory tree.
