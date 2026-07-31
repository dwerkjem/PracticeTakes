#!/usr/bin/env python3
"""Tests for the tests/-mirrors-src/ layout check."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_test_layout import find_problems  # noqa: E402


class CheckTestLayoutTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary.name)
        self.tests = self.root / "tests"
        self.source = self.root / "src"
        self.tests.mkdir()
        self.source.mkdir()

        self.addCleanup(self._temporary.cleanup)

    def write_test(self, relative: str) -> None:
        path = self.tests / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("// test\n", encoding="utf-8")

    def make_source_directory(self, relative: str) -> None:
        (self.source / relative).mkdir(parents=True, exist_ok=True)

    def test_a_conforming_tree_has_no_problems(self) -> None:
        self.make_source_directory("services/score")
        self.write_test("services/score/TempoMapTests.cpp")

        self.assertEqual(find_problems(self.tests, self.source), [])

    def test_a_test_at_the_root_is_rejected(self) -> None:
        """The root is where files land when nobody decided where they belong."""
        self.write_test("StrayTests.cpp")

        problems = find_problems(self.tests, self.source)

        self.assertEqual(len(problems), 1)
        self.assertIn("StrayTests.cpp", problems[0])

    def test_a_mirrored_path_with_no_source_is_rejected(self) -> None:
        """Usually means the source moved and the test did not."""
        self.write_test("services/ghost/GhostTests.cpp")

        problems = find_problems(self.tests, self.source)

        self.assertEqual(len(problems), 1)
        self.assertIn("does not exist", problems[0])

    def test_an_exempt_directory_is_accepted(self) -> None:
        """Shared fixtures belong to no one source directory."""
        self.write_test("support/BenchmarkFakes.h")

        self.assertEqual(find_problems(self.tests, self.source), [])

    def test_nested_paths_under_an_exempt_directory_are_accepted(self) -> None:
        self.write_test("support/audio/FakeDevice.h")

        self.assertEqual(find_problems(self.tests, self.source), [])

    def test_a_directory_named_like_an_exempt_one_deeper_down_is_not_exempt(self) -> None:
        """Only the top-level directory is exempt, not any directory sharing its name."""
        self.write_test("services/support/Thing.cpp")

        problems = find_problems(self.tests, self.source)

        self.assertEqual(len(problems), 1)

    def test_headers_are_checked_too(self) -> None:
        """A stray fixture header is as misplaced as a stray test."""
        self.write_test("Helper.h")

        problems = find_problems(self.tests, self.source)

        self.assertEqual(len(problems), 1)

    def test_non_source_files_are_ignored(self) -> None:
        """A README or a fixture resource is not a test and has no mirror."""
        (self.tests / "README.md").write_text("notes\n", encoding="utf-8")

        self.assertEqual(find_problems(self.tests, self.source), [])

    def test_every_violation_is_reported_not_just_the_first(self) -> None:
        """One run should list all the work, rather than one problem per run."""
        self.write_test("FirstStrayTests.cpp")
        self.write_test("SecondStrayTests.cpp")
        self.write_test("services/ghost/GhostTests.cpp")

        self.assertEqual(len(find_problems(self.tests, self.source)), 3)

    def test_a_missing_tests_directory_is_a_problem(self) -> None:
        """Silently passing when there are no tests at all would defeat the check."""
        problems = find_problems(self.root / "absent", self.source)

        self.assertEqual(len(problems), 1)

    def test_the_real_repository_tree_conforms(self) -> None:
        """The check has to hold for this repository, or it is not enforcing anything."""
        repository_root = Path(__file__).resolve().parents[2]

        self.assertEqual(
            find_problems(repository_root / "tests", repository_root / "src"), []
        )


if __name__ == "__main__":
    unittest.main()
