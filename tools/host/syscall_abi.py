"""Read syscall numbers and retirement status from the public ABI header."""
import re
from pathlib import Path


def syscall_entries():
    entries = []
    names, numbers = set(), set()
    header = Path(__file__).resolve().parents[2] / "runtime/abi/sys.h"
    for line in header.read_text().splitlines():
        match = re.fullmatch(r"#define\s+([A-Z0-9_]+)\s+(\d+)(?:[ \t]+(/\* reserved \*/))?", line)
        if not match:
            if re.match(r"#define\s+[A-Z0-9_]+\s+\d+", line):
                raise SystemExit(f"syscall ABI: malformed entry: {line}")
            continue
        name, number, reserved = match.groups()
        number = int(number)
        if name in names or number in numbers:
            raise SystemExit(f"syscall ABI: duplicate name/number: {line}")
        names.add(name)
        numbers.add(number)
        entries.append((name, number, reserved is not None))
    if not entries:
        raise SystemExit("syscall ABI: empty table")
    return entries
