#!/usr/bin/env python3
"""Make the repository's milestones match the roadmap ledger.

Each milestone stands for one or more of the ledger's releases. Its due date is
the commitment of the latest of them, so a milestone always carries the date the
project is held to, and nobody has to copy that date across by hand when a
decision recorded in the roadmap issue moves it.

A milestone's title and description are managed only if its ledger entry gives
them. Leave them out and the milestone keeps whatever name and description it
has on GitHub.

The roadmap-forecast workflow runs this when the ledger changes on ``main``, and
it can be run by hand. It only edits milestones the ledger names, and only the
fields that differ, so running it twice changes nothing the second time.

Deciding what to change is a pure function, tested without a network. Only
``main`` talks to GitHub. Standard library only.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys

LEDGER_PATH = Path(__file__).resolve().parent / "roadmap_ledger.json"


@dataclass(frozen=True)
class Milestone:
    number: int
    due: dt.date
    title: str | None = None  # None: leave GitHub's title alone
    description: str | None = None  # None: leave GitHub's description alone


class MilestoneError(ValueError):
    """The ledger's milestones contradict its releases. Fixed by editing the ledger."""


def planned_milestones(raw: dict) -> list[Milestone]:
    """The milestones the ledger asks for, each due on its latest release's commitment."""
    commitments = {r["id"]: dt.date.fromisoformat(r["commitment"]) for r in raw["releases"]}
    out: list[Milestone] = []
    seen_numbers: set[int] = set()
    seen_releases: set[str] = set()
    for entry in raw.get("milestones", []):
        number = int(entry["number"])
        if number in seen_numbers:
            raise MilestoneError(f"milestone {number} appears in the ledger twice")
        seen_numbers.add(number)
        releases = list(entry["releases"])
        if not releases:
            raise MilestoneError(f"milestone {number} names no release")
        for release in releases:
            if release not in commitments:
                raise MilestoneError(f"milestone {number} names unknown release {release!r}")
            if release in seen_releases:
                raise MilestoneError(f"release {release!r} belongs to two milestones")
            seen_releases.add(release)
        out.append(
            Milestone(
                number=number,
                due=max(commitments[r] for r in releases),
                title=str(entry["title"]) if "title" in entry else None,
                description=str(entry["description"]) if "description" in entry else None,
            )
        )
    return out


def due_on(date: dt.date) -> str:
    """GitHub's due_on for a date.

    Midday UTC, not midnight: GitHub shows a due date in the viewer's time zone,
    and midnight UTC reads as the day before anywhere west of Greenwich. Midday
    reads as the same date from UTC-11 to UTC+11.
    """
    return f"{date.isoformat()}T12:00:00Z"


def changes(planned: list[Milestone], current: dict[int, dict]) -> list[tuple[int, dict]]:
    """The PATCH body each milestone needs, holding only the fields that differ.

    ``current`` is the REST listing keyed by milestone number. A milestone the
    ledger names but GitHub doesn't have is an error: creating one silently would
    hide a wrong number in the ledger.
    """
    out: list[tuple[int, dict]] = []
    for milestone in planned:
        existing = current.get(milestone.number)
        if existing is None:
            raise MilestoneError(f"milestone {milestone.number} does not exist on GitHub")
        patch: dict[str, str] = {}
        if milestone.title is not None and existing.get("title") != milestone.title:
            patch["title"] = milestone.title
        if milestone.description is not None and (existing.get("description") or "") != milestone.description:
            patch["description"] = milestone.description
        if (existing.get("due_on") or "")[:10] != milestone.due.isoformat():
            patch["due_on"] = due_on(milestone.due)
        if patch:
            out.append((milestone.number, patch))
    return out


def describe(number: int, patch: dict) -> str:
    fields = ", ".join(f"{key} → {value!r}" for key, value in patch.items())
    return f"milestone {number}: {fields}"


# --------------------------------------------------------------------------
# GitHub, through `gh api`


def _gh(args: list[str], stdin: str | None = None) -> str:
    completed = subprocess.run(
        ["gh", "api", *args], check=True, capture_output=True, text=True, encoding="utf-8", input=stdin
    )
    return completed.stdout


def fetch_milestones(repo: str) -> dict[int, dict]:
    listing = json.loads(_gh([f"repos/{repo}/milestones?state=all&per_page=100"]) or "[]")
    return {int(m["number"]): m for m in listing}


def write_milestone(repo: str, number: int, patch: dict) -> None:
    _gh(["-X", "PATCH", f"repos/{repo}/milestones/{number}", "--input", "-"], stdin=json.dumps(patch))


# --------------------------------------------------------------------------
# Entry point


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"), help="owner/name")
    parser.add_argument(
        "--milestones-json",
        type=Path,
        default=None,
        help="read the current milestones from a saved REST listing instead of GitHub (implies --dry-run)",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the changes instead of making them")
    arguments = parser.parse_args(argv)

    planned = planned_milestones(json.loads(LEDGER_PATH.read_text(encoding="utf-8")))

    if arguments.milestones_json is not None:
        listing = json.loads(arguments.milestones_json.read_text(encoding="utf-8"))
        current = {int(m["number"]): m for m in listing}
        dry_run = True
    else:
        if not arguments.repo:
            print("error: --repo or GITHUB_REPOSITORY is required", file=sys.stderr)
            return 2
        current = fetch_milestones(arguments.repo)
        dry_run = arguments.dry_run

    try:
        pending = changes(planned, current)
    except MilestoneError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if not pending:
        print("Every milestone already matches the ledger")
        return 0
    for number, patch in pending:
        print(("would update " if dry_run else "updating ") + describe(number, patch))
        if not dry_run:
            write_milestone(arguments.repo, number, patch)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
