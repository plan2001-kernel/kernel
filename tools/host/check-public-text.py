#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
import sys

TOKEN = "".join(map(chr, (103, 105, 116)))
PATTERN = re.compile(r"\b" + re.escape(TOKEN) + r"(?:hub|ignore|ignored)?\b", re.IGNORECASE)
SKIP_DIRS = {"build", "__pycache__", ".cache", "node_modules"}


def generated(path: Path) -> bool:
    return any(part in SKIP_DIRS for part in path.parts) or path.suffix == ".pyc"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    root = parser.parse_args().root.resolve()
    errors = []
    for path in root.rglob("*"):
        rel = path.relative_to(root)
        if generated(rel):
            continue
        label = rel.as_posix()
        if PATTERN.search(label):
            errors.append(f"path: {label}")
        if not path.is_file() or path.is_symlink():
            continue
        try:
            data = path.read_bytes()
            if b"\0" in data[:8192]:
                continue
            text = data.decode("utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        if PATTERN.search(text):
            errors.append(f"text: {label}")
    if errors:
        print("public-text-check: forbidden legacy references found:", file=sys.stderr)
        for item in errors:
            print(item, file=sys.stderr)
        return 1
    print("public-text-check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
