#!/usr/bin/env python3
"""Tests for the path-filter decision.

The way this goes wrong silently is a check deciding it does not apply to a
change it should have checked: the job is skipped, a skip counts as passing, and
a broken change merges with every required check green. So most of these pin
that a file inside a check's scope is included, against the exact lists the
workflows carry.
"""

from __future__ import annotations

import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import changed_paths  # noqa: E402
from changed_paths import applies, is_included, parse_patterns  # noqa: E402

# The C++ quality check's list, as the workflow carries it.
CPP_QUALITY = parse_patterns(
    """
    src/**
    !src/services/**
    .clang-format
    CMakeLists.txt
    tools/cmake/**
    tools/scripts/quality/run_clang_tidy.py
    """
)


class Matching(unittest.TestCase):
    def test_double_star_reaches_any_depth(self) -> None:
        self.assertTrue(is_included("src/platform/midi/MidiInputService.cpp", ["src/**"]))
        self.assertTrue(is_included("src/main.cpp", ["src/**"]))

    def test_single_star_stays_in_its_directory(self) -> None:
        self.assertTrue(is_included("docs/a.md", ["docs/*.md"]))
        self.assertFalse(is_included("docs/sub/a.md", ["docs/*.md"]))

    def test_an_exact_name_matches_only_itself(self) -> None:
        self.assertTrue(is_included("CMakeLists.txt", ["CMakeLists.txt"]))
        self.assertFalse(is_included("src/CMakeLists.txt", ["CMakeLists.txt"]))
        self.assertFalse(is_included("CMakeLists.txt.bak", ["CMakeLists.txt"]))

    def test_dots_are_literal(self) -> None:
        self.assertTrue(is_included(".clang-format", [".clang-format"]))
        self.assertFalse(is_included("xclang-format", [".clang-format"]))

    def test_leading_double_star_slash_matches_the_root_too(self) -> None:
        self.assertTrue(is_included("a.py", ["**/*.py"]))
        self.assertTrue(is_included("x/y/a.py", ["**/*.py"]))


class Exclusion(unittest.TestCase):
    def test_a_later_exclusion_removes_what_an_earlier_pattern_added(self) -> None:
        self.assertFalse(is_included("src/services/feedback-intake/index.ts", CPP_QUALITY))
        self.assertTrue(is_included("src/platform/score/Score.cpp", CPP_QUALITY))

    def test_the_last_matching_pattern_decides(self) -> None:
        patterns = ["src/**", "!src/services/**", "src/services/keep.cpp"]

        self.assertTrue(is_included("src/services/keep.cpp", patterns))
        self.assertFalse(is_included("src/services/other.ts", patterns))

    def test_an_exclusion_alone_includes_nothing(self) -> None:
        self.assertFalse(is_included("src/a.cpp", ["!src/services/**"]))


class Applies(unittest.TestCase):
    def test_one_file_in_scope_is_enough(self) -> None:
        self.assertTrue(applies(["docs/README.md", "src/a.cpp"], CPP_QUALITY))

    def test_a_change_entirely_outside_scope_does_not_apply(self) -> None:
        self.assertFalse(applies(["docs/README.md", "openspec/x/tasks.md"], CPP_QUALITY))

    def test_no_changed_files_does_not_apply(self) -> None:
        self.assertFalse(applies([], CPP_QUALITY))


class Parsing(unittest.TestCase):
    def test_blank_lines_comments_and_quotes_are_dropped(self) -> None:
        text = """
            # why this list is what it is
            "src/**"

            '!src/services/**'
        """

        self.assertEqual(parse_patterns(text), ["src/**", "!src/services/**"])


class CommandLine(unittest.TestCase):
    def run_main(self, patterns: str, changed: str) -> tuple[int, str]:
        output = io.StringIO()
        stdin = sys.stdin

        try:
            sys.stdin = io.StringIO(changed)

            with redirect_stdout(output):
                code = changed_paths.main(["--patterns", patterns, "-"])
        finally:
            sys.stdin = stdin

        return code, output.getvalue().strip()

    def test_prints_a_github_output_line(self) -> None:
        self.assertEqual(self.run_main("src/**", "src/a.cpp\n"), (0, "relevant=true"))
        self.assertEqual(self.run_main("src/**", "docs/a.md\n"), (0, "relevant=false"))

    def test_an_empty_pattern_list_is_an_error_not_a_skip(self) -> None:
        code, output = self.run_main("\n# nothing\n", "src/a.cpp\n")

        self.assertEqual(code, 2)
        self.assertEqual(output, "")


if __name__ == "__main__":
    unittest.main()
