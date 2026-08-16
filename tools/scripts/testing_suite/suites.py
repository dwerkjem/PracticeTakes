#!/usr/bin/env python3
"""Everything the hub can run, as data.

One entry per suite: what to call it, what command runs it, what has to exist
first, and how to read a result out of the output. Adding a suite is an edit to
this list — the hub, the runner, and the page all work from it and none of them
name a suite individually.

The parsers are deliberately forgiving about *format* and strict about
*verdict*: an exit code is always trustworthy, while a count scraped out of
human-readable output may not be there at all. So a suite whose numbers cannot
be read still reports pass or fail correctly, with its counts left at zero,
rather than reporting success because a regular expression missed.

Standard library only.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
import re

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]

# What a suite is for. The hub groups by this, and "run everything of this kind"
# is the common request -- "run the tests", "run the performance work".
UNIT = "tests"
PERFORMANCE = "performance"
INTERFACE = "ui"

SAFETY = "safety"

KINDS = (UNIT, PERFORMANCE, INTERFACE, SAFETY)


@dataclass(frozen=True)
class Suite:
    id: str
    label: str
    kind: str
    description: str

    # Run from the repository root. A "{Target}" argument is replaced with
    # wherever that build target actually ended up, since CMake does not put
    # every target in the same directory. Empty for a suite the runner handles
    # itself (the UI capture, which is a pass rather than a command).
    command: tuple[str, ...] = ()
    working_directory: str = "."

    # Built targets this suite needs before it can run. The runner builds them
    # rather than failing with "no such file".
    needs: tuple[str, ...] = ()

    # A one-off preparation step: run `prepare` when `prepare_when_missing` is
    # not there. Dependencies nobody installed are the same class of problem as
    # a binary nobody built -- the hub does it rather than reporting a test
    # failure that is nothing of the kind.
    prepare: tuple[str, ...] = ()
    prepare_when_missing: str = ""

    # Reads counts out of the output. Never decides pass or fail -- the exit
    # code does that.
    parser: Callable[[str], dict] | None = None

    # A suite that needs a real display and a real audio device. Marked so the
    # page can say so rather than letting it fail confusingly on a headless box.
    needs_display: bool = False

    tags: tuple[str, ...] = field(default_factory=tuple)

    # Added to the environment this suite runs in. The sanitizers are configured
    # this way and nowhere else: their options belong beside the command that
    # needs them rather than in a shell profile somebody has to know about.
    environment: dict = field(default_factory=dict)


# --- Parsers ----------------------------------------------------------------


def parse_ctest(output: str) -> dict:
    match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", output)
    seconds = re.search(r"Total Test time \(real\) =\s+([\d.]+) sec", output)

    if not match:
        return {}

    return {
        "cases": int(match.group(3)),
        "failures": int(match.group(2)),
        "duration_seconds": float(seconds.group(1)) if seconds else 0.0,
    }


def parse_unittest(output: str) -> dict:
    match = re.search(r"Ran (\d+) tests? in ([\d.]+)s", output)

    if not match:
        return {}

    failures = re.search(r"FAILED \((.*)\)", output)
    count = 0

    if failures:
        count = sum(int(value) for value in re.findall(r"=(\d+)", failures.group(1)))

    return {
        "cases": int(match.group(1)),
        "failures": count,
        "duration_seconds": float(match.group(2)),
    }


def parse_vitest(output: str) -> dict:
    tests = re.search(r"Tests\s+(?:(\d+) failed[ |]+)?(\d+) passed\s+\((\d+)\)", output)

    if not tests:
        return {}

    return {
        "cases": int(tests.group(3)),
        "failures": int(tests.group(1) or 0),
        "duration_seconds": 0.0,
    }


def parse_catch2(output: str) -> dict:
    """Catch2's own summary line. Counts only -- the exit code is the verdict.

    A sanitizer report is not in this line at all: it goes to stderr and turns
    the exit code non-zero, which is what decides pass or fail. Reading counts
    that say "all passed" from a run the sanitizer killed is exactly why counts
    never decide anything here.
    """
    match = re.search(
        r"(?:assertions|test cases):\s+(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed",
        output,
    )

    if match:
        return {"cases": int(match.group(1)), "failures": int(match.group(3))}

    passing = re.search(r"All tests passed \((\d+) assertions? in (\d+) test cases?\)", output)

    if passing:
        return {"cases": int(passing.group(2)), "failures": 0}

    return {}


def parse_catch2_benchmarks(output: str) -> dict:
    """Catch2 benchmark output into measurements.

    Its table puts the name on one line and mean/low/high on the next, so the
    mean is taken positionally. Anything that does not match is skipped rather
    than guessed at -- a wrong number here would be stored as evidence.
    """
    measurements: list[dict] = []
    lines = output.splitlines()

    for index, line in enumerate(lines):
        heading = re.match(r"^(\S.*?)\s{2,}\d+\s+\d+\s+[\d.]+\s*\w*s?\s*$", line)

        if not heading:
            continue

        for follower in lines[index + 1 : index + 3]:
            values = re.findall(r"([\d.]+)\s*(ns|us|ms|s)\b", follower)

            if len(values) >= 3:
                amount, unit = values[0]
                measurements.append(
                    {
                        "metric": heading.group(1).strip(),
                        "value": float(amount),
                        "unit": unit,
                        "scenario": "catch2-benchmark",
                    }
                )

                break

    return {"measurements": measurements, "cases": len(measurements), "failures": 0}


# --- The list ---------------------------------------------------------------

SUITES: tuple[Suite, ...] = (
    Suite(
        id="cpp",
        label="C++ unit tests",
        kind=UNIT,
        description="PracticeTakesTests through ctest — the bulk of the automated coverage.",
        command=("ctest", "--test-dir", "build", "--output-on-failure"),
        needs=("PracticeTakesTests",),
        parser=parse_ctest,
    ),
    Suite(
        id="python",
        label="Python script tests",
        kind=UNIT,
        description="Every test_*.py under tools/scripts/, including this suite's own.",
        command=("python3", "tools/scripts/run_tests.py"),
        parser=parse_unittest,
    ),
    Suite(
        id="services",
        label="Service tests",
        kind=UNIT,
        description="The feedback-intake worker: tsc --noEmit, then vitest.",
        command=("npm", "run", "test"),
        working_directory="src/services",
        prepare=("npm", "ci"),
        prepare_when_missing="src/services/node_modules",
        parser=parse_vitest,
    ),
    Suite(
        id="services-types",
        label="Service type check",
        kind=UNIT,
        description="tsc --noEmit across every workspace.",
        command=("npm", "run", "check"),
        working_directory="src/services",
        prepare=("npm", "ci"),
        prepare_when_missing="src/services/node_modules",
    ),
    Suite(
        id="smoke",
        label="Smoke test",
        kind=UNIT,
        description="Launches the real application: does it start, open a tool, and exit cleanly?",
        command=("python3", "tools/scripts/quality/smoke_test.py"),
        needs=("PracticeTakes",),
        needs_display=True,
        parser=parse_unittest,
    ),
    Suite(
        id="benchmarks",
        label="Benchmarks",
        kind=PERFORMANCE,
        description="The opt-in [.benchmark] Catch2 cases — analysis throughput and FIFO cost.",
        command=("{PracticeTakesTests}", "[.benchmark]"),
        needs=("PracticeTakesTests",),
        parser=parse_catch2_benchmarks,
    ),
    Suite(
        id="ui-golden",
        label="UI golden images",
        kind=INTERFACE,
        description="Reference screenshots and fresh-launch timings against them.",
        command=("tools/scripts/quality/ui-validation/run-ui-golden.zsh",),
        needs=("PracticeTakes",),
        needs_display=True,
    ),
    Suite(
        id="ui-capture",
        label="UI capture",
        kind=INTERFACE,
        description="Photograph every surface at every resolution, for the review grid.",
        needs=("PracticeTakes",),
        needs_display=True,
    ),

    # --- The sanitizers -------------------------------------------------------
    #
    # These run in CI and never ran here, which is the wrong way round: they are
    # slow, they need their own build tree, and finding out on a pull request
    # that a change races is finding out an hour after writing it.
    #
    # Each is the same suite CI runs, with the same options, deliberately: two
    # definitions of "the race check" that drift is worse than one that is
    # occasionally inconvenient. See .github/workflows/sanitizers.yml and
    # sanitizers-scheduled.yml.
    Suite(
        id="asan",
        label="Memory errors and leaks",
        kind=SAFETY,
        description=(
            "The whole C++ suite under AddressSanitizer and UBSan. Catches "
            "use-after-free, overflows, undefined behaviour, and anything the "
            "tests allocate and never release."
        ),
        command=("ctest", "--test-dir", "build-asan", "--output-on-failure"),
        needs=("PracticeTakesTests-asan",),
        parser=parse_ctest,
        environment={
            # halt_on_error keeps the first report as the one a reader sees;
            # without it a cascade of consequences buries the cause.
            "ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1",
            "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
        },
    ),
    Suite(
        id="tsan",
        label="Race conditions",
        kind=SAFETY,
        description=(
            "The concurrency cases under ThreadSanitizer. Only [.load]: the "
            "rest of the suite is single-threaded, so the slowdown would be "
            "spent observing code that cannot race."
        ),
        command=("{PracticeTakesTests-tsan}", "[.load]", "--order", "lex"),
        needs=("PracticeTakesTests-tsan",),
        parser=parse_catch2,
        environment={
            "TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1",
            "PRACTICE_TAKES_SOAK_MILLISECONDS": "1500",
        },
    ),
    Suite(
        id="rtsan",
        label="Audio callback is non-blocking",
        kind=SAFETY,
        description=(
            "The [callback] cases under RealtimeSanitizer, which fails on an "
            "allocation, a lock, or a blocking call inside the audio callback. "
            "Needs Clang; the annotation is a Clang attribute."
        ),
        command=("{PracticeTakesTests-rtsan}", "[callback]", "--order", "lex"),
        needs=("PracticeTakesTests-rtsan",),
        parser=parse_catch2,
        environment={"RTSAN_OPTIONS": "halt_on_error=1"},
    ),
)


def by_id(suite_id: str) -> Suite | None:
    return next((suite for suite in SUITES if suite.id == suite_id), None)


def of_kind(kind: str) -> tuple[Suite, ...]:
    return tuple(suite for suite in SUITES if suite.kind == kind)


def validate() -> list[str]:
    """Problems with the list itself, empty when it is coherent."""
    problems: list[str] = []
    seen: set[str] = set()

    for suite in SUITES:
        if suite.id in seen:
            problems.append(f"duplicate suite id '{suite.id}'")

        seen.add(suite.id)

        if suite.kind not in KINDS:
            problems.append(f"'{suite.id}' has unknown kind '{suite.kind}'")

        if not suite.command and suite.id != "ui-capture":
            problems.append(f"'{suite.id}' has no command and is not handled by the runner")

        if suite.working_directory != "." and not (REPOSITORY_ROOT / suite.working_directory).is_dir():
            problems.append(f"'{suite.id}' runs in {suite.working_directory}, which does not exist")

        if bool(suite.prepare) != bool(suite.prepare_when_missing):
            problems.append(
                f"'{suite.id}' needs both a preparation command and the path that triggers it"
            )

    return problems
