#!/usr/bin/env python3
"""Check that ``tests/`` mirrors ``src/``.

A test's path is meant to name the source directory it covers, so
``src/services/score/TempoMap.h`` is tested by
``tests/services/score/TempoMapTests.cpp``. That only stays true if something
enforces it: the tree was flat until it was reorganised, and nothing stops a
new test being dropped at the top level out of habit.

Two rules are checked.

1. No test file sits directly at the root of ``tests/``. The root is where
   files land when nobody decided where they belong.
2. Every directory holding tests mirrors a directory that exists under
   ``src/``, unless it is explicitly exempt. A mirrored path with no
   counterpart usually means the source moved and the test did not, which
   leaves the test covering something by a name that no longer describes it.

Exempt directories are listed in ``NON_MIRRORED_DIRECTORIES`` below. That list
is deliberately the documentation of what is outside the mirror and why —
suites that do not correspond to a single source file (shared fixtures, and
later the smoke and load suites) have nowhere sensible to mirror to.

Standard library only, so it runs on the same interpreter as the rest of
``scripts/``.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

# Directories under ``tests/`` that intentionally mirror nothing in ``src/``,
# mapped to the reason. Adding an entry is a deliberate act, which is the
# point: it keeps "outside the mirror" a short, reviewable list rather than an
# emergent property of wherever files happened to be put.
NON_MIRRORED_DIRECTORIES = {
    "support": "Fixtures shared across areas, which belong to no one source directory.",
}

TEST_SUFFIXES = (".cpp", ".h")


def display(path: Path) -> str:
    """Path as written in a message.

    Relative to the repository when it is inside it, absolute otherwise — the
    roots are arguments, so they are not always under this repository.
    """
    try:
        return str(path.relative_to(REPOSITORY_ROOT))
    except ValueError:
        return str(path)


def is_exempt(relative_directory: Path) -> bool:
    """Whether a directory under ``tests/`` is outside the mirror by design."""
    if not relative_directory.parts:
        return False

    return relative_directory.parts[0] in NON_MIRRORED_DIRECTORIES


def find_problems(tests_root: Path, source_root: Path) -> list[str]:
    """Return a human-readable problem per violation, empty when the tree is fine."""
    problems: list[str] = []

    if not tests_root.is_dir():
        return [f"No tests directory at {tests_root}"]

    for path in sorted(tests_root.rglob("*")):
        if not path.is_file() or path.suffix not in TEST_SUFFIXES:
            continue

        relative = path.relative_to(tests_root)
        directory = relative.parent

        # Rule 1: nothing at the root.
        if directory == Path("."):
            problems.append(
                f"{display(path)} sits at the root of tests/. "
                f"Move it beside the mirror of the file it covers, "
                f"for example tests/services/score/ for src/services/score/."
            )
            continue

        if is_exempt(directory):
            continue

        # Rule 2: the mirrored directory must exist.
        mirrored = source_root / directory

        if not mirrored.is_dir():
            problems.append(
                f"{display(path)} mirrors "
                f"{display(mirrored)}, which does not exist. "
                f"Either the source moved and the test did not, or the directory "
                f"belongs in NON_MIRRORED_DIRECTORIES with a reason."
            )

    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tests-root",
        type=Path,
        default=REPOSITORY_ROOT / "tests",
        help="Directory holding the tests (defaults to the repository's tests/).",
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=REPOSITORY_ROOT / "src",
        help="Directory the tests mirror (defaults to the repository's src/).",
    )
    arguments = parser.parse_args(argv)

    problems = find_problems(arguments.tests_root, arguments.source_root)

    if problems:
        print("tests/ does not mirror src/:", file=sys.stderr)

        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)

        return 1

    print("tests/ mirrors src/.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
