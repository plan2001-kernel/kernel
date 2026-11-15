#!/usr/bin/env python3
"""Offline corruption and layout regression for native C UxManifest."""
import struct
import tempfile
import unittest
from pathlib import Path
from seed_unix_manifest import (MANIFEST_OID, MANIFEST_BYTES, BOOT_DIRS,
                                fnv1a, initial_manifest, provision)


class ManifestTest(unittest.TestCase):
    def test_checked_layout(self):
        data = initial_manifest()
        self.assertEqual(len(data), MANIFEST_BYTES)
        self.assertEqual(struct.unpack_from("<QII", data), (0x3153465855323030, 1, 0))
        self.assertEqual(struct.unpack_from("<Q", data, len(data)-8)[0],
                         fnv1a(data[:-8]))
        for slot, directory in enumerate(BOOT_DIRS):
            offset = 16 + slot * 272
            self.assertEqual(data[offset:offset+256].split(b"\0", 1)[0],
                             directory.encode())
            self.assertEqual(struct.unpack_from("<QII", data, offset+256), (0, 0, 1))
        broken = bytearray(data)
        broken[100] ^= 1
        self.assertNotEqual(fnv1a(broken[:-8]), struct.unpack_from("<Q", broken, len(broken)-8)[0])

    def test_no_silent_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "env").write_bytes(b"test")
            provision(path)
            self.assertEqual((path / "bad").read_bytes(), initial_manifest())
            self.assertEqual((path / "badoid").read_text(), f"{MANIFEST_OID:016x}")
            with self.assertRaises(ValueError):
                provision(path)


if __name__ == "__main__":
    unittest.main()
