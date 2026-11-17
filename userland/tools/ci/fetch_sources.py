#!/usr/bin/env python3
"""Prepare the checked-out kernel and fetch immutable BusyBox sources."""
import argparse
import json
import re
import subprocess
from pathlib import Path

EXPECTED = {"busybox": "mirror/busybox"}


def git(*args, cwd=None):
    return subprocess.check_output(["git", *map(str, args)], cwd=cwd, text=True).strip()


def source_revision(name, entry):
    if entry.get("repository") != EXPECTED[name]:
        raise ValueError(f"{name}: repository mismatch")
    commit = entry.get("commit")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("busybox requires an immutable 40-digit lowercase commit")
    if "ref" in entry:
        raise ValueError("busybox must not use a moving ref")
    return commit


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads(args.lock.read_text(encoding="utf-8"))
    requests = {name: source_revision(name, lock[name]) for name in EXPECTED}

    args.dest.mkdir(parents=True, exist_ok=True)
    args.reports.mkdir(parents=True, exist_ok=True)
    kernel = Path(__file__).resolve().parents[3]
    for marker in ("runtime/abi/sys.h", "tools/build-gcc.sh"):
        if not (kernel / marker).is_file():
            raise SystemExit(f"missing kernel source: {kernel / marker}")
    target = args.dest / "kernel"
    target.symlink_to(kernel, target_is_directory=True)
    records = {"kernel": {
        "repository": "plan2001-kernel/kernel",
        "commit": git("rev-parse", "HEAD", cwd=kernel),
        "path": str(target),
    }}
    for name, requested in requests.items():
        repo = EXPECTED[name]
        target = args.dest / name
        if target.exists() or target.is_symlink():
            raise SystemExit(f"{name}: refusing existing checkout {target}")
        target.mkdir()
        git("init", "-q", target)
        git("remote", "add", "origin", f"https://github.com/{repo}.git", cwd=target)
        print(f"Fetching {name} {repo}@{requested}", flush=True)
        git("fetch", "-q", "--depth=1", "--no-tags", "origin", requested, cwd=target)
        actual = git("rev-parse", "FETCH_HEAD", cwd=target)
        if actual != requested:
            raise SystemExit(f"{name}: checkout mismatch: expected {requested}, got {actual}")
        git("checkout", "-q", "--detach", actual, cwd=target)
        if git("rev-parse", "HEAD", cwd=target) != actual:
            raise SystemExit(f"{name}: detached checkout differs from fetched SHA")
        record = {"repository": repo, "commit": actual, "path": str(target)}
        records[name] = record

    (args.reports / "sources.json").write_text(
        json.dumps(records, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    summary = (
        "# Plan2001 userland CI scope\n\n"
        "Kernel: same checkout as userland. BusyBox: immutable commit.\n\n"
        + "".join(f"- {name}: `{record['repository']}@{record['commit']}`\n"
                  for name, record in records.items())
        + "\n**Host-control BusyBox uses Linux libc, not Plan2001 libc.**\n\n"
          "| Check | Status |\n| --- | --- |\n"
          "| Same-checkout kernel and BusyBox pin | PASS |\n"
          "| Plan2001 libc + ABI inventory | see audit report |\n"
          "| BusyBox Linux host-control compilation | see host-control report |\n"
          "| Native BusyBox link/QEMU | see separate workflow reports |\n"
          "| Physical hardware | **NOT RUN** |\n"
    )
    (args.reports / "SUMMARY.md").write_text(summary, encoding="utf-8")
    print(f"Using checked-out kernel {records['kernel']['commit']}", flush=True)
    print("Verified detached exact source revisions for this job", flush=True)


if __name__ == "__main__":
    main()
