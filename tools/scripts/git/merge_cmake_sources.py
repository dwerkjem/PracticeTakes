#!/usr/bin/env python3
"""Git merge driver for the source lists in ``CMakeLists.txt``.

Every conflict this project has seen in ``CMakeLists.txt`` had the same shape:
two branches each added, removed, or renamed entries in one of the long path
lists inside ``target_sources(...)`` or ``add_executable(...)``. Git treats
adjacent line insertions as a conflict even though the lists are unordered sets
of paths, so the resolution is always mechanical and always the same — take
every path both sides still want.

This driver runs the ordinary three-way merge first and only then looks at what
conflicted. A conflict region is resolved here **only if all three sides of it
consist entirely of source-path lines**. Anything else — a changed
``set()``, two different ``if()`` bodies, a rewritten comment — is left
conflicted for a human, because a build file that silently merges wrong still
parses, and the mistake surfaces as a confusing link error much later.

Git invokes it as::

    merge_cmake_sources.py %O %A %B %P

writing the result over ``%A`` and exiting non-zero if anything is still
conflicted.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
from typing import Sequence


# A path entry in a CMake source list: indentation, then a relative path with a
# source or header suffix, and nothing else on the line. Deliberately strict —
# a line carrying a generator expression, a variable, or a trailing comment is
# not something this driver is willing to reorder.
SOURCE_LINE = re.compile(r"^\s*[\w./+-]+\.(?:cpp|cc|cxx|c|hpp|hxx|h|mm|m)\s*$")

CONFLICT_START = re.compile(r"^<{7}")
CONFLICT_MIDDLE = re.compile(r"^\|{7}|^={7}$")
CONFLICT_BASE = re.compile(r"^\|{7}")
CONFLICT_SEPARATOR = re.compile(r"^={7}$")
CONFLICT_END = re.compile(r"^>{7}")


class MergeError(Exception):
    """The driver could not produce a result, conflicted or otherwise."""


def three_way_merge(base: Path, ours: Path, theirs: Path) -> tuple[list[str], bool]:
    """Run ``git merge-file`` and return its lines plus whether it conflicted.

    ``--diff3`` is requested so each conflict region carries the base section,
    which is what lets :func:`resolve_region` tell an added path from a removed
    one instead of guessing.
    """
    completed = subprocess.run(
        [
            "git",
            "merge-file",
            "--diff3",
            "-p",
            str(ours),
            str(base),
            str(theirs),
        ],
        capture_output=True,
        text=True,
    )

    if completed.returncode < 0:
        raise MergeError(f"git merge-file died: {completed.stderr.strip()}")

    return completed.stdout.splitlines(), completed.returncode != 0


def split_regions(lines: Sequence[str]) -> list[tuple[str, object]]:
    """Split merged output into ``("text", lines)`` and ``("conflict", sides)``."""
    regions: list[tuple[str, object]] = []
    plain: list[str] = []
    index = 0

    while index < len(lines):
        if not CONFLICT_START.match(lines[index]):
            plain.append(lines[index])
            index += 1
            continue

        if plain:
            regions.append(("text", plain))
            plain = []

        index += 1
        ours: list[str] = []
        base: list[str] = []
        theirs: list[str] = []
        target = ours

        while index < len(lines) and not CONFLICT_END.match(lines[index]):
            if CONFLICT_BASE.match(lines[index]):
                target = base
            elif CONFLICT_SEPARATOR.match(lines[index]):
                target = theirs
            else:
                target.append(lines[index])
            index += 1

        if index >= len(lines):
            raise MergeError("unterminated conflict region in merge output")

        index += 1
        regions.append(("conflict", (ours, base, theirs)))

    if plain:
        regions.append(("text", plain))

    return regions


def is_source_list(*sides: Sequence[str]) -> bool:
    """Whether every line on every side is a bare source path.

    Empty sides are fine — one branch deleting the whole block is still a
    set operation. A side with any other content is not.
    """
    return all(SOURCE_LINE.match(line) for side in sides for line in side)


def resolve_region(ours: list[str], base: list[str], theirs: list[str]) -> list[str]:
    """Union the two sides against their base, honouring deletions.

    A path survives when neither side removed it, and appears when either side
    added it. Order follows the base, so an untouched list keeps its grouping;
    additions land after it, ours before theirs, each in the order written.
    """
    ours_set, base_set, theirs_set = set(ours), set(base), set(theirs)

    removed = (base_set - ours_set) | (base_set - theirs_set)
    kept = [line for line in base if line not in removed]

    added: list[str] = []
    for side in (ours, theirs):
        for line in side:
            if line not in base_set and line not in added:
                added.append(line)

    merged = kept + added

    # A list that was sorted before the merge should stay sorted afterwards,
    # otherwise every future addition lands at the bottom and the grouping rots.
    if kept == sorted(kept):
        merged = sorted(merged)

    return merged


def merge(base: Path, ours: Path, theirs: Path) -> tuple[str, bool]:
    """Merge the three versions. Returns the text and whether it still conflicts."""
    merged_lines, conflicted = three_way_merge(base, ours, theirs)

    if not conflicted:
        return "\n".join(merged_lines) + "\n", False

    output: list[str] = []
    unresolved = False

    for kind, payload in split_regions(merged_lines):
        if kind == "text":
            output.extend(payload)  # type: ignore[arg-type]
            continue

        ours_side, base_side, theirs_side = payload  # type: ignore[misc]

        if is_source_list(ours_side, base_side, theirs_side):
            output.extend(resolve_region(ours_side, base_side, theirs_side))
            continue

        unresolved = True
        output.append("<" * 7 + " ours")
        output.extend(ours_side)
        output.append("|" * 7 + " base")
        output.extend(base_side)
        output.append("=" * 7)
        output.extend(theirs_side)
        output.append(">" * 7 + " theirs")

    return "\n".join(output) + "\n", unresolved


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, help="common ancestor (%%O)")
    parser.add_argument("ours", type=Path, help="our version, overwritten (%%A)")
    parser.add_argument("theirs", type=Path, help="their version (%%B)")
    parser.add_argument("path", nargs="?", default="", help="pathname (%%P)")
    arguments = parser.parse_args(argv)

    try:
        text, unresolved = merge(arguments.base, arguments.ours, arguments.theirs)
    except MergeError as error:
        print(f"merge_cmake_sources: {error}", file=sys.stderr)
        return 1

    arguments.ours.write_text(text, encoding="utf-8")

    if unresolved:
        label = arguments.path or arguments.ours
        print(
            f"merge_cmake_sources: {label} has conflicts outside the source "
            "lists; resolve them by hand.",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
