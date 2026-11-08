# Plan 2001-kernel: agent instructions

This is an experimental AMD64/ARM64 operating-system kernel. AMD64 currently
has the functional full-system path; ARM64 has a verified early-boot foundation,
not an equivalent full kernel. Do not narrow the project's intended target to
AMD64 or claim that ARM64 userspace and drivers already work.

## Scope

- Persistent identity: GE/O `(SpaceID, OID)`. There is no native hierarchical
  filesystem namespace. Runtime objects, capabilities, and stored occurrences
  are different concepts.
- Persistent namespaces are OID-identified GE/O objects with explicit
  OID membership relations, not directory trees. The target set algebra is
  membership, union, intersection, and difference; process `OContext`
  visibility and capabilities remain separate from stored membership.
- Never implement or restore kernel hashtag/tag semantics: no `#`-tag
  validation, tag-name objects, tag-specific indexes or tag query parsers.
  Application-specific tags and query languages belong in userspace. An OID
  or set membership never grants authority. Do not preserve legacy GE R1
  hashtag metadata for compatibility: experimental volumes may be reamed
  and objects reimported.
- Object operations belong to resources with object/capability semantics.
  Direct scalar syscalls are allowed. **There is no fixed syscall-count goal.**
- ObjectFIFO is the generic framed device/IPC mechanism. Keep USB control
  completion distinct from asynchronous Ethernet FIFO receive semantics.
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
