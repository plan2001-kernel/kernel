# Plan 2001-kernel: agent instructions

## Target architecture and mandatory deletion

Read [the authoritative target architecture](docs/ARCHITECTURE.md) first.
**Target:** [docs/OBJECTSPACE.md](docs/OBJECTSPACE.md). ObjectSpace is an
independent named capability graph with explicit attenuation, local labels,
BindingIDs, multiple roots and arbitrary object targets. There is no native
directory or process parent tree. OwnerID is immutable provenance, NEVER
ambient access authority. GE/O, region/device and process providers use
revocable attenuated Handle rights; owner equality checks were removed in
the capability-authority phase. Bootstrap permission, GE/O scope and
Kowner integrity, device exclusivity and page protections remain explicit
security conditions. Never reintroduce owner-equality authorization.
No historical disk or syscall compatibility is required.

**Do not redesign IPC.** The current memory-publication regions, one RW
publisher, RO observers and WAIT/WAKE are retained. Data received through
that mechanism belongs to the recipient as a data value; the original
publisher-owned object is unchanged. Do not introduce generic kernel
message queues, send/receive system calls, FIFO, Pipe, copy transport
or new user programs as part of this ownership-documentation change.
Current capability ABI, generic revocation and cross-owner delegation are
implemented. Do not treat the historical OwnerID-only phase as a constraint.

Whenever a phase replaces a mechanism, **remove** its obsolete code, public
ABI, wrappers/adapters, dormant providers, fixtures, tests and docs. List
exact deleted paths in the phase PR. Do not preserve old syscall numbers,
compatibility aliases, shims, dual paths or feature toggles solely for
rejected behavior. Every phase PR states Added, Deleted, Tests actually run,
NOT RUN and Remaining obsolete code. Physical deletion includes dead code;
compatibility retention is not the default.

The previous arch-reset/ownerid phases are historical and merged to main.
Keep bounded reviewable PRs, including stacked PRs during dependent phases;
do not merge without explicit approval.

This is an experimental AMD64/ARM64 operating-system kernel. AMD64 currently
has the functional full-system path; ARM64 has a verified early-boot foundation,
not an equivalent full kernel. Do not narrow the project's intended target to
AMD64 or claim that ARM64 userspace and drivers already work.

## Scope

- Persistent identity: GE/O `(SpaceID, OID)`. There is no native hierarchical
  filesystem namespace. Runtime objects, owner identities and stored occurrences
  are different concepts.
- Native ObjectSpace is a first-class kernel object, independent of Pgrp.
  Each binding has a label, stable BindingID, target and attenuated rights.
- GE SpaceID is a private storage bucket, NOT the ObjectSpace graph identity.
  Persistent ObjectSpace metadata needs atomic GE/O transactions and trusted
  capability reconstruction; these are not yet implemented.
- Pgrp/OContext are legacy to delete after explicit ObjectSpace roots replace
  them. OwnerID equality access gates have already been removed. Do not
  reintroduce them or add compatibility wrappers.
- Never implement or restore kernel hashtag/tag semantics: no `#`-tag
  validation, tag-name objects, tag-specific indexes or tag query parsers.
  Application-specific tags and query languages belong in userspace. An OID
  or set membership never grants authority. Do not preserve legacy GE R1
  hashtag metadata for compatibility: experimental volumes may be reamed
  and objects reimported.
- Object operations belong to owned resources with object semantics.
  Direct scalar syscalls are allowed. **There is no fixed syscall-count goal.**
- Keep low-level device mechanisms. Ethernet receive and console sessions
  currently use capability regions with WAIT/WAKE; USB control has distinct
  synchronous completion. Do not introduce another universal kernel message
  queue, FIFO or Pipe.
- MicroPython is the current userspace proof of concept. Do not add larger
  userspace products, protocol stacks, or compatibility requirements without
  an explicit request.
- Do not restore obsolete path, Plan 9 Chan/fid/qid, /dev, 9P, or protocol-
  specific kernel syscall abstractions to satisfy removed experiments.

## Local-only work records (mandatory)

Keep all **individual task and experiment documentation** only in the agent's
own local working directory, preferably `/.agent-work/<task>/` inside the
checkout. This includes plans, reasoning notes, run logs, test outputs,
verification summaries, screenshots, intermediate results and generated reports.
The entire `/.agent-work/` directory is ignored by Git. An equivalent
directory outside the repository is also acceptable.

**Never commit or open a PR containing task-specific documentation or test
reports**, regardless of filename or directory; in particular, do not recreate
`docs/T*` reports, audit ledgers, one-off roadmaps or verification dumps. Do
not bypass the ignore rule with `git add -f`. Review staged files before each
commit and remove local reports from the staging area.

Tracked GitHub documentation is limited to **durable project-wide architecture,
interfaces, principles, goals and development instructions**. Edit those only
when an enduring requirement genuinely changes and the task calls for that
change. A test run is not by itself a reason to update repository docs.
Summarize results and checks briefly in the PR description instead. Necessary
test source code and reusable test fixtures are allowed as implementation,
but their per-run logs and reports remain local.

## Working method

Use the GitHub repository `plan2001-kernel/kernel` as canonical source.
There is no mandatory development host, source mirror, external MCP server,
or agent scheduler. Use the current source and only relevant documentation:
[architecture](docs/ARCHITECTURE.md),
[storage](docs/OBJECT-STORAGE.md),
[kernel interface](docs/KERNEL-INTERFACE.md), and
[workflow](docs/DEVELOPMENT-WORKFLOW.md).

Implement one bounded request. Validate actual behavior with available build
and test tools; label unavailable verification **NOT RUN**, never PASS.
Create a focused PR for review; do not merge without explicit authorization.
Do not recreate deleted test reports, roadmap stages, or speculative
documentation as background context. Historical evidence remains in Git
history and may be consulted only for a concrete need.
