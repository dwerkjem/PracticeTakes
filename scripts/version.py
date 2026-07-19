#!/usr/bin/env python3
"""Manage the Practice Takes semantic version stored in the root VERSION file."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Final

PROJECT_ROOT: Final = Path(__file__).resolve().parents[1]
VERSION_FILE: Final = PROJECT_ROOT / "VERSION"
VCPKG_MANIFEST_FILE: Final = PROJECT_ROOT / "vcpkg.json"
VERSION_PATTERN: Final = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$"
)


def parse_version(value: str) -> tuple[int, int, int]:
    """Parse and validate a MAJOR.MINOR.PATCH version."""
    normalized = value.strip()
    match = VERSION_PATTERN.fullmatch(normalized)

    if match is None:
        raise ValueError(
            f"invalid semantic version {normalized!r}; expected MAJOR.MINOR.PATCH"
        )

    return tuple(int(part) for part in match.groups())  # type: ignore[return-value]


def format_version(version: tuple[int, int, int]) -> str:
    """Convert a parsed version tuple back to text."""
    return ".".join(str(part) for part in version)


def read_version() -> tuple[int, int, int]:
    """Read the current version from VERSION."""
    try:
        return parse_version(VERSION_FILE.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing version file: {VERSION_FILE}") from exc


def write_version(version: tuple[int, int, int]) -> str:
    """Write a validated version to VERSION and the vcpkg manifest."""
    value = format_version(version)
    manifest = json.loads(VCPKG_MANIFEST_FILE.read_text(encoding="utf-8"))
    manifest["version-string"] = value

    VERSION_FILE.write_text(f"{value}\n", encoding="utf-8")
    VCPKG_MANIFEST_FILE.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    return value


def calculate_next(
    version: tuple[int, int, int], bump_type: str
) -> tuple[int, int, int]:
    """Return the next semantic version for a patch, minor, or major release."""
    major, minor, patch = version

    if bump_type == "patch":
        return major, minor, patch + 1
    if bump_type == "minor":
        return major, minor + 1, 0
    if bump_type == "major":
        return major + 1, 0, 0

    raise ValueError(f"unsupported version bump: {bump_type}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("current", help="print the current version")

    next_parser = subparsers.add_parser("next", help="print the next version")
    next_parser.add_argument("bump", choices=("patch", "minor", "major"))

    set_parser = subparsers.add_parser("set", help="write an exact version")
    set_parser.add_argument("version")

    bump_parser = subparsers.add_parser("bump", help="increment and write VERSION")
    bump_parser.add_argument("bump", choices=("patch", "minor", "major"))

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        current = read_version()

        if args.command == "current":
            result = format_version(current)
        elif args.command == "next":
            result = format_version(calculate_next(current, args.bump))
        elif args.command == "set":
            result = write_version(parse_version(args.version))
        elif args.command == "bump":
            result = write_version(calculate_next(current, args.bump))
        else:
            parser.error(f"unknown command: {args.command}")
            return 2
    except (OSError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")

    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
