#!/usr/bin/env python3
"""Run clang-format on the C and C++ files passed by pre-commit."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


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
