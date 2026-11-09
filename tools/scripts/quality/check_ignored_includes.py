#!/usr/bin/env python3
"""Fail when a quoted ``#include`` in ``src/`` names a file Git ignores.

A header under an ignored directory compiles on the machine that wrote it and
is missing everywhere else, because ``git add`` skips it silently. That is how
``src/application/shell/state/target/TargetClockFollower.h`` went uncommitted:
the Python template's unanchored ``target/`` rule matched the directory, and the
first sign was CI failing to find the header. This check turns that into a
failure that names the include and the ``.gitignore`` rule behind it.

Every quoted include in a C or C++ source under ``src/`` -- tracked, or new and
not yet added -- is resolved the way the compiler resolves it: beside the file
that includes it first, then against each include root (``INCLUDE_ROOTS``,
which follows the ``target_include_directories`` calls in ``CMakeLists.txt``).

- If a candidate exists on disk, the first one is what the compiler reads. It
  fails the check when Git ignores it. This catches the bug on the machine that
  has the file.
- If no candidate exists, the include is someone else's (``JuceHeader.h`` is
  generated into the build tree) or the file is missing. It fails the check when
  any candidate is ignored, because that is what an ignored header looks like
  in a fresh checkout: absent, with a rule that would have hidden it. This
  catches the bug in CI.

A tracked file is never reported, even under an ignore rule, because
``git check-ignore`` does not apply ignore rules to tracked paths: a header that
was force-added is committed, which is all this check cares about.

Angle-bracket includes are not checked; they name system and third-party
headers, which do not live in this repository.

Standard library only, so it runs on the same interpreter as the rest of
``tools/scripts/``.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]

# Directories, relative to the repository, that quoted includes resolve against
# after the including file's own directory. Mirrors the PRIVATE include
# directories of PracticeTakes and PracticeTakesTests in CMakeLists.txt.
INCLUDE_ROOTS = ("src", "src/tests")

SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".mm")

# Leading whitespace, ``#``, optional whitespace, ``include``, then a quoted path.
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


@dataclass(frozen=True)
class Problem:
    """A quoted include that resolves to, or could only resolve to, an ignored path."""

    source: str
    line: int
    include: str
    # Each ignored path with the rule that ignores it. One entry when the header
    # is present (the one the compiler reads); every ignored candidate when it is
    # missing, since there is no telling which one the author had.
    ignored: tuple[tuple[str, str], ...]
    present: bool

    def describe(self) -> str:
        where = f"{self.source}:{self.line}"
        target = f'#include "{self.include}"'
        paths = " or ".join(f"{path} ({rule})" for path, rule in self.ignored)

        if self.present:
            return (
                f"{where}: {target} resolves to {paths}, which Git ignores. "
                f"It exists here and nowhere else: it will never be committed."
            )

        return (
            f"{where}: {target} is missing, and Git would ignore {paths}. "
            f"The header was probably never committed."
        )


def git(repository: Path, *arguments: str, stdin: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repository), *arguments],
        input=stdin,
        capture_output=True,
        text=True,
        check=False,
    )


def source_files(repository: Path, source_root: str) -> list[str]:
    """Tracked and untracked-but-not-ignored C/C++ sources under ``source_root``."""
    result = git(
        repository,
        "ls-files",
        "-z",
        "--cached",
        "--others",
        "--exclude-standard",
        "--",
        source_root,
    )

    if result.returncode != 0:
        raise RuntimeError(f"git ls-files failed: {result.stderr.strip()}")

    files = {path for path in result.stdout.split("\0") if path}

    return sorted(
        path
        for path in files
        if path.endswith(SOURCE_SUFFIXES)
        and "node_modules" not in PurePosixPath(path).parts
        and (repository / path).is_file()
    )


def normalise(path: PurePosixPath) -> str | None:
    """Collapse ``..`` segments; ``None`` when the path climbs out of the repository."""
    parts: list[str] = []

    for part in path.parts:
        if part in ("", "."):
            continue

        if part == "..":
            if not parts:
                return None

            parts.pop()
            continue

        parts.append(part)

    return "/".join(parts) if parts else None


def candidates(source: str, include: str, include_roots: tuple[str, ...]) -> list[str]:
    """Repository-relative paths the compiler would try, in the order it tries them."""
    if PurePosixPath(include).is_absolute():
        return []

    bases = [PurePosixPath(source).parent, *(PurePosixPath(root) for root in include_roots)]
    ordered: list[str] = []

    for base in bases:
        candidate = normalise(base / include)

        if candidate is not None and candidate not in ordered:
            ordered.append(candidate)

    return ordered


def ignore_rules(repository: Path, paths: list[str]) -> dict[str, str]:
    """Map each ignored path among ``paths`` to the rule that ignores it."""
    if not paths:
        return {}

    result = git(repository, "check-ignore", "--verbose", "-z", "--stdin", stdin="\0".join(paths) + "\0")

    # 0: some path is ignored. 1: none are. Anything else is a real failure.
    if result.returncode not in (0, 1):
        raise RuntimeError(f"git check-ignore failed: {result.stderr.strip()}")

    # -z --verbose output: source NUL line NUL pattern NUL path NUL, per match.
    fields = result.stdout.split("\0")
    rules: dict[str, str] = {}

    for index in range(0, len(fields) - 3, 4):
        rule_source, rule_line, pattern, path = fields[index : index + 4]

        # A negated pattern ("!...") that matched means the path is not ignored.
        if pattern.startswith("!"):
            continue

        rules[path] = f"{rule_source}:{rule_line} {pattern}"

    return rules


def find_problems(
    repository: Path,
    source_root: str = "src",
    include_roots: tuple[str, ...] = INCLUDE_ROOTS,
) -> list[Problem]:
    """Every quoted include under ``source_root`` that an ignore rule hides."""
    includes: list[tuple[str, int, str, list[str]]] = []

    for source in source_files(repository, source_root):
        text = (repository / source).read_text(encoding="utf-8", errors="replace")

        for match in INCLUDE_PATTERN.finditer(text):
            include = match.group(1)
            line = text.count("\n", 0, match.start()) + 1
            includes.append((source, line, include, candidates(source, include, include_roots)))

    every_candidate = sorted({path for *_, paths in includes for path in paths})
    rules = ignore_rules(repository, every_candidate)
    problems: list[Problem] = []

    for source, line, include, paths in includes:
        present = [path for path in paths if (repository / path).is_file()]

        if present:
            # The compiler reads the first one it finds; the others do not matter.
            checked = present[:1]
        else:
            checked = paths

        ignored = tuple((path, rules[path]) for path in checked if path in rules)

        if ignored:
            problems.append(Problem(source, line, include, ignored, bool(present)))

    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--repository",
        type=Path,
        default=REPOSITORY_ROOT,
        help="Repository to check (defaults to this one).",
    )
    arguments = parser.parse_args(argv)

    problems = find_problems(arguments.repository.resolve())

    if problems:
        print("Quoted includes in src/ name files that Git ignores:", file=sys.stderr)

        for problem in problems:
            print(f"  - {problem.describe()}", file=sys.stderr)

        print(
            "Anchor the .gitignore rule to the root (a leading /) if its tool only writes there, "
            "or rename the directory.",
            file=sys.stderr,
        )

        return 1

    print("No quoted include in src/ names an ignored file.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
