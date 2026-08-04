#!/usr/bin/env python3
"""Git merge driver for ``package-lock.json``.

A lockfile is generated, so merging it line by line is meaningless — the only
correct resolution is to regenerate it from the merged ``package.json``. This
driver takes the incoming version as a starting point and runs
``npm install --package-lock-only``, which rewrites the tree from the manifest
without touching ``node_modules``.

It refuses rather than guesses. If ``npm`` is missing, the manifest is itself
conflicted, or the regeneration fails for any reason — no network, a bad
registry — the driver exits non-zero and Git presents an ordinary conflict.
That keeps an offline clone from silently committing a half-resolved lockfile.

Git invokes it as::

    merge_npm_lock.py %O %A %B %P
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


CONFLICT_MARKERS = ("<<<<<<<", ">>>>>>>")


def find_manifest(repository_root: Path, lockfile_path: str) -> Path | None:
    """The ``package.json`` beside the lockfile Git is merging."""
    if not lockfile_path:
        return None

    manifest = repository_root / Path(lockfile_path).parent / "package.json"
    return manifest if manifest.is_file() else None


def manifest_is_conflicted(manifest: Path) -> bool:
    """Whether the manifest still carries conflict markers.

    Regenerating from a conflicted manifest would produce a lockfile that
    matches neither side, so this is a refusal case rather than a failure.
    """
    text = manifest.read_text(encoding="utf-8", errors="replace")
    return any(marker in text for marker in CONFLICT_MARKERS)


def regenerate(manifest_directory: Path) -> bool:
    """Run ``npm install --package-lock-only``. True when it succeeded."""
    npm = shutil.which("npm")
    if npm is None:
        print("merge_npm_lock: npm is not on PATH", file=sys.stderr)
        return False

    completed = subprocess.run(
        [npm, "install", "--package-lock-only", "--ignore-scripts"],
        cwd=manifest_directory,
        capture_output=True,
        text=True,
    )

    if completed.returncode != 0:
        detail = completed.stderr.strip().splitlines()
        print(
            "merge_npm_lock: npm install --package-lock-only failed"
            + (f": {detail[-1]}" if detail else ""),
            file=sys.stderr,
        )
        return False

    return True


def repository_root() -> Path:
    completed = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        return Path.cwd()
    return Path(completed.stdout.strip())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, help="common ancestor (%%O)")
    parser.add_argument("ours", type=Path, help="our version, overwritten (%%A)")
    parser.add_argument("theirs", type=Path, help="their version (%%B)")
    parser.add_argument("path", nargs="?", default="", help="pathname (%%P)")
    arguments = parser.parse_args(argv)

    root = Path(os.environ.get("PRACTICE_TAKES_REPO_ROOT", "")) or repository_root()
    manifest = find_manifest(root, arguments.path)

    if manifest is None:
        print(
            f"merge_npm_lock: no package.json beside {arguments.path or 'the lockfile'}",
            file=sys.stderr,
        )
        return 1

    if manifest_is_conflicted(manifest):
        print(
            f"merge_npm_lock: {manifest} is still conflicted; resolve it first, "
            "then re-run the merge.",
            file=sys.stderr,
        )
        return 1

    directory = manifest.parent
    lockfile = directory / "package-lock.json"
    preserved = lockfile.read_bytes() if lockfile.is_file() else None

    # Start from the incoming side so a dependency bump on the other branch is
    # the baseline, then let npm reconcile it against the merged manifest.
    lockfile.write_bytes(arguments.theirs.read_bytes())

    try:
        if not regenerate(directory):
            if preserved is not None:
                lockfile.write_bytes(preserved)
            return 1

        regenerated = lockfile.read_bytes()
    finally:
        if preserved is not None and not lockfile.is_file():
            lockfile.write_bytes(preserved)

    try:
        json.loads(regenerated)
    except json.JSONDecodeError as error:
        print(f"merge_npm_lock: regenerated lockfile is not valid JSON: {error}", file=sys.stderr)
        if preserved is not None:
            lockfile.write_bytes(preserved)
        return 1

    arguments.ours.write_bytes(regenerated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
