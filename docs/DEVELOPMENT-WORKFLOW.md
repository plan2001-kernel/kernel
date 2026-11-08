# Development workflow

Canonical repository: https://github.com/plan2001-kernel/kernel

Development may use Codex Cloud, Android Termux/OpenCode, or another suitable
environment. No particular workstation, external agent queue, MCP service,
or source-publication host is required.

1. Read the current source, `AGENTS.md`, and only the technical document
   relevant to the task.
2. Make the smallest requested change. Avoid speculative ports, compatibility
   layers, and unrequested rewrites.
3. Build and run checks that the environment actually supports. Report
   **PASS**, **FAIL**, or **NOT RUN** accurately. ARM64 host execution does not
   prove an AMD64 boot, and early ARM64 QEMU boot does not prove a full port.
4. Submit a focused pull request against `main`. Do not merge without an
   explicit instruction to do so.

## Documentation and local task records

**Do not commit documentation for individual tasks, experiments or test
runs.** Keep plans, work notes, stdout/stderr logs, measurement tables,
verification evidence and generated reports in the agent's own working
directory, such as `/.agent-work/<task>/` within the checkout. Git ignores
`/.agent-work/`. A working directory outside the repository is also valid.
Never override that ignore with `git add -f` or move those records into
`docs/`, the repository root or another tracked directory.

GitHub documentation contains **only long-lived, generally applicable
architectural decisions, interface contracts, project goals and agent
instructions**. Update it only for an expressly requested, lasting change.
Do not add stage-specific reports, retrospective audits, roadmap checklists,
per-experiment JSON output or other transient documentation. Reusable tests
and fixtures may be versioned as code; their per-run results must remain local.

Before committing, inspect `git status --short` and the staged diff so that
no transient files are included. Report the concise verification outcome in
the PR itself, not in a new repository document. Git history contains any
previously committed historical material without keeping it in active context.
