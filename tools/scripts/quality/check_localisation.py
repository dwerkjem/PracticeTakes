#!/usr/bin/env python3
"""Check the text catalogues against the code that uses them.

Every sentence Practice Takes shows a user is an entry in a catalogue under
``tools/packaging/localisation/``, addressed by identifier
(openspec/changes/archive/2026-11-09-internationalization). Two things go wrong silently there,
so both fail this check instead:

1. Code references an identifier the English catalogue does not define. The
   application would show a user ``score.import.slur.unsupported`` where a
   sentence should be.
2. The English catalogue defines an identifier nothing references. It would be
   translated into every language, each time, for a screen that is gone.

A reference is a string literal as the first argument of ``l10n::text``,
``l10n::format``, ``l10n::plural``, ``l10n::key`` or ``l10n::pluralKey`` in a
source file under ``src/`` -- qualified, so the check does not have to resolve
names. ``l10n::key`` and ``l10n::pluralKey`` do nothing at run time: they mark
an identifier kept in a table and looked up later, which is how such an
identifier is found here. A reference inside a comment, or in a test, does not
count: a key used only there is still unused by the application.

A plural reference ``l10n::plural("score.bars", ...)`` uses every form
``score.bars.one``, ``score.bars.other`` and so on, and requires English to
have its ``one`` and ``other``.

It also checks that a translation defines nothing English does not, that no
catalogue defines one identifier twice (JUCE would keep the last silently),
and that ``src/application/testcontrol`` uses none of this: the capture
harness matches on the state names and descriptions there, so they are a
contract and never translated.

Standard library only, so it runs on the same interpreter as the rest of
``tools/scripts/``.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]

SOURCE_SUFFIXES = (".cpp", ".h")

# Directories under src/ whose references do not count: tests exercise the
# mechanism with keys of their own, and the services are TypeScript.
EXCLUDED_DIRECTORIES = ("tests", "services")

# Text here is matched on by the capture harness, so it is never localised.
NEVER_LOCALISED_DIRECTORIES = ("application/testcontrol",)

PLURAL_CATEGORIES = ("zero", "one", "two", "few", "many", "other")
ENGLISH_PLURAL_FORMS = ("one", "other")

REFERENCE = re.compile(r"\bl10n::(text|format|plural|key|pluralKey)\s*\(\s*\"((?:[^\"\\]|\\.)*)\"")
LOCALISATION_USE = re.compile(r"\bl10n::|#\s*include\s*[<\"][^>\"]*localisation/Localisation\.h[>\"]")


def display(path: Path) -> str:
    try:
        return str(path.relative_to(REPOSITORY_ROOT))
    except ValueError:
        return str(path)


def without_comments(text: str) -> str:
    """``text`` with every C++ comment blanked and every string kept.

    Line breaks inside a comment are kept, so a position still has its line.
    """
    result: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        character = text[index]
        following = text[index + 1] if index + 1 < length else ""
        if character == "/" and following == "/":
            end = text.find("\n", index)
            end = length if end == -1 else end
            result.append(" " * (end - index))
            index = end
        elif character == "/" and following == "*":
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            result.append("".join("\n" if c == "\n" else " " for c in text[index:end]))
            index = end
        elif character == "R" and following == '"' and (index == 0 or not (text[index - 1].isalnum() or text[index - 1] == "_")):
            opening = text.find("(", index + 2)
            if opening == -1:
                result.append(text[index:])
                break
            delimiter = text[index + 2 : opening]
            end = text.find(")" + delimiter + '"', opening)
            end = length if end == -1 else end + len(delimiter) + 2
            result.append(text[index:end])
            index = end
        elif character in "\"'":
            end = index + 1
            while end < length and text[end] != character and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            end = min(end + 1, length)
            result.append(text[index:end])
            index = end
        else:
            result.append(character)
            index += 1
    return "".join(result)


@dataclass
class References:
    """The identifiers the code references, each with where it was first seen."""

    plain: dict[str, str] = field(default_factory=dict)
    plural: dict[str, str] = field(default_factory=dict)


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def source_files(source_root: Path) -> list[Path]:
    files = []
    for path in sorted(source_root.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(source_root)
        if relative.parts and relative.parts[0] in EXCLUDED_DIRECTORIES:
            continue
        if "node_modules" in relative.parts:
            continue
        files.append(path)
    return files


def find_references(source_root: Path) -> References:
    references = References()
    for path in source_files(source_root):
        text = without_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in REFERENCE.finditer(text):
            kind, key = match.group(1), match.group(2)
            where = f"{display(path)}:{line_of(text, match.start())}"
            target = references.plural if kind in ("plural", "pluralKey") else references.plain
            target.setdefault(key, where)
    return references


def find_never_localised_uses(source_root: Path) -> list[str]:
    problems = []
    for directory in NEVER_LOCALISED_DIRECTORIES:
        root = source_root / directory
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            text = without_comments(path.read_text(encoding="utf-8", errors="replace"))
            match = LOCALISATION_USE.search(text)
            if match:
                problems.append(
                    f"{display(path)}:{line_of(text, match.start())} localises text in "
                    f"src/{directory}, which the capture harness matches on. Its state "
                    f"names and descriptions are a contract, not interface text."
                )
    return problems


def close_quote(line: str, start: int) -> int:
    """Where JUCE's parser ends a quoted string that starts at ``start``."""
    previous = ""
    index = start
    while index < len(line):
        if line[index] == '"' and previous != "\\":
            return index
        previous = line[index]
        index += 1
    return index


@dataclass
class Catalogue:
    path: Path
    keys: dict[str, int] = field(default_factory=dict)
    duplicates: list[tuple[str, int]] = field(default_factory=list)


def read_catalogue(path: Path) -> Catalogue:
    """The identifiers a catalogue defines, read as JUCE's LocalisedStrings reads them."""
    catalogue = Catalogue(path)
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line.startswith('"'):
            continue
        end = close_quote(line, 1)
        key = line[1:end].replace('\\"', '"')
        if not key:
            continue
        if key in catalogue.keys:
            catalogue.duplicates.append((key, number))
        else:
            catalogue.keys[key] = number
    return catalogue


def plural_base(key: str) -> str | None:
    base, _, category = key.rpartition(".")
    return base if base and category in PLURAL_CATEGORIES else None


def find_problems(source_root: Path, catalogue_directory: Path) -> list[str]:
    english_path = catalogue_directory / "en.txt"
    if not english_path.is_file():
        return [f"No English catalogue at {display(english_path)}"]

    english = read_catalogue(english_path)
    references = find_references(source_root)
    problems: list[str] = []

    for key, where in sorted(references.plain.items()):
        if key not in english.keys:
            problems.append(f"{where} uses {key!r}, which {display(english_path)} does not define.")

    for key, where in sorted(references.plural.items()):
        for form in ENGLISH_PLURAL_FORMS:
            if f"{key}.{form}" not in english.keys:
                problems.append(
                    f"{where} counts with {key!r}, but {display(english_path)} has no "
                    f"{key}.{form!s}."
                )

    for key, number in english.keys.items():
        base = plural_base(key)
        if key in references.plain or (base is not None and base in references.plural):
            continue
        problems.append(
            f"{display(english_path)}:{number} defines {key!r}, which nothing in src/ uses. "
            f"Remove it, or reference it."
        )

    for path in sorted(catalogue_directory.glob("*.txt")):
        catalogue = english if path == english_path else read_catalogue(path)
        for key, number in catalogue.duplicates:
            problems.append(f"{display(path)}:{number} defines {key!r} a second time.")
        if path == english_path:
            continue
        for key, number in catalogue.keys.items():
            base = plural_base(key)
            translates_a_plural = base is not None and any(
                plural_base(english_key) == base for english_key in english.keys
            )
            if key not in english.keys and not translates_a_plural:
                problems.append(
                    f"{display(path)}:{number} translates {key!r}, which English does not define."
                )

    problems.extend(find_never_localised_uses(source_root))
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=REPOSITORY_ROOT / "src",
        help="Source tree to scan for references (defaults to the repository's src/).",
    )
    parser.add_argument(
        "--catalogue-directory",
        type=Path,
        default=REPOSITORY_ROOT / "tools" / "packaging" / "localisation",
        help="Directory holding en.txt and the translations.",
    )
    arguments = parser.parse_args(argv)

    problems = find_problems(arguments.source_root, arguments.catalogue_directory)
    if problems:
        print("The catalogues and the code disagree:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("Every identifier used is defined in English, and every one defined is used.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
