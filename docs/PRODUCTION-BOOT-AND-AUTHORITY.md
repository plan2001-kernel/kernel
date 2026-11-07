# Production server/embedded boot and authority model

**Status:** `TARGET/PROPOSED` for server and embedded deployments;
desktop is `OPEN`. This document is a design record and staged migration
plan, not an implementation. It does not claim that secure network boot,
verified boot, GE/O encryption, passkey administration, a general
capability system or a native administration service exists today.

**Baseline:** `main` at the E11.4B documentation baseline (`d015719`).
Source references below are evidence at that revision.
Terminology follows [KERNEL-API-ABI.md](KERNEL-API-ABI.md); the
implemented storage contract remains [O-FORMAT.md](O-FORMAT.md).

**Status vocabulary used here:**

- `IMPLEMENTED` — concrete code exists in this repository.
- `LEGACY` — inherited behavior retained for compatibility, not the target
  authorization model.
- `TESTED` — supported by a named executed test in `docs/STATUS.md`.
- `PROPOSED` / `TARGET` — intended direction; not implemented.
- `OPEN` — a required decision or design not yet settled.

## 1. Scope and non-goals

The production server/embedded profile is a machine that can boot
unattended, expose a narrow remote administration surface to multiple
authenticated humans, run services under resource-scoped authority, and
keep GE/O object storage confidential at rest as a future goal. It does not
require a universal interactive shell, a POSIX multiuser filesystem, a
root account, or one shared administrator identity.

This record deliberately leaves the following implementation choices
**OPEN**: signing algorithm and update metadata, firmware/EFI trust
anchors, TPM/attestation, GE/O encryption format and key release, network
boot protocol, remote management protocol, authenticator (e.g. passkey)
details, service manager API, audit format, quotas and delegation rules.

It does **not** define a mandatory `File` class hierarchy, universal
Unix owner/mode fields, a generic kernel `NativeDev`, a systemd-like
service manager, a general capability ABI, a Python security ontology, or
a new persistent GE/O format. Existing OID/Space identity and byte
storage remain the base.

## 2. Trust boundaries and actors

```text
 UEFI firmware (+ optional UEFI setup password, Secure Boot state OPEN)
        |
        +-- local/recovery: ESP -> Plan2001 UEFI loader -> kernel
        |                  (optional GRUB chainload of that loader)
        +-- future optional: authenticated network boot stage -> kernel
        |
        v
 Plan2001-O kernel  [IMPLEMENTED/LEGACY: pc64 family]
        |
        +-- process isolation, scheduling, legacy eve/iseve checks
        +-- native O syscalls -> Gefs/Ospace handles -> GE storage
        |                                      |
        |                                      +-- future GE/O
        |                                          encryption-at-rest
        |                                          (TARGET, OPEN)
        +-- services / scheduler (PROPOSED narrow authority)
        |
 One logical Administration environment/service (PROPOSED)
        |
        +-- authenticated operator A  (identity for audit/revocation)
        +-- authenticated operator B
        +-- ... (individual human identities at the management layer)
```

Independent resources guarded by the kernel or management service include:
GE/O Spaces and OIDs, service lifecycle, schedules, update requests,
reboot/power control, logs/audit records, and network-facing service
endpoints. An operator authenticated to the Administration environment is
not thereby a kernel superuser.

### Actors and resources

| Actor / resource | Current status | Boundary |
| --- | --- | --- |
| UEFI firmware / ESP | `IMPLEMENTED` boot path; firmware trust `OPEN` | Loads a loader/kernel from local media or a future network stage |
| Plan2001 UEFI loader (`boot/efi`) | `IMPLEMENTED`; local boot `TESTED` in QEMU | Reads its `plan9.ini` and kernel; no verified-boot claim |
| GRUB chainload (`tools/gcc-o-install.sh`) | `IMPLEMENTED` integration script; `NOT TESTED` in recent cleanup stages | Alternative local/recovery entry to the same UEFI loader |
| Kernel | `IMPLEMENTED` | Enforces current process, syscall, memory and legacy identity checks |
| GE/O storage | `IMPLEMENTED` plaintext format | `(SpaceID, OID)` identity; encryption at rest `TARGET/OPEN` |
| O Space handle (`Ohand`/`Ohtab`) | `IMPLEMENTED` | Process-local integer handle; not a persistent capability |
| Service/scheduler | `PROPOSED` | Resource-scoped operations, not ambient kernel privilege |
| Administration environment | `PROPOSED` | One logical service; multiple authenticated humans; audit/revocation |
| Independent services | `PROPOSED` | Receive narrow validated rights; no root shell by default |
| Update distribution endpoint | `PROPOSED/OPEN` | Must authenticate bytes independently of network location |

## 3. Lifecycle A — normal unattended startup (and future remote kernel supply)

1. Power-on or remote reboot. Firmware runs without demanding an
   interactive boot password. A UEFI **setup** password may protect
   firmware settings but is not a per-boot human prompt (`OPEN` policy).
2. Firmware selects a local EFI boot entry or a controlled network boot
   stage. Today only the local UEFI path is `IMPLEMENTED/TESTED`; signed
   network boot is `PROPOSED/OPEN`.
3. If a remote kernel workflow is later added, the stage obtains a
   **machine-appropriate kernel image** from a controlled distribution
   endpoint. Source-address restrictions or an unguessable URL are
   **not** authentication of bytes. Image signature/verified boot and
   replay/downgrade policy are separate `OPEN` designs.
4. The loader/kernel selects the O volume (`PARTUUID` in the current
   path), opens the administration Space or service definition, and
   starts the configured services. Existing O boot paths remain
   unchanged by this document.
5. The Administration environment authenticates each human operator at
   the management layer. Services start with their own scoped authority,
   not with the operator’s ambient rights.
6. Runtime authorization is checked by the approved kernel or management
   service for each resource operation. An OID, integer handle, UID-like
   string or boot signature is not by itself a permission.

## 4. Lifecycle B — signed update, recovery and offline startup

```text
 candidate kernel/update
        |
        v
 authenticate image + metadata (signature, freshness, target machine)
        |
        v
 stage previous-good kernel / recovery entry (OPEN mechanism)
        |
        v
 atomic activation + reboot
        |
        +-- success: commit new default, retain previous-good
        +-- failure/timeout: boot previous-good or local recovery
```

Required properties, all `PROPOSED/OPEN` unless noted:

- Prefer an authenticated **previous-good** kernel stored locally so a
  machine can still start when the network or distribution endpoint is
  unavailable.
- Preserve at least one local/recovery boot path (UEFI ESP, local USB,
  and/or GRUB, as available). A future netboot is **additive**, never a
  forced replacement.
- Define downgrade/replay policy explicitly; a valid old signature is not
  automatically acceptable forever.
- Keep update activation separable from GE/O data-key release. Booting an
  approved kernel does not by itself prove that the disk key should be
  released to it.
- A remote reboot may be unattended, but recovery must not depend on a
  human being physically present when the design claims unattended
  operation.

## 5. Boot-secret matrix

This matrix names **roles**, not actual keys. No secret, token, password or
private key belongs in this repository, examples, logs or URLs.

| Secret / credential | Holder | Used for | Exposure boundary | Recovery / rotation |
| --- | --- | --- | --- | --- |
| Kernel/update signing **private** key | Controlled build/distribution authority | Sign kernel/update metadata | Must never ship to machines or repo; compromise requires revocation and reissue (`OPEN`) | Offline/HSM-backed custody, rotation policy `OPEN` |
| Kernel/update **public** verification key | Firmware/loader/kernel trust store | Verify update bytes/metadata | Public, but its update/rotation path must itself be authenticated (`OPEN`) | Signed trust-store update + recovery path `OPEN` |
| Preboot network credential (if any) | Firmware/network boot stage | Reach a restricted distribution endpoint | Not a substitute for image signature; long-lived shared secrets are discouraged (`OPEN`) | Per-machine or short-lived credentials `OPEN` |
| Machine identity key/certificate | Machine/firmware or protected local store | Identify this machine to update/management service | Not a human operator identity; cloning/theft policy `OPEN` | Re-provision/replace-board flow `OPEN` |
| Kernel runtime credential | Approved kernel / protected storage | Access management or key-release service | Separate from build signing key; least privilege (`OPEN`) | Rotation/revocation `OPEN` |
| GE/O data-encryption key | Protected key store or sealed key release `OPEN` | Decrypt GE/O volume at rest | Must not be embedded in a widely distributed kernel image if avoidable; see §6 | Creation, rotation, loss and replacement-board policy `OPEN` |
| UEFI **setup** password | Local firmware administrator | Protect firmware settings/boot order | Not a per-boot user prompt; loss means firmware recovery procedure (`OPEN`) | Firmware-vendor-specific recovery `OPEN` |
| Human operator authenticator (e.g. passkey) | Each operator + Administration service | Authenticate a person to the management layer | Individual identity for audit/revocation; not a kernel UID | Enrolment/revocation/recovery `OPEN` |

## 6. Kernel-embedded secrets proposal: exposure limits

A deployment could build machine-specific secrets into a regularly updated
kernel. It is recorded here only as a **deployment proposal with explicit
limits**:

- Anyone who obtains the kernel image may be able to extract embedded
  symmetric keys; a signature authenticates bytes but does not keep them
  confidential.
- A signed kernel does not encrypt its own contents, and an alternative
  kernel that can read raw ciphertext can use any key it can obtain.
- Distributing one image with one embedded key weakens per-machine
  isolation; per-machine images increase build, distribution and rollback
  complexity.
- If the threat model includes disk theft or offline image analysis,
  a protected/TPM-sealed GE/O data key and separate build-time signing
  keys are a more defensible direction (`OPEN`, not implemented or
  mandated here).

Secret lifecycle requirements to settle before production: creation and
entropy source, per-machine vs shared scope, rotation, upgrade, recovery,
loss, replacement board, revocation, backup, audit, and rollback. No
choice is silently mandated by this design record.

## 7. Operation authorization model

Keep four axes distinct:

| Axis | Question | Current evidence | Target |
| --- | --- | --- | --- |
| Identity | Who is acting? | Legacy `eve`, `up->user`, `procsetuser`, `hostownerwrite` | Multiple authenticated humans at the management layer; per-service identities `PROPOSED` |
| Ownership / provenance | Who created or historically controls a record? | Legacy `owner` fields in `#s`, `#σ`, `#S`, `#p` etc. | Historical provenance, **not** authority by itself |
| Boot trust | Which code/configuration is approved? | Local UEFI loader; no verified boot | Signed/attested boot policy `OPEN` |
| Resource-scoped right | May this actor perform this operation on this resource now? | Partial: legacy permission checks; `iomask`; no general capability system | Explicit validated rights per operation/resource `PROPOSED` |

Consequences:

- Knowing an OID or holding an integer must **never** imply access.
  `(SpaceID, OID)` is identity, not permission. `Ohand` is a
  process-local handle, not a capability record with rights.
- A service definition, schedule, update request or reboot request needs
  an explicit operation authority. A generic “admin can do anything”
  kernel identity is not the target.
- Administrative interface authentication is not the same as kernel
  authorization. The management service must translate an authenticated
  human into a narrow, auditable operation request.
- The first deployment may give all authenticated operators the same
  management privileges, but it must retain individual identity for
  audit, revocation and future delegation.

## 8. Evidence matrix for current behavior

| Area | Source evidence | Status | Honest limit |
| --- | --- | --- | --- |
| `eve` / `iseve` | `kernel/core/auth.c` (`eve`, `iseve`), `portfns.h` | `IMPLEMENTED/LEGACY` | Global host-owner comparison; not a capability system |
| `hostownerwrite` | `kernel/core/devcons.c`, `Qhostowner` write | `IMPLEMENTED/LEGACY` | `#c`-only; unauthorized/size/rename runtime tests `NOT RUN` |
| `timecontrolallowed` | `kernel/core/sysinfo.c`; callers in `devcons.c` | `IMPLEMENTED` | Privileged clock policy; no general rights model |
| `devproc` | `kernel/core/procdev/devproc.c` (`nonone`, owner/`iseve` checks, trace restrictions) | `IMPLEMENTED/LEGACY` | Process observation/control, not service administration |
| `Ohand` / `Ohtab` | `ge/storage/devgefs.c`; `portdat.h` | `IMPLEMENTED` | Eight process-local slots; no rights/transfer/revocation contract |
| `OVOLOPEN` / `OSPACEOPEN` | `ge/storage/devgefs.c`, `systab.h` | `IMPLEMENTED` | Validates arguments/handles; no user/permission check on Space contents |
| `OSPACEGET` / `OSPACEPUT` / `OSPACECOMMIT` | `ge/storage/devgefs.c`, `docs/O-FORMAT.md` | `IMPLEMENTED/TESTED` | OID plus handle access; no capability check |
| `OEXECUTE` / executable OID | `ge/storage/sysgefs.c` | `IMPLEMENTED/TESTED` | Fixed bounded executable-object path; not general program loading |
| `iomask` | `kernel/core/portdat.h`, `sysfile.c` (`sysiowrite`), `pc64/ouser.c` | `IMPLEMENTED` | Process-local output grant; not a general capability or disk/network right |
| USB | `kernel/amd64/pc64` (`usb`, `usb*`), USB drivers in the amd64 port (the legacy `sys/src/9/pc64` tree was removed in N10) | `IMPLEMENTED/LEGACY` | Existing boot/storage/diagnostic paths must be retained; recent cleanup gates do not test USB |
| UEFI loader | `boot/efi`, `tools/build-gcc.sh`, `tools/build-efi-gcc.sh`, `tools/gcc-o-install.sh` | `IMPLEMENTED` | One bounded QEMU O-server boot after N9.8; no Secure Boot or signed-netboot claim |
| GRUB chainload | `tools/gcc-o-install.sh`; `boot/efi/fs.c` search path | `IMPLEMENTED` integration script | `NOT TESTED` in recent cleanup stages; retained as local/recovery option |
| Legacy namespace/`Dev` authority | `devsrv.c`, `devshr.c`, `devsd.c`, `devfs.c`, etc. | `LEGACY` | Retained where still linked; O-only profile prunes only selected registrations |
| Networking | native IPv6 core (`kernel/net/`), canonical `pc64` without legacy network `Dev` registrations; see `docs/STATUS.md` | `IMPLEMENTED` (runtime verified earlier) | No authenticated/SEALED mode; physical NIC breadth and a boot without a NIC do not re-verify traffic |

## 9. Staged follow-up tasks (not implemented)

Each item is intentionally small enough to prove independently. None is
authorized merely by this document.

### (a) Privilege / OSPACE authorization

- **Smallest proof:** a test process with a valid Space handle fails a
  get/put/commit on a Space it has not been granted, while an authorized
  process succeeds; OID knowledge alone changes nothing.
- **Compatibility risk:** existing O syscalls assume handle-only access;
  a new rights model must not silently break the D2b server path or
  existing tests.

### (b) Administration service/API and human authentication

- **Smallest proof:** a disposable management service authenticates two
  distinct operators, records who requested a harmless named service
  action, and grants no kernel/raw-disk/arbitrary-memory operation.
- **Compatibility risk:** no existing service-manager API to preserve;
  avoid importing `#s`/9P namespace assumptions as the target contract.

### (c) Signed/verified boot and key handling

- **Smallest proof:** a test loader rejects a modified kernel/update and
  accepts an approved one, with an explicit freshness/downgrade case.
- **Compatibility risk:** must keep the current UEFI and GRUB/local boot
  paths and recovery usable; must not require Secure Boot to be enabled
  for development.

### (d) Encrypted GE/O device

- **Smallest proof:** an offline image is unreadable without the key,
  while an approved kernel with a test key can mount/open it and pass a
  persistence round trip.
- **Compatibility risk:** on-disk format and existing plaintext volumes;
  migration, key loss and replacement-board behavior must be explicit.

### (e) Remote deployment and recovery

- **Smallest proof:** a disposable machine boots an authenticated update,
  keeps a previous-good local kernel, and recovers with the network or
  distribution endpoint unavailable.
- **Compatibility risk:** preserve local USB/ESP/GRUB recovery; do not
  make network boot mandatory.

### (f) Desktop model

- **Status:** `OPEN`.
- **Smallest proof:** not defined yet.
- **Compatibility risk:** retain current local keyboard/display source
  and desktop options; do not declare server-only to be universal.

## 10. Compatibility invariants

- USB support and its current registrations/controllers remain in the
  current kernel profiles. Recent cleanup stages have not removed or tested USB.
- UEFI boot and GRUB chainload workflows remain usable. They are not
  replaced by the future authenticated-network-boot proposal.
- The current O boot path, O volume opener, executable OID,
  `plan9.ini` handling, disk layout and MicroPython persistence are
  unchanged by this document.
- Legacy `eve`/`hostowner`/`iseve` behavior remains in place until a
  separate tested authorization migration replaces it. No documentation
  change alters kernel privilege behavior.
- The canonical kernel includes the native IPv6/Ethernet path. This
  document adds no new network profile and makes no physical-hardware
  coverage claim.
