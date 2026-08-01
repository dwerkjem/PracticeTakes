#!/usr/bin/env python3
"""Tests for the release gate on manual verification.

The case that motivates the whole design is
``test_a_record_committed_after_the_run_is_accepted``: a record can never name
the commit that contains it, so a naive commit-equality check would block every
release.
"""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_manual_verification as gate  # noqa: E402


def run_git(repository: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments],
        cwd=repository,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()


class GateTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.repository = Path(self._temporary.name)
        self.addCleanup(self._temporary.cleanup)

        run_git(self.repository, "init", "-q")
        run_git(self.repository, "config", "user.email", "test@example.com")
        run_git(self.repository, "config", "user.name", "Test")

        (self.repository / "src").mkdir()
        (self.repository / "src" / "Thing.cpp").write_text("// one\n", encoding="utf-8")
        (self.repository / "docs").mkdir()
        run_git(self.repository, "add", "-A")
        run_git(self.repository, "commit", "-q", "-m", "initial")

        self.records = self.repository / "records"
        self.records.mkdir()

        # The gate shells out to git against its own repository root.
        self._original_root = gate.REPOSITORY_ROOT
        gate.REPOSITORY_ROOT = self.repository
        self.addCleanup(lambda: setattr(gate, "REPOSITORY_ROOT", self._original_root))

    def head(self) -> str:
        return run_git(self.repository, "rev-parse", "HEAD")

    def commit(self, path: str, contents: str) -> str:
        target = self.repository / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents, encoding="utf-8")
        run_git(self.repository, "add", "-A")
        run_git(self.repository, "commit", "-q", "-m", f"change {path}")

        return self.head()

    def write_record(self, **overrides: object) -> Path:
        record = {
            "commit": self.head(),
            "mode": "full",
            "complete": True,
            "started_at": "2026-08-01T10:00:00",
            "answers": [],
            "waivers": [],
            "unreachable": [],
        }
        record.update(overrides)

        path = self.records / f"{record['started_at']}-{record['mode']}.json"
        path.write_text(json.dumps(record), encoding="utf-8")

        return path

    def check(self):
        return gate.check(gate.load_records(self.records), self.head())

    # --- Accepting -----------------------------------------------------------

    def test_a_record_for_the_exact_commit_is_accepted(self) -> None:
        self.write_record()

        self.assertTrue(self.check())

    def test_a_record_committed_after_the_run_is_accepted(self) -> None:
        """The case the whole design exists for.

        The run verifies commit A; its record is committed as B. A record can
        never name the commit containing it, so equality would fail always.
        Committing a record changes no release-affecting file, so it stays
        current.
        """
        verified = self.head()
        self.write_record(commit=verified)
        self.commit("docs/manual-runs/whatever.md", "a record\n")

        self.assertTrue(self.check())

    def test_documentation_changes_do_not_invalidate_a_run(self) -> None:
        self.write_record()
        self.commit("docs/NOTES.md", "notes\n")

        self.assertTrue(self.check())

    def test_test_changes_do_not_invalidate_a_run(self) -> None:
        """tests/ does not affect the shipped binary."""
        self.write_record()
        self.commit("tests/ThingTests.cpp", "// tests\n")

        self.assertTrue(self.check())

    def test_a_waived_failure_is_allowed_through(self) -> None:
        self.write_record(
            answers=[
                {
                    "surface": "The tuner, docked",
                    "question": "looks-good",
                    "prompt": "Does it look well-presented?",
                    "verdict": "fail",
                    "note": "cramped",
                }
            ],
            waivers=[
                {
                    "surface": "The tuner, docked",
                    "question": "looks-good",
                    "reason": "cosmetic, tracked in #113",
                }
            ],
        )

        self.assertTrue(self.check())

    # --- Rejecting -----------------------------------------------------------

    def test_no_records_at_all_blocks(self) -> None:
        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("No manual verification records", str(caught.exception))

    def test_only_a_quick_run_blocks(self) -> None:
        """A quick run is not a release check."""
        self.write_record(mode="quick")

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("quick", str(caught.exception))

    def test_an_incomplete_run_blocks(self) -> None:
        self.write_record(complete=False)

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("incomplete", str(caught.exception))

    def test_a_source_change_since_the_run_blocks(self) -> None:
        self.write_record()
        self.commit("src/Thing.cpp", "// two\n")

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("src/Thing.cpp", str(caught.exception))

    def test_a_cmake_change_since_the_run_blocks(self) -> None:
        self.write_record()
        self.commit("CMakeLists.txt", "project(Whatever)\n")

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("CMakeLists.txt", str(caught.exception))

    def test_a_packaging_change_since_the_run_blocks(self) -> None:
        self.write_record()
        self.commit("packaging/control", "Package: x\n")

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("packaging/", str(caught.exception))

    def test_a_run_from_another_branch_blocks(self) -> None:
        """It verified a different line of development."""
        self.write_record(commit="0" * 40)

        with self.assertRaises(gate.GateFailure):
            self.check()

    def test_a_record_naming_no_commit_blocks(self) -> None:
        self.write_record(commit="unknown")

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("does not name the commit", str(caught.exception))

    def test_an_unwaived_failure_blocks_and_is_named(self) -> None:
        self.write_record(
            answers=[
                {
                    "surface": "The tuner, docked",
                    "question": "works",
                    "prompt": "Does it work?",
                    "verdict": "fail",
                    "note": "meter frozen",
                }
            ]
        )

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("meter frozen", str(caught.exception))

    def test_a_waiver_without_a_reason_does_not_count(self) -> None:
        """Otherwise a waiver is just a way to silence the gate."""
        self.write_record(
            answers=[
                {
                    "surface": "S",
                    "question": "works",
                    "prompt": "Does it work?",
                    "verdict": "fail",
                    "note": "broken",
                }
            ],
            waivers=[{"surface": "S", "question": "works", "reason": "   "}],
        )

        with self.assertRaises(gate.GateFailure):
            self.check()

    def test_the_newest_full_run_is_the_one_that_counts(self) -> None:
        old = self.head()
        self.write_record(started_at="2026-07-01T10:00:00", commit=old)
        self.write_record(started_at="2026-08-01T10:00:00", complete=False)

        with self.assertRaises(gate.GateFailure) as caught:
            self.check()

        self.assertIn("incomplete", str(caught.exception))

    def test_an_unreadable_record_is_ignored_rather_than_crashing(self) -> None:
        (self.records / "broken.json").write_text("{not json", encoding="utf-8")
        self.write_record()

        self.assertTrue(self.check())


class SkipTests(unittest.TestCase):
    def test_skipping_without_a_reason_fails(self) -> None:
        self.assertEqual(gate.main(["--skip"]), 1)

    def test_skipping_with_a_reason_succeeds(self) -> None:
        self.assertEqual(gate.main(["--skip", "--skip-reason", "docs only"]), 0)

    def test_a_whitespace_reason_is_no_reason(self) -> None:
        self.assertEqual(gate.main(["--skip", "--skip-reason", "   "]), 1)


class ReleaseAffectingPathTests(unittest.TestCase):
    def test_version_is_deliberately_not_release_affecting(self) -> None:
        """The dispatch path bumps VERSION as part of releasing.

        Including it would make the gate unsatisfiable for the exact case it
        exists to guard.
        """
        self.assertNotIn("VERSION", gate.RELEASE_AFFECTING_PATHS)

    def test_the_binary_inputs_are_all_listed(self) -> None:
        for path in ("src/", "CMakeLists.txt", "packaging/"):
            self.assertIn(path, gate.RELEASE_AFFECTING_PATHS)


if __name__ == "__main__":
    unittest.main()
