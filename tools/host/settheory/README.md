# Experimental MicroPython set algebra

This prototype tests **set membership instead of pathname traversal** on the
unchanged Plan2001 GE/O v2 object store. It lives only on
`experiment/set-theory-mpy`. No kernel ABI, ObjectSpace graph provider,
filesystem path semantics or GE/O disk format is changed.

## Model

Each persistent set is a GE/O object addressed by its `SetID` (the ordinary
GE/O OID). Its payload contains a short version marker followed by sorted,
unique 64-bit `ObjectID` members. The relation is logically
`(SetID, ObjectID)`. Objects are not copied when added to another set.
A SetID is identity, **not authority**: each operation requires a previously
authorized `VkindGeVolume` Handle and is limited by its rights.

For this first prototype `union`, `intersection` and `difference` return
transient tuples, not extra persistent GE/O objects. Use `materialize` when a
result should have a new durable SetID.

## REPL example

On a disposable O volume containing the opt-in extra Environment module:

```python
import plan9 as p
import settheory

v = p.obj_open_volume()                 # privileged/authorized REPL only
sets = settheory.Store(v)
h1, k1 = p.obj_create_ge(v, b'K1')
h2, k2 = p.obj_create_ge(v, b'K2')
h3, k3 = p.obj_create_ge(v, b'K3')
p.obj_commit(v)
room1 = sets.new((k1, k2))
room2 = sets.new((k2, k3))
sets.intersection(room1, room2)       # (k2,)
sets.union(room1, room2)              # tuple of k1, k2, k3
sets.difference(room1, room2)         # (k1,)
sets.add(room1, k3)
sets.remove(room1, k3)
sets.contains(room1, k2)              # True
newset = sets.materialize(sets.intersection(room1, room2))
sets.members(newset)                  # (k2,)
p.obj_close(h1)
p.obj_close(h2)
p.obj_close(h3)
p.obj_close(v)
```

Record the returned SetIDs if you need to reopen them after reboot; a
human-friendly persistent name catalog and query-language UI are **not** part of
this first slice. `Store` never silently creates or opens an authorization
root.

## Opt-in seeding

The native O runtime cannot import arbitrary `.py` files by path; its imports
come from its GE/O Environment manifest. The host seeder now optionally
accepts `--module NAME=PATH`, and `SETTHEORY=1` passes the source into the
existing `tools/host/mkovol` flow. Ordinary builds and images are unchanged.

For the standard disposable QEMU/OVMF REPL proof, run from the repository root:

```sh
python3 tools/host/settheory/test_settheory.py
bash tools/host/settheory/run-settheory.sh
```

The second command builds/boots the current kernel, seeds the module on a
**disposable** GE/O image, imports `settheory` in the guest REPL, and checks
`SETTHEORY-PASS`. It tries KVM with TCG fallback. It does **not** add a
GitHub Actions workflow or run on a physical device.

To explore manually in the QEMU window (requires display and tools):

```sh
SETTHEORY=1 bash tools/qemu-o-boot.sh --accel kvm:tcg --interactive \
  --obootexec 0x4f45584543000003
```

## Scope and limitations

- Up to 65,536 members per set for the initial whole-object implementation.
- All members are unsigned 64-bit OIDs; across-volume membership is deferred.
- No path, current directory, parent, recursion or graph traversal.
- Every mutation rewrites the small set payload and calls `obj_commit`.
- Concurrent set writers are not coordinated by this Python layer; the last
  published payload can supersede earlier membership edits.
- GE/O's object-level permission and ownership model remains authoritative.
  Possession of a membership record never grants a capability to the member.
- The caller is responsible for object lifecycle, retention and authorized
  deletion. Set membership is not object ownership.
- No full native guest or crash/fault test result should be asserted until
  the test runner has actually been executed and its output inspected.
