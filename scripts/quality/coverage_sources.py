#!/usr/bin/env python3
"""Work out which ``src/`` files are not built into the test binary at all.

This is the half of a coverage report that a coverage tool cannot produce, and
the half that stops the headline figure from lying.

``gcovr`` measures the files it was given, which are the files compiled into
``PracticeTakesTests``. If a source file is absent from
``add_executable(PracticeTakesTests ...)`` it is simply not in the report, so it
does not appear in the denominator either — and a project that tests a third of
its code can report a healthy percentage over that third while the rest is
invisible. ``docs/development/QA_STRATEGY.md`` area 9 counted 64 of 99 files in
the test build by hand, which is exactly the gap this makes visible.

So a file that is not compiled into the test binary is reported as its own
category, distinct from and more serious than one that is compiled and covered
0%. The first means nobody can test it without changing the build; the second
means somebody chose not to.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

# Files that are compiled but hold no executable lines, so their absence from
# the test binary says nothing. Headers are pulled in by whatever includes them
# rather than listed as compiled units.
COMPILED_SUFFIXES = (".cpp",)

# The block whose contents name every translation unit in the test binary.
TEST_TARGET_PATTERN = re.compile(
    r"add_executable\(PracticeTakesTests\b(.*?)^\s*\)", re.DOTALL | re.MULTILINE
)

SOURCE_ENTRY_PATTERN = re.compile(r"^\s*(src/[^\s)]+)\s*$", re.MULTILINE)


def read_test_target_sources(cmake_lists: Path) -> set[str]:
    """Every ``src/`` path listed in ``add_executable(PracticeTakesTests ...)``.

    Raises when the block cannot be found, rather than returning an empty set —
    an empty set would silently report every source file as untested, which
    looks like a catastrophic finding instead of a parsing failure.
    """
    text = cmake_lists.read_text(encoding="utf-8")
    match = TEST_TARGET_PATTERN.search(text)

    if match is None:
        raise ValueError(
            f"Could not find add_executable(PracticeTakesTests ...) in {cmake_lists}. "
            f"If the target was renamed, update TEST_TARGET_PATTERN."
        )

    return set(SOURCE_ENTRY_PATTERN.findall(match.group(1)))


def find_compiled_sources(source_root: Path) -> set[str]:
    """Every compiled translation unit under ``src/``, as repository-relative paths."""
    sources: set[str] = set()

    for path in source_root.rglob("*"):
        if path.is_file() and path.suffix in COMPILED_SUFFIXES:
            sources.add(f"src/{path.relative_to(source_root).as_posix()}")

    return sources


def classify(source_root: Path, cmake_lists: Path) -> dict[str, list[str]]:
    """Split ``src/`` sources into those in the test build and those outside it."""
    compiled = find_compiled_sources(source_root)
    in_test_target = read_test_target_sources(cmake_lists)

    # A path listed in CMake that no longer exists is worth surfacing: the build
    # would fail, but if the list is being read for reporting the mismatch would
    # otherwise pass unnoticed.
    #
    # Checked against the filesystem rather than against `compiled`, because
    # targets legitimately list headers and those are not translation units --
    # comparing against `compiled` would report every listed header as missing.
    missing = sorted(
        path
        for path in in_test_target
        if not (source_root / Path(path).relative_to("src")).is_file()
    )

    return {
        "in_test_build": sorted(compiled & in_test_target),
        "not_in_test_build": sorted(compiled - in_test_target),
        "listed_but_absent": missing,
    }


def summarise(classified: dict[str, list[str]]) -> str:
    inside = len(classified["in_test_build"])
    outside = len(classified["not_in_test_build"])
    total = inside + outside

    if total == 0:
        return "No compiled sources found under src/."

    percentage = inside * 100.0 / total

    lines = [
        f"Compiled sources under src/: {total}",
        f"  in the test build:     {inside} ({percentage:.1f}%)",
        f"  NOT in the test build: {outside}",
    ]

    if outside:
        lines.append("")
        lines.append("Not built into PracticeTakesTests, so unmeasurable by any")
        lines.append("coverage tool until they are added to the target:")
        lines.extend(f"  - {path}" for path in classified["not_in_test_build"])

    if classified["listed_but_absent"]:
        lines.append("")
        lines.append("Listed in the test target but absent from disk:")
        lines.extend(f"  - {path}" for path in classified["listed_but_absent"])

    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=REPOSITORY_ROOT / "src")
    parser.add_argument("--cmake-lists", type=Path, default=REPOSITORY_ROOT / "CMakeLists.txt")
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable output for the CI report instead of a summary.",
    )
    arguments = parser.parse_args(argv)

    classified = classify(arguments.source_root, arguments.cmake_lists)

    if arguments.json:
        print(json.dumps(classified, indent=2))
    else:
        print(summarise(classified))

    # Reporting only. Nothing here fails a build -- coverage is informational
    # for this change, and a file being outside the test build is a fact to
    # publish, not a regression to block on.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
