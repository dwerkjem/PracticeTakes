#!/usr/bin/env python3
"""Tests for the stale-binary check.

The failure this guards against produces no error of its own: a capture of an
old build succeeds, reports success, and writes images that look entirely
plausible. So the check has to be right, and the one thing it must never do is
cry wolf -- a warning that appears on a current build teaches people to ignore
it, which leaves them worse off than no warning at all.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import freshness  # noqa: E402


class FreshnessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / "src").mkdir()
        self.source = self.root / "src" / "Thing.cpp"
        self.source.write_text("int main() { return 0; }\n")

        self.build = self.root / "build-tc" / "bin"
        self.build.mkdir(parents=True)
        self.executable = self.build / "PracticeTakes"
        self.executable.write_text("binary")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def age(self, path: Path, seconds: float) -> None:
        stamp = path.stat().st_mtime - seconds
        import os

        os.utime(path, (stamp, stamp))

    def test_a_binary_newer_than_its_source_is_current(self) -> None:
        self.age(self.source, 60)

        self.assertFalse(freshness.is_stale(self.executable, self.root))
        self.assertEqual(freshness.staleness_warning(self.executable, self.root), "")

    def test_a_binary_older_than_its_source_is_stale(self) -> None:
        self.age(self.executable, 60)

        self.assertTrue(freshness.is_stale(self.executable, self.root))

    def test_a_new_source_file_makes_a_binary_stale(self) -> None:
        """Adding a file changes the binary without touching an existing one."""
        self.age(self.executable, 5)
        (self.root / "src" / "Added.cpp").write_text("// new\n")

        self.assertTrue(freshness.is_stale(self.executable, self.root))

    def test_a_changed_cmake_list_makes_a_binary_stale(self) -> None:
        self.age(self.executable, 5)
        (self.root / "CMakeLists.txt").write_text("# changed\n")

        self.assertTrue(freshness.is_stale(self.executable, self.root))

    def test_a_missing_binary_is_not_reported_as_stale(self) -> None:
        """Absent is a different problem, and the caller already reports it."""
        self.executable.unlink()

        self.assertFalse(freshness.is_stale(self.executable, self.root))

    def test_a_tree_with_no_sources_never_warns(self) -> None:
        """A check that cannot see the sources must not block the run."""
        self.source.unlink()

        self.assertFalse(freshness.is_stale(self.executable, Path(self.root / "nowhere")))

    def test_the_warning_names_the_way_out(self) -> None:
        self.age(self.executable, 60)
        warning = freshness.staleness_warning(self.executable, self.root)

        self.assertIn("older than the source", warning)
        self.assertIn("cmake --build", warning)
        self.assertIn("build-tc", warning)


if __name__ == "__main__":
    unittest.main()
