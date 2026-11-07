# NS4: kernel-authorized GE relation discovery

OQUERY=106 is the security boundary for ordinary application processes. It
uses the existing environment-independent `runtime/ge_relations` library,
compiled through `ge/storage/relations-kernel.c`. GE remains the only storage
engine and R1's `GR_META_OID = 0xfffffffffffffffb` wire format is unchanged.
The library's bounded encoding buffer now belongs to GrDB, allocated on the
kernel heap, rather than consuming 32 KiB of the 16 KiB kernel stack.

## ABI

Five arguments are explicit: operation, optional NsIdentity pointer, UTF-8
input pointer, input length, NsOutput pointer. Operations are FIND=1, GET=2,
TAGS=3, ADD=4, REMOVE=5. The NS_BOOT=256 flag selects an explicit trusted-bootstrap variant; its
selector is `NsAuthority {u64 local_handle, u64 oid}`. It additionally supports
CREATE=6. This variant checks the original bootstrap process AND a current
process-local bootstrap Space handle before reading the selector. Ordinary
processes cannot use it, including with their own delegated handles.
Unknown operations and flags fail. NsIdentity is 32
bytes: canonical volume UUID[16], u64 SpaceID, u64 OID. NsOutput is three u64
fields: user buffer address, byte capacity, returned byte count. NULL/zero
buffer requests the size; callers must handle changes between two calls.

FIND returns packed NsIdentity records, never bare OIDs or kernel pointers.
GET returns content. TAGS returns repeated little-endian u32 byte lengths and
UTF-8 tag bytes. ADD and REMOVE require one currently effective grant carrying
both OREAD and the new explicit ORELATE=64 right. OWRITE alone grants no
relation mutation authority. ORELATE permits creating tag identifiers as part
of an authorized relation mutation; it does not permit arbitrary data-object
creation. Restricted creation remains denied, including direct OSPACECREATE.

## Query and authorization

1. Pin the caller's effective binding grants and map epoch.
2. Read R1 metadata internally only for Spaces containing readable candidates.
3. Evaluate tag posting-set intersections using the unchanged R1 algorithm.
4. Intersect matches with candidate identities using volume AND SpaceID AND OID.
5. Deduplicate aliases by complete identity.
6. Acquire retained grant gates in address order, check revocation/rights and
   current effective membership, and verify an unchanged namespace epoch.
7. Return only the resulting identities and their filtered byte count.

No raw metadata object or bootstrap Space handle is lent to the caller. An
empty namespace returns no matches; unknown tags cannot expose inaccessible
objects or pre-filter match counts. Gate ordering avoids lock inversion;
map spinlocks never enclose GE I/O, user-memory copying or gate acquisition.
Concurrent mutation can fail with `O: namespace changed; retry`. Successful
queries describe a visibility linearization point; they do not freeze the
namespace forever. Subsequent GET, OLOOKUP and native OSPACEGET revalidate
rights and membership. Revocation closes the grant's native storage handle
and affects every copied reference.

GET and TAGS require a current readable grant for the specific identity. GET
uses that grant; TAGS exposes only that object's relations. Relation mutation
is serialized with kernel queries by a QLock and uses native GE creation,
put and commit. Bootstrap's GE operations now enter the same kernel transaction lock through
the explicit privileged variant. Thus authorized child and bootstrap relation
updates cannot overwrite each other's metadata through separate read/modify/
commit sequences. Native GE transactions and commit remain unchanged. Direct
raw metadata editing by the trusted bootstrap is outside the R1 operation
protocol; that existing broad authority is not available to ordinary children.

## MicroPython

For ordinary children, `ge.find/get/tags/add/remove` call OQUERY. Filtering is
inside the kernel, independently of Python. `ge.find` retains integer OID
results when all matches belong to one Space; mixed-Space results explicitly
raise ValueError. `ge.find_ids` always returns `(UUID_bytes, SpaceID, OID)`
tuples. GET/TAGS/ADD/REMOVE accept these tuples. Bare OIDs are accepted only
when the readable namespace contains one unambiguous complete identity.

The original bootstrap process keeps the original active-Space R1 operations,
including create and its broad database authority. Children cannot pass its
PID check or use its process-local handle. It no longer caches relation metadata in Python; every operation is evaluated
inside the kernel. The active writer helper commits any pending Python graph
transaction before entering the GE operation.
`ge.find_ids` always uses the namespace query, including in bootstrap.

RFORK inherits program memory according to the existing process semantics;
capability checks cannot erase data already present in inherited memory. Use
OSPAWN for a fresh untrusted executable. NS-COMPLETE includes fresh native ELF
clients whose only authority is the selected delegated namespace and Pipe.
These clients directly test OQUERY and OSPACEGET denial, including metadata.

## Demonstrations

The seeded demo allocates actual persistent data objects 64–69 in Space 1:
Python sources 64,65; C source 66; images 67,68,69. An editor receiving only
64/65 finds `#python #source` and cannot read 66, even through native calls.
A broader explicitly delegated view can read all three sources. The image
viewer's four-tag query returns 67. SHARE children retain that result; a COPY
child removing its 67 binding returns nothing, while its parent retains 67.
An EMPTY child finds nothing. A selected COPY sender receives only image 67.

A second existing flat catalog Space (SpaceID 2) also allocates OID 64, tagged
`#c #source`, with different bytes. Qualified queries and reads distinguish it
from Space 1's 64. This is an identity-collision regression, not directory
traversal: the legacy catalog label `/` is a single existing Space name.

Both independent VM boots reconstruct grants and namespace views. The GE data
and metadata objects are not recreated between boots. The host checker verifies
identical OIDs, bytes and metadata hashes at rest, independently of guest prints.

GE retains its existing one-pending-writer-per-Space rule. A concurrent Python
graph transaction or another authorized native writer can cause a busy error;
callers must retry rather than assume success. The concurrent mutation test
uses bounded retries and asserts every addition before cleanup. Kernel R1
operations share one lock, but this does not replace GE's transaction policy
or serialize arbitrary trusted raw writes across separate syscalls.
