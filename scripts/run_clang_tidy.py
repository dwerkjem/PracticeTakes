#!/usr/bin/env python3
"""Run clang-tidy with the CMake compilation database."""

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
    clang_tidy = find_tool("CLANG_TIDY", "clang-tidy")
    if clang_tidy is None:
        print(
            "clang-tidy was not found. Install LLVM/Clang or set the "
            "CLANG_TIDY environment variable.",
            file=sys.stderr,
        )
        return 1

    build_directory = Path(os.environ.get("CLANG_TIDY_BUILD_DIR", "build"))
    compilation_database = build_directory / "compile_commands.json"

    if not compilation_database.is_file():
        print(
            f"{compilation_database} does not exist. Configure the project first:\n"
            f"  cmake -S . -B {build_directory} "
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            file=sys.stderr,
        )
        return 1

    source_files = [Path(argument) for argument in arguments if Path(argument).is_file()]
    if not source_files:
        return 0

    result = subprocess.run(
        [
            clang_tidy,
            "-p",
            str(build_directory),
            *map(str, source_files),
        ],
        check=False,
    )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
