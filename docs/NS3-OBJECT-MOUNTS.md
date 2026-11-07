# NS3: native object bindings and sealed mounts

OContext remains a bounded array of 32 entries. Each entry contains a flat
ASCII role (1–31 letters/digits/underscore/hyphen), a retained Ohand grant and
an attachment number. All direct and mounted entries share this limit.
Names contain no navigation or directory semantics.

Lookup selects the first entry for a role. A revoked or unreadable first entry
blocks that role: lookup never falls through to a less restricted lower layer.
Authorization checks effective entries, so an old handle to a hidden grant
cannot read it unless another effective alias references that same grant.

| amd64 syscall | Arguments | Result |
|---|---|---|
| OBIND 96, unchanged | handle, OID, role, length, rights | Replace role |
| ONSVIEW 101 | array of 32-byte roles, count | Process-local sealed provider handle |
| ONMOUNT 102 | provider handle, union mode | Opaque attachment number |
| ONUNMOUNT 103 | attachment number | Remove that attachment only |
| OBINDX 104 | OBIND arguments, last value = rights OR (mode << 8) | Compose binding |
| ONSVISIBLE 105 | pointer to NsOutput | Readable effective object records |

`runtime/abi/namespace.h` defines the fixed amd64 record layouts. Existing
syscalls retain their numbers and parameter layout; canonical dispatch and
MicroPython stubs are generated from the explicit ABI manifest.

Modes are REPL=0, BEFORE=1 and AFTER=2. BEFORE prepends the provider's ordered
entries, AFTER appends them, and REPL removes entries with conflicting role
names and prepends the provider. Unrelated roles remain. REPL is destructive:
unmount does not restore the removed entries. OBIND retains its original
replacement behavior; OUNBIND removes every layer of the selected role.
Creation flags, including MCREATE=4, are rejected. Union position grants no
storage creation authority.

Export copies selected effective bindings after checking delegation rights
and rejects a source-map epoch change during selection.
The resulting provider is sealed and lives in the caller's existing generated
O-handle table. It cannot be obtained by PID, pointer or numeric namespace ID,
and it cannot be used as an object-storage handle. Non-bootstrap rebinding
preserves the same grant and rights, hence the same revocation lineage.
Bootstrap can issue restricted grants using its existing authority.

Mounting retains references to the provider's grants and stores flattened
entries. Closing the provider or terminating its creator does not invalidate
an installed attachment. Removing an attachment drops its own references,
never deletes objects or revokes grants. A still-open, valid sealed provider
can be mounted again; a closed handle or revoked provider grant cannot.
Providers do not reference Pgrp, other providers or live contexts. Exporting a
composed view produces another flat snapshot, so self-derived and nested views
cannot create reference cycles or recursive lookup. This is snapshot mounting,
not a live mirror of subsequent edits in the provider's source process.

Shared Pgrp users observe changes; copied groups retain an independent ordered
array and attachment IDs. Capacity is checked before publishing any mutation.
No allocation or GE I/O occurs under the map spinlock. Grant releases happen
after unlocking. Copying pins all grant references under the map lock.

MicroPython examples:

```python
space.obind('photo', oid, space.OREAD | space.ODELEGATE)
provider = space.view(['photo'])
attachment = space.mount(provider, space.MBEFORE)
space.unmount(attachment)
space.hclose(provider)
```

`NsVisible` records contain a 16-byte canonical partition UUID, SpaceID, OID,
rights and zero reserved field. Aliases are deduplicated by full identity;
rights are descriptive, not transferable authority. Snapshot collection pins
grants, acquires gates in address order and checks the map epoch. Concurrent
mutation can produce a retry error rather than an inconsistent snapshot.
One active native volume remains the current kernel limitation.
