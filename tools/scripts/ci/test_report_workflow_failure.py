#!/usr/bin/env python3
"""Tests for the unattended-failure reporter.

Two failure modes matter here, and neither is visible from a green run.

If the duplicate check breaks, a workflow that fails every week opens an issue
every week, the queue fills with the same report, and the reader learns to
ignore all of them -- which is the exact outcome the reporter exists to prevent,
arrived at from the other direction.

If the body composition breaks, the issue arrives without the run URL or the
failing step, and a reader who cannot get from the issue to the failure in one
click is back to opening the Actions tab.
"""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from report_workflow_failure import (  # noqa: E402
    compose_body,
    find_open_issue,
    parse_arguments,
)

TITLE = "ThreadSanitizer: scheduled run is failing"


class ComposeBodyTests(unittest.TestCase):
    def body(self, **overrides: object) -> str:
        arguments = {
            "workflow": "Sanitizers (scheduled)",
            "run_url": "https://github.com/o/r/actions/runs/1",
            "commit": "abc123",
            "trigger": "schedule",
            "failing_step": "Run the concurrency suite",
        }
        arguments.update(overrides)
        return compose_body(**arguments)  # type: ignore[arg-type]

    def test_the_body_carries_everything_needed_to_reach_the_failure(self) -> None:
        """A reader must get from the issue to the run without a search."""
        body = self.body()

        self.assertIn("https://github.com/o/r/actions/runs/1", body)
        self.assertIn("abc123", body)
        self.assertIn("schedule", body)
        self.assertIn("Run the concurrency suite", body)
        self.assertIn("Sanitizers (scheduled)", body)

    def test_no_line_is_indented(self) -> None:
        """GitHub renders indented markdown as a code block.

        The workflow this replaced wrote its body with printf into a file for
        exactly this reason: an indented heredoc keeps its leading spaces and
        the whole issue arrives as one grey block.
        """
        for line in self.body().splitlines():
            self.assertFalse(line.startswith(" "), f"indented line: {line!r}")
            self.assertFalse(line.startswith("\t"), f"indented line: {line!r}")

    def test_guidance_is_included_when_given(self) -> None:
        body = self.body(guidance="Treat a race in `AudioSampleFifo` as urgent.")

        self.assertIn("Treat a race in `AudioSampleFifo` as urgent.", body)

    def test_indented_guidance_is_flattened(self) -> None:
        """A caller inside an indented YAML `run:` block passes its indentation on.

        Left alone, GitHub renders those lines as a code block and the advice
        arrives as a grey slab. This is the one way the no-indentation rule can
        be broken from outside this module.
        """
        body = self.body(
            guidance="These numbers are recorded, not gated.\n"
            "          So a failure here is usually the build.\n"
        )

        self.assertIn(
            "These numbers are recorded, not gated. So a failure here is usually "
            "the build.",
            body,
        )
        for line in body.splitlines():
            self.assertFalse(line.startswith(" "), f"indented line: {line!r}")

    def test_whitespace_only_guidance_adds_nothing(self) -> None:
        body = self.body(guidance="   \n  \n")

        self.assertNotIn("\n\n\n", body)

    def test_guidance_is_omitted_when_absent(self) -> None:
        body = self.body(guidance=None)

        self.assertNotIn("None", body)
        # No blank-line run left behind where the paragraph would have been.
        self.assertNotIn("\n\n\n", body)

    def test_the_body_says_it_will_be_reused(self) -> None:
        """The reader should know a second failure comments rather than files."""
        self.assertIn("reused", self.body())


class FindOpenIssueTests(unittest.TestCase):
    def test_an_exact_title_match_is_found(self) -> None:
        issues = [{"number": 7, "title": "something else"}, {"number": 12, "title": TITLE}]

        self.assertEqual(find_open_issue(issues, TITLE), 12)

    def test_no_match_returns_none(self) -> None:
        issues = [{"number": 7, "title": "something else"}]

        self.assertIsNone(find_open_issue(issues, TITLE))

    def test_an_empty_queue_returns_none(self) -> None:
        self.assertIsNone(find_open_issue([], TITLE))

    def test_a_title_that_merely_mentions_the_workflow_is_not_a_match(self) -> None:
        """Substring matching would hijack an unrelated issue.

        Someone writing "ThreadSanitizer: scheduled run is failing on Windows
        only" means a different problem, and commenting the nightly report onto
        it would bury both.
        """
        issues = [{"number": 9, "title": TITLE + " on Windows only"}]

        self.assertIsNone(find_open_issue(issues, TITLE))

    def test_the_first_match_wins_when_duplicates_already_exist(self) -> None:
        """Duplicates can predate this script; it must still pick one and stop."""
        issues = [{"number": 3, "title": TITLE}, {"number": 8, "title": TITLE}]

        self.assertEqual(find_open_issue(issues, TITLE), 3)


class ArgumentTests(unittest.TestCase):
    REQUIRED = [
        "--title",
        TITLE,
        "--workflow",
        "Secret scan",
        "--run-url",
        "https://example.invalid/run",
        "--commit",
        "abc123",
        "--trigger",
        "schedule",
        "--failing-step",
        "Run gitleaks",
    ]

    def test_labels_accumulate(self) -> None:
        arguments = parse_arguments(
            self.REQUIRED + ["--label", "area:build", "--label", "priority:p1"]
        )

        self.assertEqual(arguments.labels, ["area:build", "priority:p1"])

    def test_labels_default_to_empty(self) -> None:
        self.assertEqual(parse_arguments(self.REQUIRED).labels, [])

    def test_guidance_defaults_to_none(self) -> None:
        self.assertIsNone(parse_arguments(self.REQUIRED).guidance)


if __name__ == "__main__":
    unittest.main()
