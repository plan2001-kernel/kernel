#!/usr/bin/env python3
"""Phase 3 regression: GE/O persistence is a flat OID volume, not ObjectSpace."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]

def assert_contains(path, pattern):
    text = (root / path).read_text()
    assert re.search(pattern, text), f"{path}: required {pattern}"

def assert_absent(path, pattern):
    text = (root / path).read_text()
    assert not re.search(pattern, text), f"{path}: prohibited {pattern}"

# No live SpaceID catalog, Ospace API, named selectors or shims.
for path, patterns in {
    "ge/storage/gdat.h": [r"\bstruct Ospace\b", r"\btypedef struct Ospace\b"],
    "ge/storage/gfns.h": [r"\bospace(open|get|put|commit|close|create)\s*\(", r"\bonative(open|get|put|create|commit|close)\s*\("],
    "ge/storage/o.c": [r"\bospaceopen\s*\(", r"\bonativeopen\s*\("],
    "ge/storage/obj.c": [r"\bOspace\s*\*", r"\bospaceopen\s*\("],
    "ge/storage/sysgefs.c": [r"\bosopenspace\s*\(", r"\bonativeopen\s*\("],
    "runtime/abi/obj.h": [r"\bObjOpenSpace\b", r"\bObjOpenGeSpace\b"],
    "runtime/abi/vobj.h": [r"\bVkindSpace\b", r"\bVopenSpace\b", r"\bVopenGeSpace\b"],
    "micropython/p9space.c": [r'obj_open\(ObjOpenVolume,\s*\(void\s*\*\)name'],
    "micropython/main.c": [r'\-S space', r'\-S init'],
}.items():
    for pattern in patterns:
        assert_absent(path, pattern)

for path, pattern in [
    ("ge/storage/obj.c", r"\boobjopen\s*\(void\s*\*volume\)"),
    ("ge/storage/obj.c", r"\boobjcreate\s*\(void\s*\*handle"),
    ("kernel/core/sysvobj.c", r"\bprovider == VopenGeVolume\b"),
    ("kernel/core/sysvobj.c", r"\bprovider == VopenGeOid\b"),
    ("kernel/core/sysobj.c", r"\bcase ObjOpenVolume\b"),
    ("runtime/abi/obj.h", r"\bObjOpenGeOid\b"),
    ("micropython/main.c", r"__import__\('space'\)\.open\(\)"),
]:
    assert_contains(path, pattern)

print("PASS: phase 3 flat GE/O volume and explicit-capability ABI contract")
