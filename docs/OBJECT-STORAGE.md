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
- The native O opener rejects an FS-profile volume. Opening a volume must
  not implicitly reformat it.

## Stored definitions versus runtime namespaces

A target process namespace is a volatile capability view independent of
process ancestry, not a GE/O object or persistent OID. The current AMD64
implementation uses reference-counted `Pgrp/OContext`. GE/O may store a
namespace *definition* as ordinary object content, to be interpreted by
an authorized userspace loader when constructing or modifying a view.
The definition's format and loader are not yet implemented. A definition
is neither a snapshot of live Handles nor an authority token: loading it
does not grant access to the objects it mentions.

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
