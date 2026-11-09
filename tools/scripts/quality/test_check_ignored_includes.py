#!/usr/bin/env python3
"""Tests for the check that no quoted include in src/ names an ignored file."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_ignored_includes import candidates, find_problems  # noqa: E402


class CandidatesTests(unittest.TestCase):
    def test_the_including_directory_comes_before_the_include_roots(self) -> None:
        self.assertEqual(
            candidates("src/features/score/View.cpp", "Layout.h", ("src", "src/tests")),
            ["src/features/score/Layout.h", "src/Layout.h", "src/tests/Layout.h"],
        )

    def test_parent_segments_are_collapsed(self) -> None:
        self.assertEqual(
            candidates("src/a/b/File.cpp", "../c/Other.h", ("src",)),
            ["src/a/c/Other.h", "c/Other.h"],
        )

    def test_a_path_that_climbs_out_of_the_repository_is_dropped(self) -> None:
        self.assertEqual(candidates("src/File.cpp", "../../Outside.h", ("src",)), [])

    def test_an_absolute_include_has_no_candidates(self) -> None:
        self.assertEqual(candidates("src/File.cpp", "/usr/include/stdio.h", ("src",)), [])


@unittest.skipIf(shutil.which("git") is None, "git is not installed")
class FindProblemsTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.root = Path(self._temporary.name)

        subprocess.run(["git", "init", "--quiet", str(self.root)], check=True)
        self.write(".gitignore", "target/\n/var/\n")

    def write(self, relative: str, text: str) -> None:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def problems(self) -> list[str]:
        return [
            f"{problem.source}:{problem.line} "
            f"{' '.join(path for path, _ in problem.ignored)} present={problem.present}"
            for problem in find_problems(self.root, "src", ("src", "src/tests"))
        ]

    def test_a_tree_with_no_ignored_includes_has_no_problems(self) -> None:
        self.write("src/state/Clock.h", "#pragma once\n")
        self.write("src/state/Clock.cpp", '#include "Clock.h"\n#include "state/Clock.h"\n')

        self.assertEqual(self.problems(), [])

    def test_an_ignored_header_present_on_disk_is_reported(self) -> None:
        """What the author's machine sees: the file exists, so it compiles there."""
        self.write("src/state/target/Follower.h", "#pragma once\n")
        self.write("src/state/Clock.cpp", '// Clock\n#include "state/target/Follower.h"\n')

        self.assertEqual(self.problems(), ["src/state/Clock.cpp:2 src/state/target/Follower.h present=True"])

    def test_a_missing_header_under_an_ignored_directory_is_reported(self) -> None:
        """What a fresh checkout sees: the header never made it into the repository."""
        self.write("src/state/Clock.cpp", '#include "state/target/Follower.h"\n')

        problems = find_problems(self.root, "src", ("src", "src/tests"))

        self.assertEqual(len(problems), 1)
        self.assertFalse(problems[0].present)
        self.assertEqual(problems[0].line, 1)
        # Every candidate is listed, because a missing header gives no way to
        # tell which one the author had; the one under src/ is among them.
        self.assertIn("src/state/target/Follower.h", [path for path, _ in problems[0].ignored])

    def test_a_missing_header_nothing_would_ignore_is_not_reported(self) -> None:
        """Generated headers such as JuceHeader.h live in the build tree, not here."""
        self.write("src/Main.cpp", '#include "JuceHeader.h"\n')

        self.assertEqual(self.problems(), [])

    def test_a_root_anchored_rule_does_not_reach_into_src(self) -> None:
        self.write("src/var/Value.h", "#pragma once\n")
        self.write("src/Main.cpp", '#include "var/Value.h"\n')

        self.assertEqual(self.problems(), [])

    def test_a_tracked_header_under_an_ignored_directory_is_not_reported(self) -> None:
        """A force-added header is committed, which is all the check cares about."""
        self.write("src/state/target/Follower.h", "#pragma once\n")
        self.write("src/state/Clock.cpp", '#include "state/target/Follower.h"\n')
        subprocess.run(
            ["git", "-C", str(self.root), "add", "--force", "src/state/target/Follower.h"],
            check=True,
        )

        self.assertEqual(self.problems(), [])

    def test_the_first_header_found_is_the_one_checked(self) -> None:
        """A tracked header beside the includer shadows an ignored one under a root."""
        self.write("src/target/Shared.h", "#pragma once\n")
        self.write("src/feature/target/Shared.h", "#pragma once\n")
        self.write("src/feature/View.cpp", '#include "target/Shared.h"\n')
        subprocess.run(
            ["git", "-C", str(self.root), "add", "--force", "src/feature/target/Shared.h"],
            check=True,
        )

        self.assertEqual(self.problems(), [])

    def test_a_negated_rule_keeps_a_header_visible(self) -> None:
        self.write(".gitignore", "target/\n!src/state/target/\n")
        self.write("src/state/target/Follower.h", "#pragma once\n")
        self.write("src/state/Clock.cpp", '#include "state/target/Follower.h"\n')

        self.assertEqual(self.problems(), [])

    def test_angle_bracket_includes_are_not_checked(self) -> None:
        self.write("src/state/target/Follower.h", "#pragma once\n")
        self.write("src/state/Clock.cpp", "#include <state/target/Follower.h>\n")

        self.assertEqual(self.problems(), [])

    def test_headers_under_node_modules_are_not_scanned(self) -> None:
        self.write(".gitignore", "target/\n")
        self.write("src/services/node_modules/pkg/a.h", '#include "target/Missing.h"\n')

        self.assertEqual(self.problems(), [])


if __name__ == "__main__":
    unittest.main()
