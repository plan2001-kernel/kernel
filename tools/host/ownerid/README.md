# OwnerID recovery/corruption tests

`run-ownerid-recovery.sh` uses disposable O images and QEMU.  It never runs
on a physical disk.

## Hard-kill checkpoints

The guest prints `OWNERID-CRASH <checkpoint>` and disables interrupts in a
spin loop.  The host watches the serial log and sends SIGKILL to QEMU; the
guest does not shut down, flush, or unwind the failing syscall.

| point | marker | guest state at the marker |
|---|---|---|
| 101 | `before-owner-staging` | payload chunks staged, no Kowner record yet |
| 102 | `after-owner-staging` | payload and Kowner staged in version W = N+1, header not committed |
| 103 | `after-publication` | `objtxcommit()` published the header, before `gesync()` |
| 104 | `migration-after-staging` | the first migration batch is staged at W = N+1, owner-mode header not committed |

The marker only proves where guest execution stopped.  It does not by itself
prove that anything reached the disk image.  Each case reboots the same image
and asserts the actual recovered state:

* create 101/102: the target OID is absent, a retry reuses it, and the retry
  object has one correct immutable owner record;
* create 103: if the target is present it has the intended bytes and owner;
  otherwise the retry reuses it.  The retry OID, owner, and owner-record count
  are checked in both outcomes;
* migration 104: before retry every legacy live object still reads through
  the seeded legacy owner and no owner-mode header is observable; after retry
  every live object has exactly one owner record, tombstones have none, and
  an empty legacy Space also upgrades;
* migration abort: owner fault 4 raises inside the first migration batch.
  The Space must remain in its legacy pre-migration state, and a retry must
  publish one owner record for every live object and none for tombstones;
* corruption: a live object with a missing, malformed (bad magic or length),
  or zero-valued `Kowner` record fails closed for open and write, while the
  authentic unmigrated seeded Space still resolves to its legacy owner.

## Durability limits

QEMU uses the default raw-drive cache policy (`writeback`).  SIGKILL stops
the guest, but writes QEMU already issued may survive in the host page cache
and reach the image.  These tests therefore exercise guest crash recovery and
the engine's publication rules; they are not a physical power-loss or
host-cache-drop proof.  A stronger durability claim would need a host cache
policy or power-cut harness, and is not made here.

## Running

```sh
tools/host/ownerid/run-ownerid-recovery.sh            # build if needed
tools/host/ownerid/run-ownerid-recovery.sh --no-build
tools/host/ownerid/run-ownerid.sh
tools/host/ownerid/run-ownerid-reboot.sh
```
