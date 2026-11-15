# Unix object metadata profile over raw-first GE/O values

This document defines the **recommended** Unix-facing metadata profile for
the portable object footer. The fixed binary envelope layout is specified
by the kernel's docs/OBJECT-TAIL.md (draft PR #68). It is the **only**
binding part of the envelope contract: raw bytes start at offset zero,
optional metadata follows raw bytes, and the final 32 bytes locate it.

## Preserve the original name above everything else

A portable object should record **one object-owned descriptive name per
OID**, e.g. kernel_trap.c, when its producer supplies a name. It is highly valuable provenance long
after a project disappears. Do not replace this original name when a local
path is renamed. Maintain the **current** path/label separately through
UXFS/PVD/ObjectSpace. One OID may have multiple unrelated binding labels;
those labels are not additional object-owned names. Different OIDs may
share the same descriptive name. A rename or alias never rewrites it.
The name is not an OID, authorization credential or executable selector.

The other essential provenance is the original **creation instant** (when
known) and the **media/data type**, ideally with a codec/schema version.
If creation time is unavailable, mark it unknown rather than mislabel the
current machine's import or restart time. When importing Unix names with
non-UTF-8 bytes, preserve their byte form rather than normalizing away
information.

## Unix stat and extended attribute coverage

A Unix-facing service SHOULD preserve the following as portable origin
metadata, using signed 64-bit nanoseconds for time and appropriately sized
unsigned integer values for numeric identities. These are metadata
descriptions; kernel capability admission remains independent.

| POSIX / common extension | Recommended persistent record |
| --- | --- |
| st_mode | file kind and permission, setuid, setgid and sticky bits |
| st_uid, st_gid | numeric owner and group at origin; optionally their names |
| st_atime / st_atim | last access time, with known/unknown state |
| st_mtime / st_mtim | last content modification time |
| st_ctime / st_ctim | last inode *status change* time (not creation) |
| st_birthtime / statx btime | original birth time if available |
| st_rdev | originating device ID for special files |
| file flags | platform-specific immutable/append/hidden flags, if available |
| ACLs | original access-control lists, namespaced |
| xattrs | extended attributes, byte-preserving keys/values |
| st_dev | local projection derived from volume and filesystem context |
| st_ino | local projection derived from durable (VolumeUUID, OID) |
| st_nlink | current link count derived from the live namespace/bindings |
| st_size | exact RAW byte length, **excluding** CBOR and footer |
| st_blksize, st_blocks | local I/O geometry and allocation, not portable truths |

Source observations for derived fields MAY be retained separately, but
cannot overwrite their new-system values. A uid/gid/mode value cannot grant
Plan2001 rights. Unknown metadata remains unknown; a service must not
quietly return fake creation dates or successful chmod/chown operations.

A de facto CBOR profile can keep the human-friendly original name in
O-Envelope's name, creation time in created and the format in media_type.
Other metadata can live in nested maps (such as metadata.unix) and opaque
application fields. Key names here are **suggestions**, not requirements for
the fixed footer, and should be versioned by the userland producer. A digest
of the raw data should be recorded separately from the O-Envelope map hash.

## UXFS adapter invariants

UXFS must retain this separation: (1) persisted raw bytes and metadata
belong to one durable object, (2) PVD path and ObjectSpace bindings are
local view relationships, (3) an import/export operation can select either
exact RAW bytes or the full framed object. A normal read, cat, write and
lseek expose only RAW bytes; stat size and SEEK_END use its length.

For metadata-bearing files, a persisted UXFS implementation needs to:
* encode a trailing CBOR metadata document and the fixed footer on every
  newly published file-object version, with matching raw length;
* recover metadata on reboot, even if a local path has changed;
* preserve the original name and original creation instant on rename;
* update modification/change times when actual edits occur, and separately
  record new creation/import times when creating distinct objects;
* project stat/fstat with the ordinary fields above and reject unsupported
  chmod/chown/ACL/xattr requests honestly; and
* preserve one GE/O object version's raw data and metadata together.
  No guessed OID or copied OwnerID authorizes access.

**Implementation:** native ELF/MPY provisioning and loading use validated
footers. UXFS v3 publishes each file as raw bytes, CBOR metadata and footer
in one GE/O version, and publishes its framed manifest only after staging
all successor files. Original name, known creation time, media type,
mode/uid/gid/rdev and known atime/mtime/ctime/birthtime are restored. Unknown
metadata keys, including producer-specific ACL/xattr values, survive rewrites;
non-map opaque metadata remains opaque. The optional source raw_sha256
field is cleared on UXFS publication rather than retained as a stale digest;
offline inspection computes the current raw digest directly. This does not implement chmod,
chown or ACL/xattr modification APIs: unsupported operations still fail.
Raw reads, stat size and seek expose only file bytes. The manifest and file
footer are mandatory; older unframed manifests are explicitly rejected.
Use a freshly provisioned image or an explicit offline migration, never a
silent reformat. Generic GE/O values outside these producer paths remain
opaque; this is not a global conversion of every existing OID.
