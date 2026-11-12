# GE/O v2: flat persistent object volume

Plan2001-O uses one GE/O **volume** as the persistent identity and
transaction domain. A stored *olio* is an uninterpreted OID-addressed
record; an interpreted object and its ObjectSpace relationships are separate
semantics. **There is no SpaceID dimension in the on-disk identity.**

## On-volume format

- The GE superblock retains `gefs9.00`; bit 63 (`Ovolflag`) selects
  the O profile and the low byte (`Ovolvermask`) is **2**.
  An FS-profile or unsupported O-version volume is rejected, never
  reformatted automatically.
- The GE snapshot tree contains one `Kobj` header, key `07` (one byte):
  value `N[8] K[8] W[8] user[]`. `N` is the published version, `K`
  the oldest retained version and `W` the last begun version.
- Object chunks have key `Kobj | oid[8] | ~version[8] | index[4]`
  (21 bytes). Chunk zero starts with a 4-byte length; `0xffffffff`
  represents a tombstone. The newest object version at or below `N`
  is visible; versions above `N` are unpublished.
- Immutable provenance has key `Kowner | oid[8] | ~version[8]`
  (17 bytes), value `OOWNER01 | OwnerID[8]`. The owner is set on
  first publication and does **not** grant authority. Reads/writes
  require explicit, attenuable capabilities. Missing or malformed owner
  metadata for a live object in owner-aware mode is corruption.
- There is **no** `Ko` name catalog, `store[8]` prefix, per-Space
  version header or SpaceID allocator. The transitional API may still
  accept the boot label `"init"`; it is not a persistent storage scope.

## Volume-global allocator and transactions

- The volume header's user bytes hold the seeded Environment metadata
  (72 bytes), optional `OALLOC01 | nextOID[8]` at offset 72 and
  optional `OOWNMODE | publishedVersion[8]` at offset 88.
- Native automatic allocation starts at OID 64, skips reserved and
  **any previously published OID**, including tombstones, and advances
  the high-water mark in the same transaction as the initial payload and
  immutable owner record. An interrupted pre-publication creation may
  reuse its candidate, but never inherit unpublished owner metadata.
- All O handles for the same volume share one allocator, one published
  version and one active writer slot (`Gefs.otxs`). Allocation and
  commit/sync serialization use a **per-volume** lock
  (`Gefs.oalloclk`); writes through another handle cannot start a
  competing transaction. Independent volumes do not share this lock.
- A transaction stages keys at `W=N+1`; publishing the volume header
  makes its records visible together. Abort removes unpublished object
  and owner records. After a crash, the next writer sweeps unpublished
  keys before reuse. The reader never interprets staged `Kowner`
  records as authority.
- Opening a volume never silently reformats it. A successful commit
  performs GE sync, but this does not eliminate hardware power-loss or
  device flush failure risks.

## Owner metadata upgrade

A v2 volume formatted without owner metadata can be upgraded once on
its first owner-aware write. All published live OIDs are assigned the
trusted seeded owner; tombstones are skipped. Staged owner records and
the owner-mode marker become visible together on the volume's single
published version. This is **not** migration from the obsolete SpaceID
disk format, which has no compatibility promise.

## Boundaries of this phase

The `Ospace` structure, `ospaceopen()`, the `"init"` bootstrap
selector and legacy ABI names are transitional **in-memory wrappers**,
not independent on-disk stores. Removing those wrappers is **phase 3**.
Persisting ObjectSpace graphs as ordinary GE/O objects and validating
their capability reification is **phase 4**. Neither is claimed here.

## Acceptance and inspection

- `tools/host/oinspect.py` rejects unexpected or SpaceID-qualified
  keys, malformed volume headers and inconsistent version order in a
  v2 image; it reports published OIDs and owner records.
- `tools/host/ownerid/run-ownerid-reboot.sh` runs QEMU twice on the
  same disk, testing that independent handles share the OID allocator,
  writer slot, published objects and reboot-persistent high-water mark.
- `tools/host/ownerid/run-ownerid-recovery.sh` tests crash/retry
  points 101–104, owner migration and corruption handling.

The format and native implementation are in `ge/format/ream.c`,
`ge/storage/obj.c`, `ge/storage/o.c` and
`ge/storage/gdat.h`. GE/O storage does not require a directory tree,
9P, path-based open or a synthetic filesystem namespace.
