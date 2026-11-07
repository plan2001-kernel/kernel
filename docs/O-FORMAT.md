# O Volume Format v1 — implemented experimental specification

**Scope:** the O-only PoC imported from `pereuna/Plan2001` at
`436ed30c894c4060638341ecebe82004d457f496`.
This document describes the **implemented** O profile and its current
MicroPython payloads. The experimental [GE relations R1](GE-RELATIONS-R1.md)
layer stores a portable OID-pair representation in ordinary objects; it does
not add a native relationship key type to the disk format.
It is an experimental format; it has no production compatibility promise.
See [STATUS.md](STATUS.md) for current verification and the boundary
between implemented behavior and earlier demonstrated results. For the proposed semantics of files as byte-content objects (without mandatory Python classes or a directory hierarchy), see [OBJECT-MODEL.md](OBJECT-MODEL.md). It is **not** a new v1 disk-layout specification.

## 1. GE volume and O profile

O is a profile of GE, **not** a second disk filesystem or allocator.
It reuses GE's superblock, arenas, allocation logs, snapshot key/value
tree, recovery mechanisms and sync machinery. It has no FS root,
directory tree, `adm` tree or 9P service.

The GE superblock retains version string `gefs9.00`.
Its `flag` field distinguishes profiles:

- `Ovolflag`: bit 63 set means native O volume.
- `Ovolvermask`: low 8 bits hold the O format version, currently **1**.
- The inherited FS profile has bit 63 clear (normally `flag == 0`).

The native O opener `oopenblk` (through `geopenblk`) requires the O bit,
version 1 and catalog binding `init -> SpaceID 1`; it rejects an FS
volume. There is no FS opener or fd/pathname fallback in the live kernel
engine. Opening **never** formats a volume, and an
ordinary boot does not initialize or repair missing O data.

References: `ge/storage/o.c`, `ge/storage/load.c`.

## 2. Flat Space catalog

Named catalog records are entries in GE's snapshot key/value tree.
For name `n`, the key and value are:

```text
key   = Ko | name_bytes                 # no terminating NUL
value = record_bytes
```

Names are **flat identifiers**. They are not directory paths and do
not imply parent traversal, path normalization or mount operations.
Version 1 seeds:

```text
Ko | "init"  ->  SpaceID = 1 (unsigned 64-bit big-endian, PACK64)
```

`test.value` is a separate plain `Ko` record used by the GE/O
persistence regression. The current PoC has no general catalog
creation API or native namespace graph. The scope of an OID is its
Space; `(SpaceID, OID)` identifies an object *within the volume*.

Reference: `ge/storage/o.c`.

## 3. Native object records

Object storage reuses GE's `Kobj` versioning engine. Its current values are opaque byte sequences: a future descriptive `file` type or object-property graph is **not encoded by this v1 record format**. A file's bytes are not a Unix-style metadata bundle. The older
filesystem-backed `DMOBJ` adapter uses a file's `qid.path` as its
store ID; **native O** instead uses the stable `SpaceID`. Native
operations enter the engine directly and do not use `fid`, `qid`,
a file path or a 9P object service.

```text
key:   Kobj | SpaceID[8]
value: N[8] | K[8] | W[8] | user[...]

key:   Kobj | SpaceID[8] | OID[8] | ~version[8] | chunk_index[4]
value: chunk_bytes
```

Integer fields use the engine's packed integer representation.
`N` is the latest published version; `K` is the oldest retained
version; `W` is the last begun transaction version. Each object's
visible value is the newest version `<= N`. `~version` means the
bitwise-complemented encoded version, which sorts recent versions
first in the key index.

For a native O Space, the header's `user[...]` part may also carry the
local OID allocator described in section 5.4. This is an application of
the existing header value, not a new key type or a disk-format revision.

For object data, chunk index 0 begins with a 4-byte length followed
by payload bytes. A length of `0xffffffff` denotes a tombstone.
Subsequent chunks continue the payload. Missing objects or
tombstones are not returned as live values. Version `N=0` denotes
an empty Space. The PoC's object read assembles a **whole** value
into a supplied buffer; it does not expose a native streaming cursor.

References: `ge/storage/obj.c`, `ge/storage/gdat.h`.

## 4. Transactions and visibility

- `ospaceget` sees **published** versions, not uncommitted writes.
- The first `ospaceput` begins a Space transaction. Its writes
  are initially invisible to published readers.
- `ospacecommit` publishes the version through GE's tree update
  and synchronizes it. A commit with no open transaction returns
  the already published version.
- The current Space handle has a writer lock; engine transaction
  rules reject concurrent begins for the same SpaceID.
- Closing a handle with an unfinished transaction aborts it.
  GE uses `W > N` to detect possibly abandoned versions and sweep
  them on a subsequent begin.

These are current PoC semantics, **not** a durability, concurrency
or crash-recovery guarantee for arbitrary failures. The engine's
limited retained version history is not a general immutable object
history API.

In particular, `gflushdev()` currently reports a device flush error once
and returns without propagating it: a successful COMMIT does not prove
that the device cache was made durable. A no-transaction commit does not
call `gesync()`. Writable volume recovery can also truncate an allocation
log at its replay barrier; opening without formatting is not equivalent
to read-only inspection. A successful COMMIT therefore does not by itself
prove that a device cache was flushed or that a writable volume is safe to
open after an arbitrary failure.

Reference: `ge/storage/obj.c`.

## 5. Native engine API and system-call boundary

The GE/O kernel API in `ge/storage/obj.c` is:

```text
ospaceopen(volume, "init")                 -> Ospace
ospaceget(space, oid, buffer, len, &ver)   -> length or -1
ospaceput(space, oid, buffer, len)
ospacecreate(space, buffer, len, &oid)     -> allocate and publish
ospacecommit(space)                        -> published version
ospaceclose(space)
```

The PoC's direct user-space syscall interface is implemented in
`ge/storage/sysgefs.c` and exposed through
`runtime/abi/sys.h`:

```text
ovolopen(spec, len)                        -> volume handle
ospaceopen(vol, name, len)                 -> Space handle
ospaceget(space, oid, buf, len, &ver)      -> bytes or -1
ospaceput(space, oid, buf, len)
ospacecommit(space, &ver)                  -> publish and sync
ospaceclose(space)
oexecute(space, oid, argv, argc)            -> execute object (no return on success)
```

`spec` must denote `PARTUUID=...`. Other specifiers are rejected by
`OVOLOPEN` rather than falling back to a device path or `Chan` open. The
legacy `#G` Dev, GEFS root mount and historical probe paths are removed;
there is no alternate device form for this syscall. A `nil` buffer and
length zero request the object's size.
The kernel owns `Gefs` and `Ospace` pointers; callers see integer
handles. This boundary does not use `#G`, a device file, pathname
lookup, 9P or a synthetic filesystem for object operations.

### 5.1 Process-local runtime handles

The on-disk format is **unchanged**. The following is runtime state
only; no handle record is stored in GE/O.

- Each process has its own eight-entry O Space handle table. Resolving
  an integer handle uses only the calling process. The same integer in
  two processes can name different sessions.
- `(SpaceID, OID)` remains the persistent object identity. A handle is
  a process-local runtime session, not an object identity and not a
  durable capability.
- Opening allocates a session and one ownership reference. Lookup
  takes a temporary reference under the table lock; the reference is
  released on success and error. The table lock is not held during
  storage operations.
- Explicit close removes the table entry and drops its ownership
  reference. The last reference closes the native `Ospace`; an
  unfinished transaction is aborted by the existing engine path.
  Cleanup never commits implicitly.
- Process exit releases all remaining handles. The exiting process's
  table is detached before the sessions are released, so one failing
  close cannot hide the others. A reused `Proc` starts with no table.
- A child starts with an empty table, including `RFMEM` children.
  Successful `exec` closes all handles; failed `exec` preserves them.
  Handle inheritance and cross-process transfer are deferred.
- Closing an already closed or invalid handle fails; it does not
  affect another process's table.

The volume instance (`Gefs *ovol`) is still global to this
experiment. Volume acquisition and authorization remain separate work;
these process-local handles do **not** constitute complete capability
security.

### 5.2 Native executable-object loading (bounded experiment)

`oexecute(space, oid, argv, argc)` resolves `space` only in the calling
process, reads one published object value, and follows `exec` semantics:
on success the calling process is replaced and does not return; on
failure the original image and its handles remain. The on-disk format is
unchanged. `argc` is bounded (32); `argc == 0` keeps the original
synthetic `oexec <oid>` argv[0].

The read is a **bounded whole-object read**. The kernel allocates
`8 MiB` (`Objexecmax`) for the object, so this temporary memory cost is
paid even for a small executable, and an object larger than the limit is
rejected. There is no size-then-read pair: `ospaceget` selects one
published version and copies that version's bytes in a single call, so
the header and payload cannot come from different versions. The real
MicroPython executable is 2,191,593 bytes, so the limit is unchanged.

The loader reuses the existing `Exec`/a.out validation and segment
machinery. Before committing it checks the amd64 architecture magic, header
length, text/data/bss sizes, segment bounds and rounding, object length
against `text+data`, the entry point's membership in the text segment,
and the user-address bounds. Segment addresses (`adata`, `abss`, `ebss`)
remain `uintptr`; only sizes are narrower. It then builds ordinary text,
data and BSS segments and the exec stack, and enters the shared
process-transition path.

The `argv` interface is Plan 9's bounded pointer-and-count form. The
copy helper is `execargcopy()` in `kernel/core/sysproc.c`:

```text
Oargmax   = 32 arguments
Oargbytes = 16 KiB, all argument bytes including every terminating NUL
```

The count, the pointer-array alignment and mapped span, every string
pointer, and the aggregate byte limit are checked. The kernel snapshots
the pointer array once into kernel-owned storage, then copies each string
with a hard destination-capacity bound. It finds the accepted length and
terminating NUL only in the bytes actually copied, never by measuring a
live user string and then copying it without a bound. A string without a
NUL inside the remaining aggregate capacity, an invalid or cross-page
pointer, or a bad count returns the ordinary syscall error (`-1`) and
leaves the caller image and O handles intact.

An RFMEM peer can still change pointers, bytes or terminators while the
copy runs. No atomic snapshot of all arguments is promised: the accepted
arguments are the bounded bytes actually copied, and a clean error is
also valid. Unchecked user access, an accepted unterminated argument and
a kernel buffer overrun are not acceptable outcomes. Bounded scans on
the private, NUL-terminated snapshot are used afterwards.

The aggregate limit includes the terminators; the stack construction
additionally reserves `sizeof(Tos)`, the argv pointer array, alignment
and the amd64 entry word, and rejects a stack that exceeds the
16 MiB user-stack segment. `argc == 0` keeps the synthetic
`oexec <oid>` argv[0]. The resulting stack reserves `sizeof(Tos)` first
and places argv and its strings below it without overlap. No pathname,
synthetic file, `Chan`, `Fid`, Qid, mount or 9P operation is involved.
Successful transition applies the current close-on-exec O handle rule.
The current MicroPython arguments are far below `Oargbytes`.

Native segments carry no file image, so the shared loader sets an
internal `SG_NOSWAP` flag on its text, data, BSS and stack segments and
`dupseg()` tolerates a data segment whose `image` is `nil`. This keeps
`rfork` and the page reclaimer from treating the loaded bytes as
file-backed or swappable pages. Those pages stay resident instead of
being reclaimed; the bounded image, BSS and stack are the memory cost.
It does not change the on-disk format.

Canonical objects are seeded through the existing seed directory layout,
now consumed directly by the native formatter:

```text
OID_MP_EXEC = 0x4f45584543000003   real MicroPython amd64 executable
OID_ENV     = 0xfffffffffffffffc   Environment manifest
OID_CODE    = 0xffffffff00000000   base of host-assigned code OIDs
```

`tools/mkovol ... SOURCE` accepts `MPEXEC=FILE`, which is stored byte for
byte under `OID_MP_EXEC`. With `SOURCE` and no `MPEXEC`, the canonical
`build/gcc/micropython.elf` is seeded by default, so every fresh runnable O
image has the interpreter needed by the normal boot path. `NOMPEXEC=1`
deliberately omits it for a missing-object diagnostic image; normal startup then prints the missing
OID and does not fall back to an embedded interpreter. An existing
persistent image is never migrated or reseeded implicitly. The historical
native-executable test objects at `0x4f45584543000001` and
`0x4f45584543000002` are not part of the canonical seed.

The seed builder in `ge/format/ream.c` builds a sorted GE tree
bottom-up from the seed records. Small seeds are still one leaf; a large
executable is split into many leaf blocks with pivot blocks above them.
The seed is therefore not limited to one catalog leaf, and the on-disk
record representation is unchanged. Historically, `/boot/oinit` was the small
embedded bootstrap harness and `/boot/oexec` the native-exec regression
harness. Neither is the loader and neither is a production bootstrap. E12.2
removed those kernel embeddings from the canonical `pc64` image along with the
`root` Dev and `bootdir`. The `oinit` helper remains buildable; the separate
`oexec` regression harness was removed in N9.8.

Limits of this experiment: no program catalog, permissions, dynamic linking,
or general executable-object format support. The canonical `oboot=server`
boot uses the native kernel bootstrap and does not execute `oinit`, does not
mount a root and does not resolve absolute paths. The historical `oinit` `init=`/REPL/diagnostic paths and the
`root=` GEFS mount are **removed from the canonical kernel** in E12.2, not
merely untested. It does not change the on-disk format.

The canonical amd64 configuration `pc64` embeds no MicroPython and has no
`/boot/mp`. Historical test-only profiles are not built.

References: `ge/storage/sysgefs.c`, `kernel/core/sysproc.c`,
the historical `sys/src/cmd/oinit/oinit.c` (removed in N10).

### 5.3 Direct byte-object binding in MicroPython

The built-in `space` module exposes a small PoC binding over the same
native calls; it does not add a storage layer or change the format:

```python
import space
space.oput(42, b"hello\n")   # ospaceput + ospacecommit (publish and sync)
assert space.oget(42) == b"hello\n"
```

`space.oget(oid)` queries the published object size, then reads that
whole value; the size/read pair is retried up to three times if a
concurrent commit changes it. Empty published content returns `b""`;
a missing OID raises `OSError`. `space.oput(oid, data)` accepts
bytes-like data and commits before returning, so it does not depend on
the Python graph's dirty flag or the next automatic commit. Both are
available only while a native `-O` volume and `-S` Space are open;
otherwise they fail with `OSError` rather than falling back to files.

The binding refuses writes to the implementation-reserved OIDs: 0,
`OID_ROOTS`/`OID_FIRST`, the whole region at and above `OID_CODE`
(host-assigned code OIDs, `OID_ENV`, `OID_GRAPH`, upgrade metadata), and
the native-exec test/boot OIDs `0x4f45584543000001`-`03`. Its
whole-object limit is **1 MiB**, conservative relative to the kernel's
64 MiB transaction cap. `space.oid(x)` is unrelated: it is a Python
logical graph OID inside the `OID_GRAPH` payload, even when the number
coincidentally matches a native OID.

The explicitly numbered OIDs used by `oput` remain PoC identifiers, not an
allocation scheme.
Reference: `micropython/p9space.c`.

### 5.4 Local OID registration

`space.oregister([data])` allocates one fresh local OID and publishes
its initial payload (empty when no argument is given) through the native
`ospacecreate` call. It returns a Python integer only after the native
commit and GE sync succeed:

```python
import space
a = space.oregister()
b = space.oregister(b"hello\n")
assert a != b
assert space.oget(a) == b""
assert space.oget(b) == b"hello\n"
```

The data argument follows `oput`: bytes-like data is accepted, `str`
and other non-buffer objects are rejected, and the whole-object limit is
1 MiB. An empty payload is a real retrievable occurrence, distinct from
a missing OID (which still raises `OSError`). `oput` revises an existing
OID and does not allocate.

**Scope.** Registration is local to the active native O volume and its
`init` Space. The identifier is a 64-bit number meaningful only in that
native Space; it is not a UUID, content digest, host name, IP address or
globally unique identifier. Another volume or machine may issue the
same number for an unrelated occurrence. Remote observation, copying
and import are not implemented; deliberately importing bytes elsewhere
would be a separate registration there, not migration of this OID.
Machine-wide allocation across all volumes, disks and Spaces is also
**not** implemented.

**Allocator state encoding.** The state is appended to the existing
Space header `user[...]` part (the `N|K|W|user` value in section 3), so
it is neither a Kobj object nor an addressable OID:

```text
legacy seed:   env_sha256[64] | next[8]                  (72 bytes)
allocator:     env_sha256[64] | next[8] | "OALLOC01" | next[8]
                                                         (88 bytes)
value:         N[8] | K[8] | W[8] | 88-byte user part
```

The second `next` is the next candidate high-water mark, in the engine's
packed integer encoding. It is independent of the Python graph's
`sp.next` and of the 72-byte seed metadata that precedes it.

- A header with no user part, or the pre-allocator 72-byte seed, starts
  at candidate **64** on first use; a successful create rewrites the
  header to the 88-byte form and preserves the first 72 bytes.
- An 88-byte part with the `OALLOC01` marker is read normally.
- A malformed length/marker or a `next` outside the allowed range is
  rejected with `O: corrupt allocator state` or `O: unrecognized Space
  allocator state`; it is never silently reset to zero.

**Candidate range.** Allocator candidates are `64 <= oid <
0xffffffff00000000` (`OID_CODE`), excluding the native-exec OIDs
`0x4f45584543000001`-`03`. `OID_ROOTS`/`OID_FIRST` are below 64;
`OID_ENV`, `OID_GRAPH`, upgrade metadata and code OIDs are at or above
`OID_CODE`. Candidate selection also scans the published Space for any
record naming the candidate, including a tombstone, and checks pending
writes in the same transaction. Thus an explicitly written `oput` in
the future candidate range is skipped and the high-water mark advances
past it. The high-water mark never decreases, so a committed
allocator-issued OID is not reused merely because its content was
retired. Reaching the end of the range fails with `O: OID range
exhausted`; it does not wrap.

**Publication and concurrency.** `ospacecreate` begins one Space
transaction, reads the published high-water mark, selects and checks a
candidate, writes the initial payload and the new header user part, then
commits and syncs. The occurrence and allocator advancement become
visible together in the commit. A native lock (`oalloclk`) serializes a
whole create, including its sync, and also `ospacecommit`; concurrent
registration therefore queues rather than issuing duplicate IDs. A
failed operation before publication aborts its transaction; the next
begin sweeps unpublished versions (`W > N`). Because no OID was returned,
an uncommitted candidate may be retried. Once a create has returned, its
OID and high-water mark are durable for the tested volume.

Reads (`oget`, size queries) never invoke registration and do not change
the allocator state. The future remote rule is the same conceptually: an
observation of content on another machine need not create a local OID or
a durable local copy.

References: `ge/storage/obj.c` (`ospacecreate`), `spacecreate` in
`ge/storage/sysgefs.c`, `micropython/p9space.c`.

## 6. Reserved OIDs and MicroPython payloads

The current seeded `init` Space reserves:

```text
OID_ENV     = 0xfffffffffffffffc   Environment manifest
OID_GRAPH   = 0xfffffffffffffffd   serialized Python graph stream
OID_CODE    = 0xffffffff00000000   base of host-assigned code OIDs
OID_MP_EXEC = 0x4f45584543000003   experimental MicroPython executable
```

The local allocator state is not one of these OIDs: it lives in the
Space header user part (section 5.4). Allocator-issued identifiers begin
at 64 and are excluded from the code/Environment/graph region and the
native-exec fixtures.

The Environment manifest is an object containing text:

```text
runtime <mp-version>-mpy<MPY_VERSION>.<MPY_SUB_VERSION>
<import_name> <definition_sha256> <mpy_sha256> <code_oid_16_hex>
```

The source definition's SHA-256 determines its definition identity.
Each code OID refers to a compiled `.mpy` payload verified by its
SHA-256; the loader also checks the MicroPython MPY ABI. On the
native O import path there is no file search path, source fallback
or `sys.path` lookup. Host-side preparation may still use files.

**The Python graph is not a GE/O-native edge index.** At commit,
MicroPython turns the V7 graph's object stream into one byte stream
stored at `OID_GRAPH`. It contains a header and per-Python-object
records, conceptually:

```text
'H' | graph_N[8] | graph_K[8] | user_length[4] | user[72]
'o' | python_oid[8] | graph_version[8] | byte_length[8] | bytes
... repeated 'o' records
```

The 72-byte user header includes the Environment hash and the
next-Python-OID value. Python objects in this *internal stream* have
logical OIDs and can preserve identity and aliases after reboot, but
**they do not each have a separate native GE/O `Kobj` record**.
The graph stream's version and the enclosing O Space's version are
separate counters. On commit the graph is rewritten as a whole.
The Environment and referenced code objects are put again in the
same O transaction to keep them available under history sweeping.

An ordinary top-level function defined directly in the MicroPython REPL
stores its executable `.mpy` bytes in a separate native O object allocated
by `ospacecreate`. Its graph record holds that native OID and its defaults;
the Python function's logical graph OID remains a different identity. The
function is reconstructed with `__main__` globals, so aliases continue to
refer to the same callable after restore. Existing Environment functions
continue to refer to their module's code tree.

REPL code objects use the local native OID allocator below `OID_CODE`.
`space.oput` refuses to overwrite an OID currently referenced as REPL
function code. The code OID remains allocated and is not reused if its
function is later removed from the graph; code reclamation is not
implemented. Code is published and synced before the graph commit. If code
creation fails, no graph reference is written. If the later graph commit
fails, its previous published graph remains usable and the newly published
code is an unreferenced O object, not a dangling graph reference. Restore
rejects a missing, invalid or incompatible code object.

Only ordinary top-level REPL functions with no nested code are supported.
Lambdas, closures, generators, nested definitions and class or bound methods
are rejected at commit; native-code functions are not accepted as bytecode
functions.

References: `micropython/p9space.c` and
`micropython/p9image.c`.

## 7. Preparation, seeding and initialization

`tools/mkovol OUT [MIB] [SOURCE]` formats a **fresh disposable
image** with the real `ge/format/ream.c::reamo()` code built as a Linux
GCC host program. It writes only a caller-selected regular image, never a
block device. The default image size is 512 MiB; the minimum is 129 MiB
because GE ream requires more than 128 MiB. This operation **formats its
output**: never point it at a disk containing data.

If `SOURCE` is supplied, `tools/host/oseed.py` hashes the source bytes,
compiles the module to `.mpy` with the same-version Linux `mpy-cross`
(bytecode emitter, source basename embedded for tracebacks, and the
runtime's small-int width), writes `seed/env`, `seed/envhex`,
`seed/mod/<source_sha256>.mpy`, and, by default, `seed/mp`/`seed/mpoid`
for the GCC MicroPython executable byte for byte at `OID_MP_EXEC`. The
reamer consumes that seed directory directly; there is no VM, `gefs -i`
process or tar-as-virtual-disk step. The PoC seeds a small, fixed
catalog/Environment. This is not general package management.

The normal amd64 O-only boot configuration `pc64` embeds no MicroPython.
Before E12.2 its generated boot directory contained `oinit`, the `kbdfs`
console helper and the separate `ohandle`/`oexec` regression harnesses. E12.2
removed the `root` Dev and `bootdir` section, and N9.8 removed the test-only
`ohandle`/`oexec` sources, so the canonical kernel now embeds no `/boot`
payloads:

```text
UEFI -> kernel (9pc64) -> oboot=server kernel bootstrap
             -> ovolopen + ospaceopen("init")
             -> oexecute(space, OID_MP_EXEC, argv, argc)
             -> argv: micropython -O PARTUUID=... -S init -N
             -> Environment, code and init Space from O
             -> commit; persistent run()
```

The canonical `oboot=server` path uses the native kernel bootstrap and does not
execute `oinit`, does not mount a root and does not resolve absolute paths.
The historical `oinit` `init=`/REPL/diagnostic paths, the `root=` GEFS mount
and the embedded `kbdfs` helper are **removed from the canonical kernel**; they
are not silently available. The MicroPython executable bytes, Environment/code
and persistent state come from O. There is no `/boot/mp` and no
pathname-based MicroPython `exec` on this path; the removed embedded files were
**not** an initramfs, the O volume or a general native executable loader.

The historical `oinit -repl` path passed `-O PARTUUID=... -B` (no `-S`),
so `boot.py` set up the console and the O-loaded interpreter reached its REPL.
That path is no longer reachable from the canonical kernel because the process
root, absolute lookup and embedded `/boot/oinit` were removed. The former
disposable ESP/O REPL disk generator and its launcher have been removed and are
not a working REPL on current `main`.

On the **first** O boot, missing `OID_GRAPH` causes import of the
seeded demo module `odemo`. Its top level and `init()` initialize
state and persistent `run`. On later boots the Space and `run`
are restored without rerunning module initialization. On normal
return, the interpreter commits and syncs. An unhandled exception
skips the final commit; the previous published version remains.
A historical three-boot test recorded `1 -> 2 -> 3` on the same disk; it is not re-run by current cleanup gates.

References: `micropython/main.c`,
`micropython/p9space.c`, `tools/mkovol`.

## 8. Compatibility and limits

- The superblock O-profile version is **1**. There is no declared
  stable compatibility contract or migration tool for future formats.
- A flat seeded `init` Space is not a general persistent namespace
  or relation model. There is no native tag/edge index yet.
- The current Python payload format is separate from GE/O's native
  object-record layout and must not be generalized into the native
  graph specification.
- The PoC relies on a fixed module seed, one native O volume, a small
  per-process Space-handle table and whole-value object reads.
- OID allocation is local to the active native O volume and Space. It is
  not a machine-wide or cross-machine allocator, and no remote protocol,
  cache-to-O write or OID migration exists.
- The normal boot uses a fixed `OID_MP_EXEC`; there is no program catalog
  or general executable selection. An older O image without that object is not
  migrated automatically: startup reports the missing OID and exits
  without an embedded-interpreter fallback.
- Object deletion/tombstones and engine-level version retention do
  not constitute a complete object lifecycle, GC or capability model.
- Missing/corrupt-volume diagnostics, arbitrary interruption recovery,
  concurrency and production durability have not been established.

**Evolution rule:** document a future format or native relation
encoding as a *proposal* until implemented and demonstrated; do not
silently repurpose this version-1 description.

## N13 live access grants

[N13-OBJECT-CONTEXT](N13-OBJECT-CONTEXT.md) describes the kernel's in-memory
process object views and per-operation rights checks. These grants are live
kernel state, not new GE/O records, persistent object classes, or a storage
format revision. Existing native Space/OID identity and O v1 bytes remain
unchanged. The optional persisted view object is not implemented.
