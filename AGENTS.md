# Plan 2001-kernel: agent instructions

This is an experimental AMD64/ARM64 operating-system kernel. AMD64 currently
has the functional full-system path; ARM64 has a verified early-boot foundation,
not an equivalent full kernel. Do not narrow the project's intended target to
AMD64 or claim that ARM64 userspace and drivers already work.

## Scope

- Persistent identity: GE/O `(SpaceID, OID)`. There is no native hierarchical
  filesystem namespace. Runtime objects, capabilities, and stored occurrences
  are different concepts.
- Object operations belong to resources with object/capability semantics.
  Direct scalar syscalls are allowed. **There is no fixed syscall-count goal.**
- ObjectFIFO is the generic framed device/IPC mechanism. Keep USB control
  completion distinct from asynchronous Ethernet FIFO receive semantics.
- MicroPython is the current userspace proof of concept. Do not add larger
  userspace products, protocol stacks, or compatibility requirements without
  an explicit request.
- Do not restore obsolete path, Plan 9 Chan/fid/qid, /dev, 9P, or protocol-
  specific kernel syscall abstractions to satisfy removed experiments.

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
