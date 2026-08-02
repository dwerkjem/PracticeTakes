#!/usr/bin/env python3
"""Install this repository's merge configuration into the local Git config.

Git deliberately refuses to read config out of a cloned tree — a repository
that could set arbitrary config would be a remote code execution vector — so
``.gitattributes`` can name a merge driver but cannot define one. Every clone
has to opt in locally, which is what this does.

It is idempotent and quiet: it only prints when it changes something, so the
pre-commit hook that calls it stays invisible until it has work to do.

What gets set, and why each one earned its place:

``merge.conflictStyle=zdiff3``
    Shows the common ancestor inside a conflict. Without it a rename-heavy
    merge gives two versions and no way to tell which side moved a line.

``merge.directoryRenames=true``
    Follows a directory rename onto files the other branch added inside it.
    Without it, a branch that adds ``src/tests/foo/Bar.cpp`` while the base
    moves ``tests/`` elsewhere leaves the file stranded at the old path.

``merge.renameLimit`` / ``diff.renameLimit``
    Rename detection gives up past this many candidates and silently degrades
    to add+delete. The defaults are well under the size of a repository-wide
    move, which is exactly when rename detection matters most.

``rerere.enabled=true``
    Records how a conflict was resolved and replays it on the next merge that
    produces the same conflict. Syncing one change across a fleet of branches
    otherwise means resolving the identical conflict once per branch.

``merge.<driver>.driver``
    The custom drivers named in ``.gitattributes``.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
DRIVER_DIRECTORY = Path("tools/scripts/git")

# Rename detection is quadratic, but the cost only lands on merges that are
# already unusual. A repository-wide move ran to roughly 250 files; 10000
# leaves headroom without being effectively unbounded.
RENAME_LIMIT = "10000"

SETTINGS: tuple[tuple[str, str], ...] = (
    ("merge.conflictStyle", "zdiff3"),
    ("merge.directoryRenames", "true"),
    ("merge.renameLimit", RENAME_LIMIT),
    ("diff.renameLimit", RENAME_LIMIT),
    ("rerere.enabled", "true"),
    ("rerere.autoUpdate", "true"),
    # Built-in "keep ours" driver, used by .gitattributes for generated files.
    ("merge.ours.driver", "true"),
    ("merge.ours.name", "keep our version of a generated file"),
)

DRIVERS: tuple[tuple[str, str, str], ...] = (
    (
        "cmake-sources",
        "merge_cmake_sources.py",
        "union the CMake source lists, conflict on anything else",
    ),
    (
        "npm-lock",
        "merge_npm_lock.py",
        "regenerate package-lock.json from the merged manifest",
    ),
)


def git_config(root: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(root), "config", *arguments],
        capture_output=True,
        text=True,
    )


def current(root: Path, key: str) -> str | None:
    completed = git_config(root, "--local", "--get", key)
    return completed.stdout.strip() if completed.returncode == 0 else None


def desired_settings(root: Path) -> list[tuple[str, str]]:
    """Every key/value this repository wants, drivers included.

    The driver command is deliberately **relative**. Linked worktrees share one
    ``.git/config``, so an absolute path would be rewritten to whichever
    worktree committed last, and would break every merge everywhere the moment
    that worktree was removed. Git runs a merge driver from the top of the
    working tree doing the merge, so a relative path resolves correctly in the
    main checkout and in every worktree at once.
    """
    settings = list(SETTINGS)

    for name, script, description in DRIVERS:
        command = f"python3 {(DRIVER_DIRECTORY / script).as_posix()} %O %A %B %P"
        settings.append((f"merge.{name}.driver", command))
        settings.append((f"merge.{name}.name", description))

    return settings


def apply(root: Path, *, check_only: bool) -> list[str]:
    """Set anything missing or stale. Returns the keys that differ."""
    changed: list[str] = []

    for key, value in desired_settings(root):
        if current(root, key) == value:
            continue

        changed.append(key)
        if not check_only:
            result = git_config(root, "--local", key, value)
            if result.returncode != 0:
                raise RuntimeError(f"git config {key} failed: {result.stderr.strip()}")

    return changed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="report what would change without writing anything",
    )
    parser.add_argument(
        "--repository",
        type=Path,
        default=REPOSITORY_ROOT,
        help="repository to configure (defaults to this checkout)",
    )
    arguments = parser.parse_args(argv)

    inside = subprocess.run(
        ["git", "-C", str(arguments.repository), "rev-parse", "--git-dir"],
        capture_output=True,
        text=True,
    )
    if inside.returncode != 0:
        print(f"{arguments.repository} is not a Git repository", file=sys.stderr)
        return 1

    try:
        changed = apply(arguments.repository, check_only=arguments.check)
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 1

    if not changed:
        return 0

    if arguments.check:
        print("Merge configuration is out of date:")
        for key in changed:
            print(f"  {key}")
        print("Run: python3 tools/scripts/git/configure_merge.py")
        return 1

    print(f"Configured {len(changed)} Git merge setting(s) for this clone:")
    for key in changed:
        print(f"  {key}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
