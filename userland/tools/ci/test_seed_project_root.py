"""Independent durable root seeds through the existing formatter inputs."""
import os
from pathlib import Path
import struct
import tempfile
import unittest
from seed_project_root import provision, initial_root, F6_PROJECT_OID


class ProjectSeedsTest(unittest.TestCase):
    def test_second_root_is_complete_version_one_object(self):
        kernel = Path(os.environ['PLAN2001_KERNEL'])
        with tempfile.TemporaryDirectory() as directory:
            seed = Path(directory)
            (seed/'env').write_bytes(b'fixture')
            provision(seed, kernel, second_root=True)
            raw = (seed/'raw').read_bytes()
            self.assertEqual(raw[:8], b'RAWREC01')
            nk, nv = struct.unpack_from('>II', raw, 8)
            self.assertEqual(nk, 21)
            key, value = raw[16:16+nk], raw[16+nk:]
            self.assertEqual(len(value), nv)
            self.assertEqual(struct.unpack('>QQI', key[1:]),
                             (F6_PROJECT_OID, (1 << 64)-2, 0))
            self.assertEqual(struct.unpack_from('>I', value)[0], len(value)-4)
            self.assertEqual(value[4:], initial_root(kernel))
            self.assertEqual(value[4:], (seed/'bad').read_bytes())
            before = {p.name: p.read_bytes() for p in seed.iterdir()}
            with self.assertRaises(ValueError):
                provision(seed, kernel, second_root=True)
            self.assertEqual(before, {p.name: p.read_bytes() for p in seed.iterdir()})

    def test_existing_raw_seed_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            seed = Path(directory)
            (seed/'env').write_bytes(b'fixture')
            (seed/'raw').write_bytes(b'existing')
            with self.assertRaises(ValueError):
                provision(seed, Path(os.environ['PLAN2001_KERNEL']), second_root=True)
            self.assertEqual((seed/'raw').read_bytes(), b'existing')
            self.assertFalse((seed/'bad').exists())
