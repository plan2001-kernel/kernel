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

Historical test reports belong to repository history, not the live
`docs/` tree. Actual source and executed tests override obsolete prose.
