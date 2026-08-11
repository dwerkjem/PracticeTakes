#!/usr/bin/env python3
"""Whether the application under test was built from the source that is here now.

A stale binary is the worst kind of wrong, because everything works. The capture
pass runs, every surface photographs, the summary says twelve captured and none
failed, and the images show an application from last week. Nothing in the output
distinguishes that from a run of the current build.

It has already cost twice in this project's history: a capture of the old tuner
reported success against a tree that had rebuilt it, and a manual launch refused
a geometry the source had added days earlier — the second only noticed because
the app said so out loud.

So this compares the executable's timestamp against the newest source file and
says plainly when the binary is behind. A warning rather than a refusal:
capturing an older build on purpose is a legitimate thing to do, and the run
record has a field for saying which build it was. What is not legitimate is
doing it by accident.

`Tooling.ensure` in capture.py already rebuilds the X helpers on the same
comparison; this applies the idea to the thing actually under test.

Standard library only.
"""

from __future__ import annotations

from pathlib import Path

# What the application is built from. The CMake lists matter because adding a
# source file changes the binary without changing any existing file.
SOURCE_ROOTS = ("src",)
SOURCE_FILES = ("CMakeLists.txt",)

SOURCE_SUFFIXES = (".cpp", ".h", ".txt")

# The suite's own code. The hub is a long-lived process that imported this once
# at startup, while `web/` is re-read from disk on every request -- so an editor
# open on this directory can leave a running hub answering new pages with old
# code. Only Python is compared, because only Python is the part that got
# frozen at import.
SUITE_ROOT = Path(__file__).resolve().parent


def newest_source_time(repository_root: Path) -> float:
    """The most recent modification time across the application's sources.

    Returns 0.0 when nothing is found, which makes every binary look current --
    the right way to fail for a check that must never block a run on its own.
    """
    newest = 0.0

    for name in SOURCE_FILES:
        path = repository_root / name

        if path.is_file():
            newest = max(newest, path.stat().st_mtime)

    for root in SOURCE_ROOTS:
        directory = repository_root / root

        if not directory.is_dir():
            continue

        for path in directory.rglob("*"):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                newest = max(newest, path.stat().st_mtime)

    return newest


def is_stale(executable: Path, repository_root: Path) -> bool:
    """Whether the binary predates the newest source file."""
    if not executable.is_file():
        return False

    newest = newest_source_time(repository_root)

    return newest > 0.0 and executable.stat().st_mtime < newest


def staleness_warning(executable: Path, repository_root: Path) -> str:
    """A warning to print, or "" when the binary is current.

    Names the rebuild command, because the answer is always the same and the
    person reading this is in the middle of something else.
    """
    if not is_stale(executable, repository_root):
        return ""

    return (
        f"WARNING: {executable} is older than the source it was built from.\n"
        f"         What follows describes that binary, not the code in this tree.\n"
        f"         cmake --build {executable.parents[1].name} "
        f"--target PracticeTakes --parallel"
    )


def newest_suite_source_time(suite_root: Path = SUITE_ROOT) -> float:
    """The most recent modification time across the suite's own modules."""
    newest = 0.0

    for path in suite_root.glob("*.py"):
        if path.is_file():
            newest = max(newest, path.stat().st_mtime)

    return newest


def suite_staleness_warning(loaded_at: float, suite_root: Path = SUITE_ROOT) -> str:
    """A warning for a hub running code older than the files on disk, or "".

    The failure this names is a nasty one because it looks like a bug in the
    feature rather than in the process serving it: the page is fetched fresh
    and asks for something the running code has never heard of, so a button
    does nothing and a comment renders blank. Nothing in either half is broken.

    `loaded_at` is what `newest_suite_source_time` returned when the process
    started. Zero disables the check, which is the right way to fail for
    something that must never stop a run on its own.
    """
    if loaded_at <= 0.0 or newest_suite_source_time(suite_root) <= loaded_at:
        return ""

    return (
        "This hub is running code older than the files on disk. Quit it and "
        "start it again — the page is served fresh on every request, so what "
        "you are looking at is newer than what answers it."
    )
