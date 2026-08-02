"""Unit tests for the CMake source-list merge driver.

The driver edits a build file automatically during a merge, so the cases that
matter most are the ones where it must *refuse*: anything that is not purely a
list of paths has to stay conflicted rather than be silently unioned.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
from typing import Iterator
import unittest

import contextlib


MODULE_PATH = Path(__file__).with_name("merge_cmake_sources.py")
SPEC = importlib.util.spec_from_file_location("merge_cmake_sources", MODULE_PATH)
assert SPEC and SPEC.loader
driver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(driver)


@contextlib.contextmanager
def versions(base: str, ours: str, theirs: str) -> Iterator[tuple[Path, Path, Path]]:
    """Write the three sides to disk the way Git hands them to a driver."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        paths = []
        for name, text in (("base", base), ("ours", ours), ("theirs", theirs)):
            path = root / name
            path.write_text(text, encoding="utf-8")
            paths.append(path)
        yield tuple(paths)  # type: ignore[misc]


def block(*entries: str) -> str:
    body = "".join(f"        {entry}\n" for entry in entries)
    return f"target_sources(PracticeTakes\n    PRIVATE\n{body})\n"


class SourceListMergeTests(unittest.TestCase):
    def test_each_side_adding_a_path_keeps_both(self) -> None:
        with versions(
            block("src/a/Alpha.cpp", "src/c/Gamma.cpp"),
            block("src/a/Alpha.cpp", "src/b/Beta.cpp", "src/c/Gamma.cpp"),
            block("src/a/Alpha.cpp", "src/c/Gamma.cpp", "src/d/Delta.cpp"),
        ) as (base, ours, theirs):
            text, unresolved = driver.merge(base, ours, theirs)

        self.assertFalse(unresolved)
        for path in ("Alpha.cpp", "Beta.cpp", "Gamma.cpp", "Delta.cpp"):
            self.assertIn(path, text)
        self.assertNotIn("<<<<<<<", text)

    def test_a_path_removed_on_one_side_does_not_come_back(self) -> None:
        with versions(
            block("src/a/Alpha.cpp", "src/b/Beta.cpp"),
            block("src/a/Alpha.cpp"),
            block("src/a/Alpha.cpp", "src/b/Beta.cpp", "src/c/Gamma.cpp"),
        ) as (base, ours, theirs):
            text, unresolved = driver.merge(base, ours, theirs)

        self.assertFalse(unresolved)
        self.assertNotIn("Beta.cpp", text)
        self.assertIn("Gamma.cpp", text)

    def test_a_rename_on_one_side_replaces_the_old_path(self) -> None:
        """The restructure case: tests/X.cpp became src/tests/X.cpp."""
        with versions(
            block("tests/Thing.cpp", "src/a/Alpha.cpp"),
            block("tests/Thing.cpp", "src/a/Alpha.cpp", "src/a/Beta.cpp"),
            block("src/tests/Thing.cpp", "src/a/Alpha.cpp"),
        ) as (base, ours, theirs):
            text, unresolved = driver.merge(base, ours, theirs)

        self.assertFalse(unresolved)
        self.assertIn("src/tests/Thing.cpp", text)
        self.assertNotIn("\n        tests/Thing.cpp", text)
        self.assertIn("src/a/Beta.cpp", text)

    def test_a_sorted_list_stays_sorted(self) -> None:
        with versions(
            block("src/a.cpp", "src/c.cpp"),
            block("src/a.cpp", "src/b.cpp", "src/c.cpp"),
            block("src/a.cpp", "src/c.cpp", "src/d.cpp"),
        ) as (base, ours, theirs):
            text, _ = driver.merge(base, ours, theirs)

        entries = [line.strip() for line in text.splitlines() if line.startswith("        ")]
        self.assertEqual(entries, sorted(entries))

    def test_an_unsorted_list_keeps_its_grouping(self) -> None:
        with versions(
            block("src/z/Late.cpp", "src/a/Early.cpp"),
            block("src/z/Late.cpp", "src/a/Early.cpp", "src/m/New.cpp"),
            block("src/z/Late.cpp", "src/a/Early.cpp", "src/n/Other.cpp"),
        ) as (base, ours, theirs):
            text, _ = driver.merge(base, ours, theirs)

        entries = [line.strip() for line in text.splitlines() if line.startswith("        ")]
        self.assertEqual(entries[:2], ["src/z/Late.cpp", "src/a/Early.cpp"])


class RefusalTests(unittest.TestCase):
    """What the driver must NOT resolve on its own."""

    def test_conflicting_logic_stays_conflicted(self) -> None:
        base = 'set(PRACTICE_TAKES_FEEDBACK_ENDPOINT "https://base")\n'
        ours = 'set(PRACTICE_TAKES_FEEDBACK_ENDPOINT "https://ours")\n'
        theirs = 'set(PRACTICE_TAKES_FEEDBACK_ENDPOINT "https://theirs")\n'

        with versions(base, ours, theirs) as (b, o, t):
            text, unresolved = driver.merge(b, o, t)

        self.assertTrue(unresolved)
        self.assertIn("<<<<<<<", text)
        self.assertIn("https://ours", text)
        self.assertIn("https://theirs", text)

    def test_logic_conflicting_beside_list_entries_stays_conflicted(self) -> None:
        """A region is only unioned when every line in it is a path.

        Both sides change the scope keyword differently, so it lands in the
        same conflict region as the added paths. One non-path line is enough to
        disqualify the whole region.
        """
        base = "target_sources(App\n    PRIVATE\n        src/a.cpp\n)\n"
        ours = "target_sources(App\n    PUBLIC\n        src/a.cpp\n        src/b.cpp\n)\n"
        theirs = "target_sources(App\n    INTERFACE\n        src/a.cpp\n        src/c.cpp\n)\n"

        with versions(base, ours, theirs) as (b, o, t):
            text, unresolved = driver.merge(b, o, t)

        self.assertTrue(unresolved)
        self.assertIn("<<<<<<<", text)

    def test_a_one_sided_keyword_change_still_merges(self) -> None:
        """Only one side touched the keyword, so Git resolves it without help.

        Pinned because it is the boundary of the rule above: the driver is not
        what decides this, and it should not start conflicting on it.
        """
        base = "target_sources(App\n    PRIVATE\n        src/a.cpp\n)\n"
        ours = "target_sources(App\n    PRIVATE\n        src/a.cpp\n        src/b.cpp\n)\n"
        theirs = "target_sources(App\n    PUBLIC\n        src/a.cpp\n        src/c.cpp\n)\n"

        with versions(base, ours, theirs) as (b, o, t):
            text, unresolved = driver.merge(b, o, t)

        self.assertFalse(unresolved)
        self.assertIn("PUBLIC", text)
        self.assertIn("src/b.cpp", text)
        self.assertIn("src/c.cpp", text)

    def test_a_generator_expression_is_not_treated_as_a_path(self) -> None:
        self.assertFalse(
            driver.is_source_list(["        $<$<BOOL:${X}>:src/a.cpp>"])
        )

    def test_a_commented_entry_is_not_treated_as_a_path(self) -> None:
        self.assertFalse(driver.is_source_list(["        src/a.cpp # temporary"]))

    def test_a_variable_entry_is_not_treated_as_a_path(self) -> None:
        self.assertFalse(driver.is_source_list(["        ${EXTRA_SOURCES}"]))

    def test_a_plain_path_is_a_path(self) -> None:
        self.assertTrue(
            driver.is_source_list(
                ["        src/a/Alpha.cpp", "        src/b/Beta.h"], []
            )
        )


class CleanMergeTests(unittest.TestCase):
    def test_a_non_conflicting_merge_passes_straight_through(self) -> None:
        with versions(
            block("src/a.cpp"),
            block("src/a.cpp"),
            block("src/a.cpp", "src/b.cpp"),
        ) as (base, ours, theirs):
            text, unresolved = driver.merge(base, ours, theirs)

        self.assertFalse(unresolved)
        self.assertIn("src/b.cpp", text)
        self.assertNotIn("<<<<<<<", text)

    def test_the_driver_writes_its_result_over_ours(self) -> None:
        with versions(
            block("src/a.cpp", "src/c.cpp"),
            block("src/a.cpp", "src/b.cpp", "src/c.cpp"),
            block("src/a.cpp", "src/c.cpp", "src/d.cpp"),
        ) as (base, ours, theirs):
            code = driver.main([str(base), str(ours), str(theirs), "CMakeLists.txt"])
            result = ours.read_text(encoding="utf-8")

        self.assertEqual(code, 0)
        self.assertIn("src/b.cpp", result)
        self.assertIn("src/d.cpp", result)

    def test_an_unresolved_merge_reports_failure_to_git(self) -> None:
        with versions('set(A "base")\n', 'set(A "ours")\n', 'set(A "theirs")\n') as (
            base,
            ours,
            theirs,
        ):
            code = driver.main([str(base), str(ours), str(theirs), "CMakeLists.txt"])

        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
