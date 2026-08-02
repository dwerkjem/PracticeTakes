#!/usr/bin/env python3
"""Refuse to release without a current, complete, full-mode manual run.

Run at the front of the release workflow, before any artifact is built. This is
what makes the manual GUI harness load-bearing rather than optional.

The interesting part is what "current" means. A run verifies the application
built from some commit, and the record of that run is committed *afterwards* —
so a record can never name the commit that contains it, and a
``record.commit == HEAD`` check would fail every single time. Instead a record
is current when:

1. the commit it verified is an ancestor of (or identical to) the commit being
   released — the run happened on this line of development; and
2. no *release-affecting* file differs between those two commits.

The second condition is the real check, and it is what makes committing the
record harmless: a commit that only adds a run record changes nothing that ends
up in the binary.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]

# Paths that change what a user receives. An explicit list rather than an
# inference, so a gap in it is a reviewable mistake rather than silent
# under-enforcement, and so adding a new release-affecting directory is a
# deliberate act.
#
# VERSION is deliberately absent: the dispatch path bumps it as part of
# releasing, so including it would make this gate unsatisfiable for exactly the
# case it exists to guard.
RELEASE_AFFECTING_PATHS = (
    "src/",
    "CMakeLists.txt",
    "tools/cmake/",
    "tools/packaging/",
    "tools/vcpkg.json",
)

# src/tests/ sits inside src/ but ships nothing, so a test-only change must not
# invalidate a manual verification run.
RELEASE_EXEMPT_PATHS = ("src/tests/",)

RECORDS_DIRECTORY = Path("docs/development/manual-runs")

FULL_MODE = "full"


class GateFailure(RuntimeError):
    """The release must not proceed. The message says which condition failed."""


def git(*arguments: str) -> str:
    completed = subprocess.run(
        ["git", *arguments],
        cwd=REPOSITORY_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )

    return completed.stdout.strip()


def load_records(directory: Path) -> list[dict]:
    """Every parseable run record, newest first by its recorded start time."""
    records: list[dict] = []

    for path in sorted(directory.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue

        data["_path"] = str(path)
        records.append(data)

    return sorted(records, key=lambda entry: entry.get("started_at", ""), reverse=True)


def is_ancestor(candidate: str, descendant: str) -> bool:
    completed = subprocess.run(
        ["git", "merge-base", "--is-ancestor", candidate, descendant],
        cwd=REPOSITORY_ROOT,
        capture_output=True,
    )

    return completed.returncode == 0


def release_affecting_changes(verified: str, release: str) -> list[str]:
    """Release-affecting files that differ between the two commits."""
    if verified == release:
        return []

    changed = git("diff", "--name-only", f"{verified}..{release}").splitlines()

    return sorted(
        path
        for path in changed
        if any(
            (path == affecting or path.startswith(affecting))
            and not any(path.startswith(exempt) for exempt in RELEASE_EXEMPT_PATHS)
            for affecting in RELEASE_AFFECTING_PATHS
        )
    )


def check(records: list[dict], release_commit: str) -> str:
    """Return the accepted record's path, or raise GateFailure with the reason."""
    if not records:
        raise GateFailure(
            f"No manual verification records found in {RECORDS_DIRECTORY}. "
            f"Run `uv run ui-test --full` before releasing."
        )

    full_runs = [entry for entry in records if entry.get("mode") == FULL_MODE]

    if not full_runs:
        raise GateFailure(
            "No full-mode manual run found; only quick runs. A quick run is not "
            "a release check — run `uv run ui-test --full`."
        )

    newest = full_runs[0]

    if not newest.get("complete"):
        raise GateFailure(
            f"The most recent full run ({newest['_path']}) is marked incomplete, "
            f"so the remaining surfaces were never verified."
        )

    verified = newest.get("commit", "")

    if not verified or verified == "unknown":
        raise GateFailure(
            f"{newest['_path']} does not name the commit it verified, so it "
            f"cannot be matched against this release."
        )

    if not is_ancestor(verified, release_commit):
        raise GateFailure(
            f"The run in {newest['_path']} verified {verified}, which is not an "
            f"ancestor of {release_commit} — it verified a different line of "
            f"development."
        )

    changed = release_affecting_changes(verified, release_commit)

    if changed:
        raise GateFailure(
            f"The run in {newest['_path']} verified {verified}, and these "
            f"release-affecting files have changed since:\n  "
            + "\n  ".join(changed)
            + "\nRe-run `uv run ui-test --full`."
        )

    unwaived = [
        answer
        for answer in newest.get("answers", [])
        if answer.get("verdict") == "fail"
        and not any(
            waiver.get("surface") == answer.get("surface")
            and waiver.get("question") == answer.get("question")
            and waiver.get("reason", "").strip()
            for waiver in newest.get("waivers", [])
        )
    ]

    if unwaived:
        listed = "\n  ".join(
            f"{answer.get('surface')}: {answer.get('prompt')} — {answer.get('note')}"
            for answer in unwaived
        )

        raise GateFailure(
            f"The run in {newest['_path']} has {len(unwaived)} unwaived "
            f"failure(s):\n  {listed}\n"
            f"Fix them, or add a waiver with a written reason to that record."
        )

    return newest["_path"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--release-commit",
        default="HEAD",
        help="The commit being released.",
    )
    parser.add_argument(
        "--records-directory",
        type=Path,
        default=REPOSITORY_ROOT / RECORDS_DIRECTORY,
    )
    parser.add_argument(
        "--skip",
        action="store_true",
        help="Skip the gate. Requires --skip-reason. Recorded with the release, "
        "so which releases shipped unverified stays answerable.",
    )
    parser.add_argument("--skip-reason", default="")
    arguments = parser.parse_args(argv)

    if arguments.skip:
        reason = arguments.skip_reason.strip()

        if not reason:
            print(
                "Manual verification was skipped without a reason. Give one with "
                "--skip-reason; skipping has to be a deliberate decision, not a "
                "default that drifts into always being set.",
                file=sys.stderr,
            )

            return 1

        print(f"MANUAL VERIFICATION SKIPPED: {reason}")

        return 0

    try:
        release_commit = git("rev-parse", arguments.release_commit)
    except subprocess.CalledProcessError:
        print(f"Not a commit: {arguments.release_commit}", file=sys.stderr)

        return 1

    try:
        accepted = check(load_records(arguments.records_directory), release_commit)
    except GateFailure as failure:
        print(f"Release blocked: {failure}", file=sys.stderr)

        return 1

    print(f"Manual verification satisfied by {accepted}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
