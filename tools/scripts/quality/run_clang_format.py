#!/usr/bin/env python3
"""Run clang-format on the C and C++ files passed by pre-commit.

The version matters. CI checks formatting with the clang-format on its runner,
and formatter versions disagree at the column limit -- 21 accepts wrapping that
18 rejects. That drift once produced a tree that was clean locally and red in
CI, which is a miserable thing to debug, so this warns when the version in use
is not the pinned one.

Get the pinned version with `uv sync --extra coverage`, or set CLANG_FORMAT to
point at a matching binary.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


# The version CI formats with. Kept in step with the `coverage` extra in
# pyproject.toml and with whatever the runner image ships.
EXPECTED_VERSION = "18.1.8"


def installed_version(clang_format: str) -> str | None:
    """The version string of the binary that will actually be used."""
    try:
        output = subprocess.run(
            [clang_format, "--version"], capture_output=True, text=True, check=True
        ).stdout
    except (subprocess.CalledProcessError, OSError):
        return None

    for token in output.split():
        if token[:1].isdigit():
            return token

    return None


def warn_on_version_mismatch(clang_format: str) -> None:
    """Say so when this will format differently from CI.

    A warning rather than an error: a mismatched formatter still formats, and
    refusing to commit over a patch version would be worse than the problem.
    What matters is that a surprising CI failure is explicable.
    """
    version = installed_version(clang_format)

    if version is None or version == EXPECTED_VERSION:
        return

    print(
        f"warning: using clang-format {version}, but CI checks with "
        f"{EXPECTED_VERSION}. They disagree at the column limit, so this tree "
        f"can be clean here and fail there. Install the pinned version with "
        f"`uv sync --extra coverage`, or set CLANG_FORMAT.",
        file=sys.stderr,
    )


def find_tool(environment_variable: str, executable_name: str) -> str | None:
    """Return an explicitly configured tool or find it on PATH."""
    configured_tool = os.environ.get(environment_variable)
    if configured_tool:
        return configured_tool

    return shutil.which(executable_name)


def main(arguments: list[str]) -> int:
    clang_format = find_tool("CLANG_FORMAT", "clang-format")
    if clang_format is None:
        print(
            "clang-format was not found. Install LLVM/Clang or set the "
            "CLANG_FORMAT environment variable.",
            file=sys.stderr,
        )
        return 1

    warn_on_version_mismatch(clang_format)

    source_files = [Path(argument) for argument in arguments if Path(argument).is_file()]
    if not source_files:
        return 0

    result = subprocess.run(
        [clang_format, "-i", "--style=file", *map(str, source_files)],
        check=False,
    )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
