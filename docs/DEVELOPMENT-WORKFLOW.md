# Plan2001-O — development workflow

The canonical source repository is
[plan2001-kernel/kernel](https://github.com/plan2001-kernel/kernel).
Development can take place in Codex Cloud, Android Termux with OpenCode, or
another suitable environment. No specific machine, local checkout path,
external task queue, or source publication service is required.

1. Obtain the task directly from the user or development environment.
2. Read the current repository and applicable project instructions.
3. Implement the requested change with ordinary Git version control.
4. Run the available checks and report their actual results. Mark checks that
   cannot run in the current environment **NOT RUN**. An ARM64 Termux host is
   not expected to execute amd64 kernel tests natively.
5. Submit a focused pull request against `main` for review.
6. Never merge without explicit authorization.

Build and test commands remain in [README](../README.md) and the relevant test
documents. Physical disk operations are separate from routine development
checks and must not be used as a substitute for disposable tests.

Dated test reports and technical evidence elsewhere in `docs/` describe their
own historical execution environments. They do not impose current development
machine or publication requirements.
