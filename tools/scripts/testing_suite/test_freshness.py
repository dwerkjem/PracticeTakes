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


class SuiteFreshnessTests(unittest.TestCase):
    """The hub outliving the code it was started from.

    Caught in practice: a hub started an hour before a feature landed served
    the new page, which asked it to delete a comment. The route did not exist
    in the running process, so the button did nothing and the comments it was
    attached to rendered blank. Nothing was wrong with either half.
    """

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.module = self.root / "server.py"
        self.module.write_text("# code\n")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def touch(self, path: Path, offset: float) -> None:
        import os

        stamp = path.stat().st_mtime + offset
        os.utime(path, (stamp, stamp))

    def test_code_unchanged_since_start_does_not_warn(self) -> None:
        loaded = freshness.newest_suite_source_time(self.root)

        self.assertGreater(loaded, 0.0)
        self.assertEqual(freshness.suite_staleness_warning(loaded, self.root), "")

    def test_an_edited_module_warns(self) -> None:
        loaded = freshness.newest_suite_source_time(self.root)
        self.touch(self.module, 60)

        self.assertIn("older than the files on disk", freshness.suite_staleness_warning(loaded, self.root))

    def test_a_new_module_warns(self) -> None:
        """A feature usually arrives as a file, not as an edit to one."""
        loaded = freshness.newest_suite_source_time(self.root)
        added = self.root / "display.py"
        added.write_text("# new\n")
        self.touch(added, 60)

        self.assertNotEqual(freshness.suite_staleness_warning(loaded, self.root), "")

    def test_a_changed_web_asset_does_not_warn(self) -> None:
        """`web/` is re-read per request, so it cannot go out of step."""
        loaded = freshness.newest_suite_source_time(self.root)
        (self.root / "web").mkdir()
        asset = self.root / "web" / "app.js"
        asset.write_text("// new\n")
        self.touch(asset, 600)

        self.assertEqual(freshness.suite_staleness_warning(loaded, self.root), "")

    def test_an_unknown_start_time_never_warns(self) -> None:
        self.touch(self.module, 600)

        self.assertEqual(freshness.suite_staleness_warning(0.0, self.root), "")

    def test_the_warning_says_what_to_do(self) -> None:
        loaded = freshness.newest_suite_source_time(self.root)
        self.touch(self.module, 60)

        self.assertIn("start it again", freshness.suite_staleness_warning(loaded, self.root))


if __name__ == "__main__":
    unittest.main()
