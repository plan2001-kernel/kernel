# Portable object payload framing (v1)

This is the stable byte-level framing contract for persistent Plan2001
object values, independent of the GE/O B-tree keys. The **target state** is
that all newly published persistent values use this structure; metadata
may be empty or opaque, but the footer is always present. It does **not**
reinterpret GE/O OIDs as file types or add a kernel pathname namespace.
**Implementation status:** the target is not yet activated in GE/O. Existing
GE/O v2 values stay unframed until explicit migration, and this change does
not silently alter readers, ELF loading or boot provisioning.

## Normative binary layout

~~~text
offset 0                            total_length - 32
| RAW bytes, R | METADATA bytes, M | fixed footer, 32 bytes |
              ^ metadata_offset=R  ^ EOF-32
~~~

Raw content always begins at byte 0, unchanged. No CBOR prefix, padding,
escaping or implicit container header is permitted. The metadata may be
any opaque bytes, including empty. **Every framed object has a footer**,
including when metadata length is zero.

The 32-byte footer is laid out in *network/big-endian* order:

| Relative offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 8 | ASCII magic P2OTAIL1 |
| 8 | 4 | Version = 1 |
| 12 | 4 | Reserved flags = 0 |
| 16 | 8 | Metadata offset R (also the raw length) |
| 24 | 8 | Metadata length M |

For total logical object size L, accept the footer only if magic, version
and flags match, R <= L-32, and M == (L-32)-R. Reject integer overflow,
unknown versions and malformed values. Metadata starts at R, ends at L-32.
An unframed legacy value must not be silently treated as a valid framed
value in APIs that require framing. A verifier can parse the footer with
runtime/objtail.[ch] without reading or interpreting any metadata.
This framing does not in itself guarantee integrity or authenticity.

## Recommended metadata, intentionally non-normative

CBOR per RFC 8949 is recommended, and the existing O-Envelope v1
application codec is a practical starting point. **The framing is the
only binding binary contract**. The kernel does not require CBOR, specific
CBOR key names, a metadata schema or even nonempty metadata. Unknown
metadata must remain opaque so future codecs and applications can coexist.

For durable understanding decades after creation, producers SHOULD retain:
* **One object-owned descriptive name per durable OID**, e.g.
  kernel_trap.c, independent of all namespace bindings. Preserve this name
  when the object is bound, renamed or aliased in a Unix/ObjectSpace view.
  Different OIDs may have the **same** object-owned name.
* **Original creation instant**, including timezone or UTC, when honestly
  known. Never silently synthesize an origin creation time.
* **Media/data type** and any format/schema version needed for decoding.
  Distinguish a genuinely unknown type from a guessed type.
* Digest of the **raw** bytes, provenance, and optionally other identifiers,
  relationships and origin observations.

The recommended object-name cardinality is **one descriptive name per
OID when a name is recorded**, not one unique name globally. This is the
object's own metadata, not an ObjectSpace binding label: a single OID can
be bound under many unrelated labels, including randomly selected ones,
and several different OIDs can carry the same object name. Binding or
rebinding must not rewrite the object's own name. The suggested existing
O-Envelope fields are name, created, media_type and metadata;
application-specific fields or nested maps are unrestricted.
An O-Envelope sha256 covers canonical CBOR with its own hash field omitted;
raw content outside CBOR requires its *own* digest. When a Unix name cannot
be represented losslessly as UTF-8 CBOR text, preserve the original byte
spelling in a CBOR byte-string attribute too. A name never grants rights.

Other-object references should identify at least the source volume plus
OID when crossing volume boundaries. A reference never constitutes a
capability, access grant or native ownership assignment.

### Unix userland de facto attributes

Unix file services SHOULD preserve normal stat-family attributes, using
well-defined integer widths and clock units:

| Attribute | Recommended handling |
| --- | --- |
| File kind and mode | Persist file-type and permission/set-id/sticky bits |
| uid, gid | Persist numeric origin identities; never infer kernel authority |
| atime, mtime, ctime | Preserve access, data modification, **inode status change** times; ctime is not creation time |
| birthtime / created | Preserve original creation instant separately when known |
| rdev | Persist special-device identity when applicable |
| ACL, xattrs, filesystem flags | Preserve namespaced metadata where supported |

st_dev, st_ino, st_nlink, st_blksize and st_blocks are generally **derived
by the local Unix projection** from volume mapping, OID identity, current
bindings, and allocation. A producer may record their source observations,
but importing those values must not silently create object links or access
rights. Unix metadata is not the GE/O OwnerID and not a substitute for
capability authorization. The current UXFS v1 manifest persists path,
OID, raw length and kind only, and does not yet implement this profile.

## Views, operations and rollout

* Raw-file read, stat size and Unix EOF expose exactly bytes [0,R).
* Full-object export/import preserves raw bytes, metadata and footer.
* A reader only needs total length and the last 32 bytes to find metadata.
* No claims of fast tail reads or efficient append are made until GE/O
  supports range reads and a suitable update path; current oobjget is
  whole-object.
* A native ELF/MPY executable loader cannot be assumed to accept trailer
  bytes just because its magic starts at offset zero. Audit loaders and
  provisioners before making framed payloads the default.
* The framing contract is not retroactively enabled on existing persistent
  objects or GE/O seed inputs. Migration and file-system integration require
  separate reviewed changes and reboot-persistent tests.

The native provider keeps byte values opaque. Kernel capability rights
remain independent of all metadata and names.
