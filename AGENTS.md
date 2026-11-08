# Plan2001-O — instructions for coding agents

## Current source and task workflow (2026-11-07)

The owner's later GitHub-first decision supersedes the kuha-only development
and mandatory MCP source-publication rules preserved below. Read the
[current workflow](docs/DEVELOPMENT-WORKFLOW.md#nykyinen-työnkulku-2026-11-07)
before acting. Use Plan2001 MCP to claim tasks and report results; use
[GitHub main](https://github.com/plan2001-kernel/kernel/tree/main) as current
source. Verify the actual executor, available authentication and exact main
SHA for each task. Propose changes on a dedicated branch and PR, verify the
remote commit/files after writing, and report commit/PR links and actual test
outcomes through MCP. Do not merge or update main without explicit assignment.

The MCP source snapshot is historical T11 until separately synchronized; never
substitute it for GitHub main or overwrite T12 with it. GitHub branch/PR writes
and task_report do not update that snapshot. The dated publisher/hooks below
are historical mechanisms, not the default route for current tasks.

## Network target decision (2026-11-08, latest T15 assignment)

The owner's later minimal T15 assignment supersedes the earlier expansive
ESP-SA-BUNDLE/key-lifecycle decision. [Current T15 scope](docs/ESP-SA-BUNDLE.md)
is an **interface integration test**, not a VPN/security product: reuse T14
userspace IPv6/ICMPv6/ND and T13 raw Device ABI/ObjectFIFO for bidirectional
transport-mode ESP echo (IPv6 NH=50), with two fixed distinct directional
TEST-ONLY AES-128-GCM SAs in a disposable fixture. Reuse existing crypto
primitives in the user process. Correct packet nonces/tags are required within
the running test; intended protected echo must not silently use plaintext.

Do not restore persistent key objects, versioned bundles, durable consumed
generations, anti-rollback, replay windows, key lifecycle/rekeying, expiry or
clock guarantees, secure distribution/storage or enterprise VPN requirements.
The [old expansive design](docs/history/ESP-SA-BUNDLE-2026-11-08.md) is history,
not a checklist. Fixed demo keys/counters carry no production, restart or
key-reuse security claim. IKEv2 remains entirely excluded; no negotiation,
key exchange or extra daemon. No new syscall, protocol kernel provider/FIFO,
pathname/Chan/fd adapter or restored native network stack. UDP/TCP and T16
are independent; T16 is not assigned.

T13 and T14 have their recorded emulated gates; T15 now passes its minimal
[ESP integration gate](docs/T15-ESP-RESULT.md) (13 guest / 43 peer assertions).
Physical NICs were NOT RUN.
Native IP startup/OBJ TCP/UDP opens remain disabled. Preserve actual boot/MPY/
driver dependencies, with no unused-stack compatibility obligation. In reports
call the executor **agentti** and inspect the actual host/test environment.
GitHub-first branch/PR/readback/MCP reporting above remains in force.

## plan2001.com: current infrastructure (2026-11-03)

Owner-confirmed facts, authoritative over older documents and session summaries:

- **plan2001.com runs Ubuntu Linux.** The host is not running Plan 9,
  9front, Plan9+, Plan2001 or Plan2001-O.
- The former plan2001.com service implemented in **plan9-wasm32** and
  **monolith** has been removed. Those repositories are not the current
  production service or deployment source for this host.
- Their old rc-httpd/tlssrv, rcpu/9pterm, dp9ik, WireGuard administration,
  `/sys/lib/plan2001` paths, demo/ISO/passkey gateway and native Plan 9 MCP
  instructions are retired. Do not use them to administer, diagnose or
  restore this Ubuntu host, even if old source code or credentials remain.
- Diagnose or restore MCP services using the actual Ubuntu configuration,
  deployed files and Linux service management. Current SSH access, service
  names, paths, backups, ports and MCP endpoints must be verified; none are
  established by an old Plan 9 deployment report.
- Plan 9/9front code, local VMs and the browser guest remain separate project
  contexts. Their operating system does not describe plan2001.com.
- When resuming an older Codex, OpenCode or Copilot session, discard any
  conflicting host assumptions and follow these facts. Do not automatically
  redeploy either removed service.

## Historical source-publication decision (2026-11-04)

**Historical scope:** the 2026-11-07 workflow above supersedes this section's
source-transfer requirements. Its dated text and mechanism descriptions are
retained as evidence; they do not require running the old publisher or hooks
for a GitHub branch/PR task. Hardware/format authorization boundaries remain.

Owner decision, binding over older publishing notes at that date: there is **no production
kernel**. The kernel is a PoC on `kuha`. The full canonical source tree is
edited on `kuha` and published immediately to `plan2001.com`. Every task round
must publish the **complete current source tree**, including failed or
incomplete rounds. Never gate publication on a build or test result; record the
result honestly instead. The previous complete snapshot must always remain
available. Do not claim `kuha` and `plan2001.com` are identical before the
hash verification succeeds.

Use `python3 tools/host/mcp-publish.py` (or `make pub`), or the `EXIT`-trap
wrapper `tools/host/session-publish.sh`. Read
[docs/DEVELOPMENT-WORKFLOW.md](docs/DEVELOPMENT-WORKFLOW.md) before changing the
pipeline. This rule does **not** authorize hardware boot, p5/ESP writes, or
GE/O format changes; those remain separate, explicitly authorized actions.

**Automatic worker hook.** The canonical tree carries
`.opencode/plugins/plan2001-worker-publish.js` (implementation in
`tools/host/opencode/plan2001-worker-publish.js`). An OpenCode process started
in this worktree loads it automatically. It observes `task_claim` and
`task_report` tool completion, publishes the complete source tree through the
existing publisher after a report, and also publishes at `/new`, `session.idle`,
`session.error` and `session.deleted` for a claimed task that has not been
published. Publication is therefore not dependent on the agent remembering to
run a command. The hook keeps a machine-local state file under
`build/mcp-publish/` (never part of the published source). The manual commands
remain valid fallbacks.

The remote finalizer journals an activation before changing `CURRENT`, records
the session only after `CURRENT` is durable, and reconciles interrupted
finalizations idempotently. A snapshot that was activated but failed
post-verification is re-verified as the same session, not published again. An
old pending draft is kept in history but never moves `CURRENT` backwards past a
newer snapshot. Do not weaken these guarantees when editing the publisher.

## Project scope

**Plan2001-O is an experimental operating system for the amd64 architecture.**
amd64 is the only supported and intended target. Do not treat dormant
non-target sources as a supported port, a development objective or a reason to
retain dead code in the amd64 kernel. Future amd64 cleanup must assess only the
current amd64 build, its userspace and tools, and its real callers; there is no
obligation to keep amd64 code compatible with dormant non-target sources.

Consult the [Finnish philosophical manifesto](docs/OLIO-OBJEKTI-MANIFESTI.md) and the [English general software specification](docs/GENERAL-SOFTWARE-SPECIFICATION.md) when their concepts matter. The manifesto is **not an implementation mandate**. The Plan2001-O PoC **deliberately uses ordinary 8-bit bytes, 64-bit OIDs, existing Plan2001/9front code, and conventional amd64 hardware**. Distinguish the philosophical meaning of *olio* (uninterpreted entity) and relational *object* from the concrete byte-oriented storage chosen for this PoC. Do not redesign storage, hardware abstractions or existing working code merely to make them philosophically general.

**Source tree:** the host layout is documented in [REPOSITORY-LAYOUT](docs/REPOSITORY-LAYOUT.md). The directories (`boot/`, `kernel/`, `ge/`, `micropython/`, `runtime/`, `include/`, `tools/host/`) are a development/export view, not a GE/O namespace. Do not recreate a `sys/src` 9front root.

**Source-of-truth order:** current source; [O-FORMAT](docs/O-FORMAT.md) and [KERNEL-API-ABI](docs/KERNEL-API-ABI.md) for implemented storage/native/API contracts; [STATUS](docs/STATUS.md) for current verification; [README](README.md), [GENERAL-SOFTWARE-SPECIFICATION](docs/GENERAL-SOFTWARE-SPECIFICATION.md), [OBJECT-MODEL](docs/OBJECT-MODEL.md) and [PRODUCTION-BOOT-AND-AUTHORITY](docs/PRODUCTION-BOOT-AND-AUTHORITY.md) for design and limits; [LEGACY-DEPENDENCIES](docs/LEGACY-DEPENDENCIES.md) for the inherited dependency inventory. Inherited Plan 9/9front documentation is reference material, not a Plan2001-O specification. Read the relevant implementation contract when a change depends on it; do not read every large document for mechanical work.

## Practical PoC contract

**Goal:** a small, working, byte-oriented implementation of Space, olios, relational objects, observations, and eventually named graphs on an ordinary amd64 computer with a disk and network. Keep the philosophical manifesto independent of these contingent hardware choices.

- **Reuse before rewriting.** Begin with the working Plan2001-O code and its Plan2001/9front-derived kernel, GE/O storage, executable loader, processes, networking, console, and device drivers. Prefer a narrow modification or thin direct interface over replacing functioning subsystems. Remove an inherited abstraction only when it demonstrably obstructs the PoC and a simpler tested replacement exists.
- **Host preparation is native.** The O image is produced by the real `ge/format/ream.c::reamo()` code compiled as a Linux GCC host program, and its `.mpy` seed by the same-version Linux `mpy-cross`. The normal build must not require a 9front VM, Ken C `cc`/`6c`/`6l`, Plan 9 `mk` or a cached 9front image. The host formatter writes only a caller-selected regular image; physical partition writes remain the separate hardened installer.
- **No swap — architectural rule.** Plan2001-O MUST NOT implement swapping in any form: no disk/file/network-backed swap, compressed RAM swap, anonymous-memory page-out/page-in to backing storage, swap device, or swap service in the canonical kernel. Ordinary reclamation of clean, reproducible caches, demand-zero pages and normal COW are distinct from swap; do not disable those merely because they are named in inherited paging code. The inherited 9front `devswap`, pager and swap-backed fault paths are **legacy code to eliminate**, not intended Plan2001-O features; until the migration is verified, do not claim they are absent. A **future** privileged memory-supervision process is intended to monitor per-process memory budgets and terminate processes exceeding their limits; the required accounting/enforcement semantics are undecided and are NOT implemented by this rule. Do not implement that supervisor or replace swap with another paging mechanism without an explicit task.
- **Bytes are the native carrier for this PoC.** Store and transfer ordinary octet sequences. Use existing 64-bit identifiers and GE/O conventions where possible; keep application interpretation and graph relations logically distinct from raw content. No seven-bit, analogue, exotic hardware, or substrate-independent storage engine is required.
- **Hardware and access:** target the amd64 architecture on conventional hardware with persistent disk storage and networking. On a desktop, use the available display and keyboard. On a server, provide remote interaction over the network without requiring local GUI hardware. Reuse existing console/network/driver paths first. This is a target usage profile, not a claim that every mode is implemented today.
- **Keep it a proof of concept.** Demonstrate one coherent vertical slice, not a universal ontology engine, a new general-purpose desktop, a wholesale kernel rewrite, or a new network protocol for its own sake. Prefer the smallest complete behavior that can be built and tested.
- **Avoid premature generalization.** Any interpretation API or persistent graph is an incremental experiment. Do not implement speculative abstraction layers, type registries, class taxonomies, analogue representations, or cross-machine identity schemes merely because they are discussed philosophically.
- **Preserve operational evidence.** The current code stores versioned byte values and a serialized Python graph; do not claim a native relation graph or general observation API before they exist and are tested.

## p5 is a disposable development O volume

The owner has explicitly authorized destructive reaming of the experimental
physical p5 GE/O partition **contents**. p5 is a development fixture, not a
production database: its Python variables, the old a.out interpreter object,
prior version chains and demo state have **no preservation requirement**.
Ream and reseed p5 at the start of a fresh test/upgrade cycle whenever it
materially simplifies an experiment or validates the O storage design; a
reformat is part of an accepted test. Do **not** spend sessions preserving
throwaway variables or migrating old a.out data.

Only the correctly identified p5 partition contents may be destroyed. Verify
the live device, PARTUUID, exact byte size, partition number and non-mounted
state first. Never wipe the partition table, ESP, Debian partitions, swap,
other volumes or firmware; never guess a block device and never use a
whole-disk operation. Do not ream automatically on every normal boot:
persistent state must survive the two boots used to test recovery. The
photographed physical pre-cleanup GCC `oboot=server` boot reached an
interactive MicroPython REPL, ran `import space` / `space.info()`, and made
real GE/O commits: **PASS**. Cross-reboot persistence of a newly assigned REPL
variable is **NOT YET documented**. The cleaned N9.8 kernel is **NOT YET
physically booted**; that is the operator's next action.

## Evidence and reporting

- Distinguish **implemented** (visible in source), **demonstrated** (supported by a named executed test), and **proposed** (unimplemented design).
- Read the relevant code before asserting an API or behavior. Report the source state, exact commands run, their results and log paths. Never infer a successful test from a script or an older branch's logs.
- A broken gate is evidence to investigate, not permission to weaken it or silently change the specification.
- Update STATUS when verification changes, but keep it a short current-status document; do not append stage chronicles. Keep current instructions and reports self-contained.

### Verification policy: two sanctioned QEMU scripts, no broad test tree

Validation is normally by compiling the real artifacts with the normal
hosted toolchain. The owner has sanctioned exactly **two retained host-side
QEMU tools**:

- `tools/qemu-o-boot.sh` — one bounded, disposable QEMU/OVMF boot of a fresh
  native GE/O image;
- `tools/qemu-o-persist.sh` — two boots against the same disposable GPT disk
  and O partition, with a verified commit and restore.

They are documented in [QEMU-O-TESTING](docs/QEMU-O-TESTING.md), build or
reuse only canonical artifacts, and write only disposable regular files under
`build/`. They are not build or boot prerequisites. The canonical tree
retains **no other test harnesses, test suites, fixture generators, fixture
binaries, numbered diagnostic stages, kernel probe/debug boot selectors or
alternative boot modes**. Do not add such infrastructure merely to validate a
change. Report broad regressions honestly as **NOT RUN by design**, never as
PASS. Full regression campaigns, if ever needed, are separate tasks and do
not become build or boot prerequisites.

## Production server/embedded profile

The server and embedded production profile is a **design target**, not an
implemented security model. Follow [docs/PRODUCTION-BOOT-AND-AUTHORITY.md](docs/PRODUCTION-BOOT-AND-AUTHORITY.md)
for its boot, administration and authority boundaries. There is no native
all-powerful `root`/`sudo`/universal administrator account in that target.
Inherited `eve`/`hostowner`/`iseve` behavior is **legacy and currently
present**; do not change or quietly weaken it while migration work is
incomplete. Retain USB support and the existing UEFI and GRUB boot/recovery
workflows. Do not claim secure netboot, verified boot, encrypted GE/O,
passkeys or a native administration service is implemented unless a named
test demonstrates it.

## Architectural direction

- GE/O owns persistent object identity **and, as a goal, native persistent relationships**. Current identity is `(SpaceID, OID)` with a 64-bit OID within a GE volume, not a path or disk pointer.
- **A file is an O object's finite byte sequence**, not the combined Unix/Plan 9 metadata-plus-data record. The word `file` is a content abstraction/descriptive category, not a required `File` superclass, exclusive class, or on-disk `type=file` field.
- Files need no root directory, directory hierarchy, parent link, canonical pathname, Unix `stat` fields, or global namespace to exist. An object's optional properties and relations are separate from its file bytes.
- **Do not import Python's class system into GE/O.** Python `class`, inheritance, MRO, descriptors, modules, heap layout and runtime object types must not define the kernel's OID representation, native storage type hierarchy, or graph semantics.
- Python replaces the scripting layer and demonstrates persistent values, namespaces and code. GE/O must not depend on Python.
- Code, source and native executables may be ordinary byte-content files addressed by OID. A separate semantic program object may relate to one or more executable implementations (see OID 43/100 in OBJECT-MODEL). Those relations are **not implemented**. A bounded native O executable test path exists; general executable-object loading is **not implemented**.
- Native object-handle lifetime, indexing, permissions, IPC and remote operation remain research questions; do not invent a universal schema or protocol in advance. Natively scoped O handles and native child `rfork` were demonstrated earlier; they are not re-tested by cleanup gates.
- The current O path commits the Python graph as **one `OID_GRAPH` object**. Do not misrepresent this as native GE/O edges or per-object Python persistence.
- **Canonical target:** one amd64 `pc64`/`9pc64` configuration. The ordinary `oboot=server` boot loads MicroPython from O by fixed OID through native `OEXECUTE` without running `oinit`; `oinit`, the 9front kernel boot image and `kbdfs` are historical bootstrap material and are no longer built by the canonical tree. The complete boot is not pathless: the bootstrap and console still use inherited `/boot` and `/dev` interfaces.

## Boundaries and workflow

- Do not introduce a 9P adapter, fake directory-tree filesystem, implicit path-based O API or compatibility namespace to evade an open native-design question. Files and byte streams themselves are allowed and fundamental.
- For legacy migration, treat concrete Plan 9/Unix files as O byte objects. Do not recreate Plan 9 `bind`, `mount`, process-local hierarchical namespaces or `/bin` overlays. Explicit source paths may be used to disambiguate tools at import time; a source path is not an OID.
- Do not add external/string OIDs, a new native class hierarchy, a mandatory metadata schema or a GE/O v1 format change based only on the architectural examples. The `mk.py`/numeric build-manifest concept is not yet an implemented build API.
- Byte offsets, reads, writes and resource handles are useful operations, not by themselves a filesystem hierarchy.
- **Legacy code may remain as a bootstrap dependency; its presence is not an architectural requirement. Remove it only after an alternative has been demonstrated.**
- Preserve the O-only vertical slice and its demonstrated behavior. Prefer small reversible changes with real build evidence over speculative rewrites. Do not reintroduce retained historical tests or make them build or boot prerequisites, except the two sanctioned scripts above.
- Before work, inspect the relevant source and preserve unrelated local work. Keep changes scoped; do not discard or overwrite another agent's files.
- Never run image-formatting or `tools/gcc-ovol-ream.sh --install` against a real disk as part of routine tests. Use disposable images; p5 writes require the explicit destructive-fixture authorization above.
- Preserve source copyright, license notices and provenance. Importing a source archive does not transfer authorship.

See [docs/STATUS.md](docs/STATUS.md) for the **single maintained set of build, install and current-status commands**, and [docs/DEVELOPMENT-WORKFLOW.md](docs/DEVELOPMENT-WORKFLOW.md) for the binding source publication workflow.
