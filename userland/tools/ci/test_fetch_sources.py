"""Offline validation of the immutable BusyBox source policy."""
import unittest

from fetch_sources import source_revision


class TestSources(unittest.TestCase):
    def test_busybox_sha_is_required(self):
        sha = "b" * 40
        self.assertEqual(source_revision("busybox", {
            "repository": "mirror/busybox", "commit": sha
        }), sha)

    def test_reject_wrong_repo_or_moving_busybox(self):
        with self.assertRaises(ValueError):
            source_revision("busybox", {
                "repository": "fake/busybox", "commit": "b" * 40
            })
        with self.assertRaises(ValueError):
            source_revision("busybox", {
                "repository": "mirror/busybox", "ref": "master"
            })


if __name__ == "__main__":
    unittest.main()
