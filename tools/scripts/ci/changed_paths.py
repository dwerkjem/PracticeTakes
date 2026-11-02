#!/usr/bin/env python3
"""Decide whether a pull request touches the files a check is about.

A workflow can filter itself with ``on.pull_request.paths``, and several here
did. A filtered workflow that does not match never starts, so it never reports
a status -- and a check that never reports can never be *required*, because the
pull request waits for it forever. Every documentation-only change would block.

So a check that should be required always starts, asks this whether it applies,
and skips its real job when it does not. GitHub records a skipped job as passing,
which is what lets a required check stay out of the way of a change it has
nothing to say about.

The patterns are the ones ``on.<event>.paths`` takes, with the same meaning, so
moving a list from the trigger to here changes nothing about which changes a
check runs for:

- ``*`` matches anything but ``/``; ``**`` matches anything, ``/`` included.
- ``!`` in front of a pattern excludes what it matches.
- Patterns are read in order and the last one that matches a file decides it,
  so ``src/**`` then ``!src/services/**`` is "src except services".

A check applies when any changed file is included. Renames count under both
names, so moving a file out of a check's scope still runs the check once.

Standard library only.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Iterable


def _pattern_to_regex(pattern: str) -> re.Pattern[str]:
    parts: list[str] = []
    index = 0

    while index < len(pattern):
        if pattern.startswith("**", index):
            # "dir/**" also matches the files directly in dir, and "**/" matches
            # zero or more whole directories, as GitHub's filter does.
            if pattern.startswith("**/", index):
                parts.append("(?:.*/)?")
                index += 3
            else:
                parts.append(".*")
                index += 2
        elif pattern[index] == "*":
            parts.append("[^/]*")
            index += 1
        elif pattern[index] == "?":
            parts.append("[^/]")
            index += 1
        else:
            parts.append(re.escape(pattern[index]))
            index += 1

    return re.compile("".join(parts) + r"\Z")


def parse_patterns(text: str) -> list[str]:
    """One pattern per line, as a YAML block scalar gives them. Blank lines and
    ``#`` comments are ignored, so the list can carry its reasons inline."""

    patterns = []

    for raw in text.splitlines():
        line = raw.strip()

        if not line or line.startswith("#"):
            continue

        patterns.append(line.strip("\"'"))

    return patterns


def is_included(path: str, patterns: Iterable[str]) -> bool:
    included = False

    for pattern in patterns:
        negated = pattern.startswith("!")
        body = pattern[1:] if negated else pattern

        if _pattern_to_regex(body).match(path):
            included = not negated

    return included


def applies(changed: Iterable[str], patterns: list[str]) -> bool:
    return any(is_included(path, patterns) for path in changed)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--patterns", required=True, help="Newline-separated path patterns."
    )
    parser.add_argument(
        "changed",
        help="File of changed paths, one per line; '-' reads standard input.",
    )
    arguments = parser.parse_args(argv)

    patterns = parse_patterns(arguments.patterns)

    if not patterns:
        # A check with no patterns would never run. That is a mistake in the
        # workflow, not a decision, so it fails rather than skipping quietly.
        print("No path patterns were given.", file=sys.stderr)
        return 2

    stream = sys.stdin if arguments.changed == "-" else open(arguments.changed, encoding="utf-8")

    with stream:
        changed = [line.strip() for line in stream if line.strip()]

    print(f"relevant={'true' if applies(changed, patterns) else 'false'}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
