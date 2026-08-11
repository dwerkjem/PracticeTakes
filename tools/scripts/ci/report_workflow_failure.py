#!/usr/bin/env python3
"""Report a failed unattended workflow run into the issue queue.

A scheduled workflow that fails reports into the Actions tab and nowhere else,
and nobody opens the Actions tab on a repository that looks healthy. A weekly
secret scan that has been red for a month is worse than no secret scan, because
the repository looks guarded while it is not. That is the whole reason this
exists: the difference between a check that *runs* and a check that is *read*.

One issue per outstanding failure. A later failure of the same workflow comments
on the open issue rather than opening another, so a month of weekly failures is
one thread instead of four issues nobody triages.

Only the unattended triggers report. A person who clicked "Run workflow", or who
is watching their own pull request go red, does not need an issue opened on their
behalf -- the failure is already in front of them. The workflow decides that with
its ``if:`` condition; this script does not second-guess it.

Composing the body and choosing between commenting and creating are pure
functions, so they are tested without a network or a ``gh`` binary. Everything
that talks to GitHub is confined to ``main``.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys

# GitHub's own cap on `gh issue list --limit`. Reading more than this to find a
# title match would mean the queue has bigger problems than a red nightly.
OPEN_ISSUE_SEARCH_LIMIT = 100


def compose_body(
    *,
    workflow: str,
    run_url: str,
    commit: str,
    trigger: str,
    failing_step: str,
    guidance: str | None = None,
) -> str:
    """Build the issue body.

    Written as one string rather than an indented heredoc on purpose: GitHub
    renders indented markdown as a code block, and a heredoc inside a workflow
    step keeps its leading spaces.
    """
    lines = [
        f"Workflow `{workflow}` failed at **{failing_step}**.",
        "",
        f"- Run: {run_url}",
        f"- Commit: `{commit}`",
        f"- Trigger: `{trigger}`",
        "",
    ]

    if guidance:
        # Every line stripped, not just the ends. A caller writing guidance
        # inside an indented YAML `run:` block passes the indentation along with
        # the words, and GitHub renders an indented line as a code block -- so
        # the advice would arrive as a grey slab. Rewrapping is the reader's
        # problem to never have.
        paragraph = " ".join(
            part for part in (line.strip() for line in guidance.splitlines()) if part
        )
        if paragraph:
            lines.extend([paragraph, ""])

    lines.append("This issue is reused for later failures rather than duplicated.")
    return "\n".join(lines) + "\n"


def find_open_issue(issues: list[dict], title: str) -> int | None:
    """Return the number of an open issue with exactly this title, if any.

    Matched on the exact title rather than a search query, so an unrelated issue
    that merely mentions the workflow is never mistaken for this report.
    """
    for issue in issues:
        if issue.get("title") == title:
            number = issue.get("number")
            return int(number) if number is not None else None
    return None


def _run(command: list[str]) -> str:
    completed = subprocess.run(
        command, check=True, capture_output=True, text=True, encoding="utf-8"
    )
    return completed.stdout


def list_open_issues() -> list[dict]:
    output = _run(
        [
            "gh",
            "issue",
            "list",
            "--state",
            "open",
            "--limit",
            str(OPEN_ISSUE_SEARCH_LIMIT),
            "--json",
            "number,title",
        ]
    )
    return json.loads(output or "[]")


def comment_on_issue(number: int, body: str) -> None:
    _run(["gh", "issue", "comment", str(number), "--body", body])


def create_issue(title: str, body: str, labels: list[str]) -> None:
    command = ["gh", "issue", "create", "--title", title, "--body", body]
    if labels:
        command.extend(["--label", ",".join(labels)])
    _run(command)


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--title",
        required=True,
        help="Issue title. Reused verbatim to find an already-open report, so it "
        "must be stable across runs of the same workflow.",
    )
    parser.add_argument("--workflow", required=True)
    parser.add_argument("--run-url", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--trigger", required=True)
    parser.add_argument(
        "--failing-step",
        required=True,
        help="Which step or job failed, in words a reader recognises.",
    )
    parser.add_argument(
        "--label",
        action="append",
        default=[],
        dest="labels",
        help="Repeatable. Applied only when the issue is created.",
    )
    parser.add_argument(
        "--guidance",
        default=None,
        help="Optional markdown paragraph telling the reader what this failure "
        "usually means and how urgent it is.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)

    body = compose_body(
        workflow=arguments.workflow,
        run_url=arguments.run_url,
        commit=arguments.commit,
        trigger=arguments.trigger,
        failing_step=arguments.failing_step,
        guidance=arguments.guidance,
    )

    try:
        existing = find_open_issue(list_open_issues(), arguments.title)
        if existing is not None:
            comment_on_issue(existing, body)
            print(f"Commented on existing issue #{existing}")
        else:
            create_issue(arguments.title, body, arguments.labels)
            print(f"Opened an issue: {arguments.title}")
    except subprocess.CalledProcessError as error:
        # The workflow has already failed by the time this runs. Failing the
        # reporting step on top of that would replace a legible failure with a
        # confusing one, so say what went wrong and let the original stand.
        print(f"Could not file the failure report: {error.stderr}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
