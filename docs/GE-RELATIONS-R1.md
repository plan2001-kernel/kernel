# GE relation database demo R1

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

R1 implements persistent native byte objects, persistent tag objects,
directed OID relations, and multi-tag AND queries. It uses the existing
GE/O volume and transaction engine. There are no directories, paths,
SQL, network synchronization or new kernel/syscall interfaces in R1.
The earlier OID-path experiment is separate and is not used here.

## Build and reproduce

```sh
CC=gcc-14 tools/build-mpy-gcc.sh --no-incremental -j3
bash tools/test-ge-relations-r1.sh
```

Prerequisites are the normal built kernel, EFI loader, GE/O formatter and
mpy-cross, plus GCC 14, Python 3, QEMU, OVMF and mtools. Normal build commands
are documented in README.md. `QEMU`, `OVMF_CODE`, `OVMF_VARS` select local
installed tools/firmware when needed. The regression uses TCG and fresh
regular-file GPT disks; it never writes a physical disk.

`tools/test-ge-relations-r1.sh` runs the standalone C tests under ASan/UBSan
and then `tools/qemu-ge-relations-r1.sh`. The latter uses the existing
`qemu-o-persist.sh` helpers to boot one disk twice, type the supplied REPL
commands through native PS/2 input, and inspect the committed GE/O volume
after each boot. There is no format or reseed between the two boots.

The VM test artifacts are in `build/ge-relations-r1.XXXXXX/`:

- `boot-boot1.log`, `boot-boot2.log`: actual native REPL transcripts.
- `o-boot1.img`, `o-boot2.img`: at-rest O-volume snapshots from the same disk.
- `ge-objects-boot1/`, `ge-objects-boot2/`: independently extracted objects.
- `ge-report.json`: verified OIDs, tags, edges, hashes and published versions.
- `report.txt`: existing init/REPL/O-volume persistence regression results.

## Interface

```python
import ge
photo = ge.create(b"JPEG DATA")
ge.add(photo, "#summer")
ge.add(photo, "#Liisa")
ge.add(photo, "#motorcycle")
ge.add(photo, "#jpg")
print(ge.find("#summer #Liisa #motorcycle #jpg"))
print(ge.tags(photo))
print(ge.get(photo))
ge.remove(photo, "#motorcycle")
```

`JPEG DATA` is placeholder binary content, not a valid JPEG image.
`create` returns a fresh **native GE/O OID**, not an identity inside the
serialized Python graph. Unnamed/untagged objects are valid. `get` returns
the original bytes. Strings are not accepted as binary content.

`add` reuses an existing tag object's OID. Duplicate edges and removing an
absent edge are successful no-ops. Removing an edge retains the tag object
and content object. `tags` returns relation names in edge-insertion order;
removing/re-adding an edge appends it. `find` returns ascending source OIDs.
Repeated query terms are harmless; empty queries and unknown tags return
an empty list. Non-string tags/queries and malformed tags raise errors.

Tags start with `#`, contain no NUL or ASCII whitespace, and are validated
UTF-8 strings. Names are exact and case-sensitive; no normalization or
semantic inference is performed. ASCII whitespace separates query terms.
For example `#kesä` works. Tags are application identifiers, not paths.

## Environment-independent implementation

`runtime/ge_relations/relations.{h,c}` contains the data model, wire codec,
validation, tag hash table, reverse index and query logic. It contains no
MicroPython types, Python serialization, filesystem operations or kernel
code. Its C API uses caller-owned state and four storage callbacks:

```text
read(OID, buffer/capacity)     -> content / size / missing
create(bytes)                -> newly allocated, published OID
write(OID, bytes)             -> stage native object replacement
commit()                     -> publish/synchronize GE transaction
```

`micropython/modge.c` is the first adapter. It maps these callbacks to the
existing OSPACEGET, OSPACECREATE, OSPACEPUT and OSPACECOMMIT calls. Future C,
Erlang and other clients can use the same native objects and explicit byte
format without running Python or using a Python heap image.

The module uses the active `init` Space (SpaceID 1 in the demo). The small
`p9_space_relation_handle()` bridge checks that the caller is the active
Space writer, flushes pending Python graph changes, and rejects operation
inside `space.atomic`. Relations are not stored inside `OID_GRAPH`.
The existing Python roots record (logical OID 1), boot interpreter,
Environment and REPL persistence are left intact.

## Persistent format

Each user-content object has its own allocated OID and raw binary content.
Each tag object has its own allocated OID and the exact UTF-8 bytes of its
name as content. Assigning the tag to more sources creates only additional
OID pairs; it does not copy either the content object or the tag object.

One ordinary native object, reserved by the R1 application at
`0xfffffffffffffffb` **within the active Space**, contains relation metadata.
It does not change the GE/O superblock profile or introduce new index key
types. This reservation is separate from existing `OID_ENV` (`...fffc`)
and `OID_GRAPH` (`...fffd`).

All integers below are unsigned and **big-endian**, with no alignment gaps:

```text
offset  bytes  meaning
0       8      magic: 47 45 52 45 4c 52 31 00 ("GERELR1\0")
8       4      format version = 1
12      4      flags = 0
16      4      number of registered objects
20      4      number of named tag objects
24      4      number of directed edges
28      4      reserved = 0
32      ...    registered OIDs: u64 each (includes user and tag objects)
...     ...    tags: u64 target OID, u32 UTF-8 byte length, exact name bytes
...     ...    edges: u64 source OID, u64 target OID
```

Tag names in metadata form the persistent name-to-tag-OID dictionary. Tag
content objects are verified against that dictionary when reopening the
database. Duplicate OIDs, duplicate tag names/IDs, duplicate edges, missing
registered endpoints, invalid UTF-8, invalid counts and trailing/truncated
bytes are rejected. The metadata is a wire format, not a dumped C structure.

## Index and queries

On open, R1 reconstructs an in-memory hash table from tag names to tag
slots and a reverse posting set for each target tag OID. Posting sets are
256-bit source-object sets; an object's bit refers to its slot in the
registered OID table. They are not persisted as a second database.

A query hashes each tag name, fetches its posting set, intersects the sets
word by word, and returns the matching original OIDs in ascending order.
Queries do not scan object contents. Indexes are rebuilt after a metadata
update and after reboot. There is no new B-tree or optimizer.

## Commits and actual persistence semantics

Before implementation, the native APIs and their call paths were inspected
in `ge/storage/sysgefs.c`, `ge/storage/obj.c` and `micropython/p9space.c`:

- OSPACEGET reads published data, supports size queries, and returns the
  selected version. It does not return staged writes.
- OSPACEPUT stages writes; OSPACECOMMIT publishes and synchronizes them.
- OSPACECREATE allocates a fresh OID, publishes its bytes and advances the
  allocator in one native transaction before returning.
- Closing an uncommitted Space aborts its pending transaction. Space writer
  locks do not provide a multi-process relation-level transaction protocol.

R1 publishes each relation update as one replacement of the metadata object
followed by native commit. A new content/tag allocation precedes that
metadata commit. Interrupted allocation/registration can therefore leave an
unreferenced content object. It cannot require a duplicate image for every
relation. I/O failure invalidates the in-memory database state; the next
module call reopens metadata. A commit error can leave outcome uncertain.

The existing `gflushdev()` reports device cache-flush errors without reliably
propagating them to the caller. R1 does not fix or hide this limitation.
The completed test demonstrates persistence across controlled VM shutdown
and reboot, not arbitrary power-loss consistency or production durability.

## Actual demonstrated output

The completed three-photo VM run produced these actual results:

```text
GE R1 ONE [64, 65]
GE R1 TWO [64]
GE R1 FOUR [64]
GE R1 TAGS ['#summer', '#Liisa', '#motorcycle', '#jpg']
GE R1 UTF8 ['#summer', '#Liisa', '#jpg', '#motorcycle', '#kesä']
GE R1 BEFORE [64]
GE R1 FIRST PASS

<second boot, same O-volume>

GE R1 AFTER [64]
GE R1 CONTENT b'JPEG DATA1'
GE R1 SECOND PASS
```

The first boot tested one/two/four relations, no match, duplicate add,
removal and the changed result, re-addition, UTF-8 and content retrieval.
After reboot the same query returned OID 64 and the exact same binary
content. The independent checker verified metadata and content hashes from
both volume snapshots, all tag objects, ten distinct edges, and one native
copy of each photo's content. Native relation metadata stayed unchanged
while normal Python graph commits continued on the second boot.

The existing persistence runner also verified the normal init Counter
advancing from 1 to 2, REPL globals surviving, and unchanged interpreter
bytes. The C regression additionally covers binary NUL/0xff content, Chinese
UTF-8 names, storage failures, resource limits and 5,000 malformed metadata
mutations with AddressSanitizer and UndefinedBehaviorSanitizer.

## R1 limits

- One active database per process, tied to the active Space; only the
  existing active Space writer is supported. No multi-writer protocol.
- 256 registered objects including tag objects, 64 tags, 1,024 relations,
  127 UTF-8 bytes per tag, 4,096 bytes per query, 1 MiB per content object.
- Metadata is rewritten whole; index rebuild is intentionally simple.
- The client sees only R1-registered native objects, not every arbitrary
  object already present in the Space. There is no deletion/GC/import API.
- Edges in R1 target tag OIDs. There is no general named namespace, relation
  inference, OR expression language, replication, UI or stable migration API.
- External raw writes to R1 objects and incompatible future format changes
  are outside this single-writer demo's guarantees.

Modified integration points: MicroPython module/build registration,
`p9space.c`'s active-handle bridge, and the new independent C library and
host/VM regression tools. No kernel, syscall ABI or disk-engine code change
was needed for R1.
