# Durable project namespaces

A project, directory, package and library are application interpretations of
one ObjectSpace. A durable space has its own `(VolumeUUID, OID)`; its labels
refer to other durable spaces or GE/O file objects. Names are independent of
object-owned descriptive metadata. There is no stored single parent or kernel
pathname. An application receives a project Handle, not an ambient volume.

## Representation and publication

`ObjOpenProject` takes an authorized volume Handle and an existing namespace
OID. OID zero creates a root from an empty framed namespace record. This is a
trusted bootstrap/installer operation. Ordinary callers receive delegated
ObjectSpace Handles. The backing Ohand is private to the provider. File Handles
remain scoped to one OID. Live operations read the authoritative GE/O records;
processes do not own independent directory snapshots.

The raw namespace record is big endian: `P2PROJ01`, epoch, BindingID high-water
mark and entry count (four 64-bit words). Entries contain BindingID and target
OID (64-bit), rights, relation, native kind and name length (32-bit), then name
bytes. A 64-bit FNV-1a checksum terminates the raw data. The normal raw-first,
opaque-metadata-last footer follows. All persistent targets currently belong
to the same volume. Names have at most 255 bytes, excluding NUL. Unix slash
parsing is exclusively userspace policy.

`ObjOpProject` uses the fixed-width request in `runtime/abi/project.h`:
read snapshot, create, link, unlink, rename, replace file payload, resolve,
attenuate, create detached object, query persistent binding references, and
read footer-selected opaque metadata with metadata authority alone.
The conventional ObjectSpace bind/unbind/resolve/enumerate operations also
operate on durable ObjectSpace Handles. Enumeration checks an epoch; duplicate
labels remain distinct edges identified by BindingID.

A provider transaction stages changed objects and publishes them in one GE/O
commit. File updates retain their OID. Cross-directory rename publishes both
directories atomically. A version/epoch precondition rejects concurrent stale
writes. Every payload and record is validated before publication. New directory
payloads must be empty: serialized OIDs cannot mint bindings to guessed objects.
Detached creation permits constructing an unpublished namespace before binding
it into a visible project. Failed construction may leave orphan objects, which
are never automatically reused.

The provider maintains a framed per-volume binding-reference index in the same
transaction as binding updates. Counts include durable names, not open Handles.
A zero count does not invalidate an opened capability. Root selection and open
capabilities are independent of names; there is no `2 + child directories`
fiction for directory link counts in a multigraph. Cycles require graph-aware
reclamation; automatic garbage collection is not provided.

## Authority

Every request admits a live typed Handle. Resolving a child intersects the
saved edge ceiling with the current project grant; a read-only root cannot
produce writable descendants. Each derived Handle retains the capability
lineage of both the traversed project and the capability used to create the
binding. Revoking either lineage invalidates existing descendants and future
resolves. Removing a binding does not revoke its open descendants. A trusted
boot issuer reconstructs persisted binding ceilings on a new boot; live
capability revocations are not serialized. The provider also retains GE/O origin revocation and owner-record
integrity checking. Owner equality, Unix uid/gid, labels and raw OIDs never
authorize an operation. Mutation requires directory control or file write;
creating objects additionally requires create authority.

`obootgraph=<OID>` now selects an existing durable project root directly; the
obsolete whole-graph node-index snapshot format is rejected. Bootstrap installs
an attenuated read/meta/delegate/execute project Handle. Processes without a
root selector receive roots only by explicit delegation. No volume Handle is
retained in the bootstrap process's Handle table by project-root restoration.

## Userspace composition and Python

Union and intersection are userspace interpretations of ordinary ObjectSpace
objects whose opaque metadata contains `project_view` and `write_source`.
Source bindings are real attenuated namespace references. A union merges equal
`(label, target identity)` pairs; distinct targets with one label are ambiguous.
An intersection retains only identical pairs present in every source. Lookup
never ORs rights. Composition is read-only by default; an explicit write source
routes mutations to that source, without copy-up, hidden whiteouts or fallback.
Cycles in composition are reported rather than recursively expanded forever.

MicroPython's `namespace` module wraps a delegated Handle, reads raw data and
metadata, binds namespace references, and explicitly activates a project import
context. Source and MPY modules, packages and relative imports use that context;
no global volume catalog is consulted. Imported modules are cached per project
context until explicit reload. Automatic loading checks execute authority.
As with any interpreter, readable source bytes can separately be supplied to
ordinary `exec`; the import policy is not a Python-language sandbox.

Anonymous volatile ObjectSpaces remain useful for process-local contexts.
Their entries grow dynamically; enumeration is paged. They are not serialized
by the retired whole-graph snapshot API. Persistent spaces likewise have no
64-binding or 16-node schema limit; allocation and the existing GE/O transaction
budget bound resource usage. Full cross-volume persistence, garbage collection,
and physical-hardware validation remain separate work.

## Python naming and path boundary

`namespace.resolve(label)` follows exactly one local ObjectSpace binding. It
rejects slash-separated paths. Native namespaces have no `open(path)` method.
A process uses `namespace.from_handle(handle)` for an explicitly delegated
root, or `namespace.current()` when exactly one root is available (or an import
context has been activated). `from_handle` takes ownership of that Handle.

Unix path interpretation is explicit and lives in `posix`:

```python
import namespace
import posix

root = namespace.current()
tmp = root.resolve('tmp')
file = tmp.resolve('koe.txt')
print(file.metadata())
file.close()
tmp.close()

with posix.open_filesystem(root) as fs:
    with fs.open('/tmp/koe.txt') as file:
        print(file.metadata())
```

The filesystem view owns an attenuated copy of its root Handle. An opened
object remains valid after the view closes. Absolute and relative paths start
at this view's root; `..` follows the traversed route and clamps at that root.
Intermediate non-directory objects are rejected, even before `..`.

`namespace.open_boot_namespace(oid, rights)` is an explicitly trusted bootstrap
or installer helper, not the ordinary application entry point. The OID names a
stored namespace on the boot volume; it is not a pathname, mount point or source
of authority. Ordinary programs receive scoped namespace Handles instead.
