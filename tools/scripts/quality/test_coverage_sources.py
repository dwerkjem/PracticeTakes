#!/usr/bin/env python3
"""Tests for the coverage denominator derivation.

A bug here would silently shrink the denominator and inflate the headline
coverage figure, which is the one failure mode this whole report exists to
prevent.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from coverage_sources import (  # noqa: E402
    classify,
    find_compiled_sources,
    read_test_target_sources,
    summarise,
)

CMAKE_TEMPLATE = """\
add_executable(PracticeTakesTests
{entries}
    )

    target_link_libraries(PracticeTakesTests
        PRIVATE
            Catch2::Catch2WithMain
    )
"""


class CoverageSourcesTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary.name)
        self.source = self.root / "src"
        self.source.mkdir()

        self.addCleanup(self._temporary.cleanup)

    def write_source(self, relative: str) -> None:
        path = self.source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("// source\n", encoding="utf-8")

    def write_cmake(self, entries: list[str]) -> Path:
        path = self.root / "CMakeLists.txt"
        body = "\n".join(f"        {entry}" for entry in entries)
        path.write_text(CMAKE_TEMPLATE.format(entries=body), encoding="utf-8")

        return path

    def test_only_compiled_units_count(self) -> None:
        """Headers are pulled in by whatever includes them, not listed as units."""
        self.write_source("a/Thing.cpp")
        self.write_source("a/Thing.h")

        self.assertEqual(find_compiled_sources(self.source), {"src/a/Thing.cpp"})

    def test_test_target_sources_are_read(self) -> None:
        cmake = self.write_cmake(
            ["src/tests/a/ThingTests.cpp", "src/a/Thing.cpp", "src/b/Other.cpp"]
        )

        self.assertEqual(
            read_test_target_sources(cmake), {"src/a/Thing.cpp", "src/b/Other.cpp"}
        )

    def test_a_missing_target_block_raises(self) -> None:
        """An empty result would report every file as untested.

        That looks like a catastrophic finding rather than a parsing failure,
        so it must be loud.
        """
        path = self.root / "CMakeLists.txt"
        path.write_text("project(Whatever)\n", encoding="utf-8")

        with self.assertRaises(ValueError):
            read_test_target_sources(path)

    def test_files_outside_the_test_build_are_identified(self) -> None:
        self.write_source("a/Tested.cpp")
        self.write_source("b/Untested.cpp")
        cmake = self.write_cmake(["src/a/Tested.cpp"])

        classified = classify(self.source, cmake)

        self.assertEqual(classified["in_test_build"], ["src/a/Tested.cpp"])
        self.assertEqual(classified["not_in_test_build"], ["src/b/Untested.cpp"])

    def test_a_fully_covered_target_leaves_nothing_outside(self) -> None:
        self.write_source("a/One.cpp")
        self.write_source("a/Two.cpp")
        cmake = self.write_cmake(["src/a/One.cpp", "src/a/Two.cpp"])

        classified = classify(self.source, cmake)

        self.assertEqual(classified["not_in_test_build"], [])

    def test_a_listed_file_that_does_not_exist_is_surfaced(self) -> None:
        """The build would fail, but a reporting run would otherwise not notice."""
        self.write_source("a/Real.cpp")
        cmake = self.write_cmake(["src/a/Real.cpp", "src/a/Ghost.cpp"])

        classified = classify(self.source, cmake)

        self.assertEqual(classified["listed_but_absent"], ["src/a/Ghost.cpp"])

    def test_headers_listed_in_the_target_do_not_count_as_units(self) -> None:
        """The app target lists headers; they must not inflate the numerator."""
        self.write_source("a/Thing.cpp")
        self.write_source("a/Thing.h")
        cmake = self.write_cmake(["src/a/Thing.cpp", "src/a/Thing.h"])

        classified = classify(self.source, cmake)

        self.assertEqual(classified["in_test_build"], ["src/a/Thing.cpp"])
        self.assertEqual(classified["listed_but_absent"], [])

    def test_the_summary_names_the_files_outside_the_build(self) -> None:
        """A count alone is not actionable; the point is the list."""
        self.write_source("a/Untested.cpp")
        cmake = self.write_cmake([])

        summary = summarise(classify(self.source, cmake))

        self.assertIn("NOT in the test build", summary)
        self.assertIn("src/a/Untested.cpp", summary)

    def test_the_summary_handles_an_empty_tree(self) -> None:
        cmake = self.write_cmake([])

        self.assertIn("No compiled sources", summarise(classify(self.source, cmake)))

    def test_the_real_repository_reports_the_known_untested_files(self) -> None:
        """QA_STRATEGY area 9 names these as the largest untested units.

        If any of them stops appearing, either it gained tests — good, update
        this list — or the derivation broke and the denominator just shrank.

        `AudioInputService.cpp` left this list on 2026-08-10: the real-time
        verification work needed a test that drives the audio callback, so the
        file entered the test target. Only the callback is covered; the rest of
        it is still #116's work.
        """
        repository_root = Path(__file__).resolve().parents[3]
        classified = classify(
            repository_root / "src", repository_root / "CMakeLists.txt"
        )
        outside = set(classified["not_in_test_build"])

        for expected in (
            "src/features/feedback/FeedbackComponent.cpp",
            "src/features/analysis/tuner/TunerComponent.cpp",
            "src/features/analysis/spectrogram/SpectrogramComponent.cpp",
        ):
            self.assertIn(expected, outside)

    def test_the_real_repository_has_files_on_both_sides(self) -> None:
        """Guards against a parse failure quietly classifying everything one way."""
        repository_root = Path(__file__).resolve().parents[3]
        classified = classify(
            repository_root / "src", repository_root / "CMakeLists.txt"
        )

        self.assertTrue(classified["in_test_build"])
        self.assertTrue(classified["not_in_test_build"])
        self.assertEqual(classified["listed_but_absent"], [])


if __name__ == "__main__":
    unittest.main()
