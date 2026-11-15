#!/usr/bin/env python3
"""Offline corruption and layout regression for native C UxManifest."""
import os
import sys
import struct
import tempfile
import unittest
from pathlib import Path
from seed_unix_manifest import (MANIFEST_OID, MANIFEST_BYTES, BOOT_DIRS,
                                fnv1a, initial_manifest, provision)


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.kernel=Path(os.environ['PLAN2001_KERNEL'])
        sys.path.insert(0,str(self.kernel/'tools/host'))

    def test_checked_layout(self):
        from object_format import unpack
        data,metadata=unpack(initial_manifest(self.kernel))
        self.assertTrue(metadata)
        self.assertEqual(len(data), MANIFEST_BYTES)
        self.assertEqual(struct.unpack_from("<QII", data), (0x3153465855323030, 3, 0))
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
            provision(path,self.kernel)
            self.assertEqual((path / "bad").read_bytes(), initial_manifest(self.kernel))
            self.assertEqual((path / "badoid").read_text(), f"{MANIFEST_OID:016x}")
            with self.assertRaises(ValueError):
                provision(path,self.kernel)


if __name__ == "__main__":
    unittest.main()
