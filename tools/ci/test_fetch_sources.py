"""Offline validation of the live-kernel/immutable-BusyBox checkout policy."""
import unittest

from fetch_sources import source_revision


class TestSources(unittest.TestCase):
    def test_kernel_main_is_required(self):
        self.assertEqual(source_revision("kernel", {
            "repository": "plan2001-kernel/kernel", "ref": "main"
        }), "refs/heads/main")

    def test_kernel_sha_is_rejected(self):
        with self.assertRaises(ValueError):
            source_revision("kernel", {
                "repository": "plan2001-kernel/kernel",
                "ref": "main", "commit": "a" * 40
            })

    def test_kernel_other_branch_is_rejected(self):
        with self.assertRaises(ValueError):
            source_revision("kernel", {
                "repository": "plan2001-kernel/kernel", "ref": "develop"
            })

    def test_busybox_sha_is_required(self):
        sha = "b" * 40
        self.assertEqual(source_revision("busybox", {
            "repository": "mirror/busybox", "commit": sha
        }), sha)

    def test_reject_wrong_repo_or_moving_busybox(self):
        for entry in (
            {"repository": "fake/kernel", "ref": "main"},
            {"repository": "plan2001-kernel/kernel", "ref": "main", "commit": ""},
        ):
            with self.assertRaises(ValueError):
                source_revision("kernel", entry)
        with self.assertRaises(ValueError):
            source_revision("busybox", {
                "repository": "mirror/busybox", "ref": "master"
            })


if __name__ == "__main__":
    unittest.main()
