#!/usr/bin/env python3
"""Hermetic checks for the libc audit's symbol parsing and gap accounting."""
import tempfile
import unittest
from pathlib import Path

import libc_audit


class LibcAuditTests(unittest.TestCase):
    def test_nm_posix_parser_ignores_archive_headers_and_weak_undefined(self):
        raw = (
            "libc-user.a[libc_malloc.o]:\n"
            "malloc T 00000000 00000018\n"
            "memcpy T 00000040 00000018\n"
            "unused_weak w\n"
            "missing U\n"
            "libc-user.a[libc_calloc.o]:\n"
            "calloc T 00000000 00000018\n"
        )
        self.assertEqual(
            libc_audit.symbols_from_nm(raw, undefined=False),
            {"malloc", "memcpy", "calloc"},
        )
        self.assertEqual(
            libc_audit.symbols_from_nm(raw, undefined=True), {"missing"}
        )

    def test_host_internal_references_are_removed(self):
        report = libc_audit.analyze(
            {"main", "memcpy", "open", "__errno_location"},
            {"memcpy", "rt_start"},
            {"main", "other_host_symbol"},
        )
        self.assertEqual(report["in_plan2001_direct_runtime"], ["memcpy"])
        self.assertEqual(
            report["not_in_plan2001_direct_runtime"],
            ["__errno_location", "open"],
        )
        self.assertEqual(
            report["gap_categories"]["open"], "userspace-namespace-or-io")
        self.assertEqual(
            report["gap_categories"]["__errno_location"],
            "linux-host-toolchain-candidate",
        )

    def test_compiled_sources_taken_from_manifest_not_source_glob(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            path = base / "tools/host/build-mpy-gcc.py"
            path.parent.mkdir(parents=True)
            path.write_text('LIBC_SOURCES = ["9sys/sbrk.c", "port/malloc.c"]\n',
                            encoding="utf-8")
            self.assertEqual(
                libc_audit.built_source_manifest(base),
                ["9sys/sbrk.c", "port/malloc.c"],
            )


if __name__ == "__main__":
    unittest.main()
