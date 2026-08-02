#!/usr/bin/env python3
"""Measure C++ coverage and write a report.

Configures a dedicated coverage build tree, runs ``PracticeTakesTests`` under
gcov instrumentation, and produces both a human summary and machine-readable
output.

The build tree is separate on purpose. Instrumented objects are not
interchangeable with ordinary ones, so sharing a tree means every switch between
coverage and normal builds is a full rebuild anyway; and ``ccache`` interacts
badly with coverage counters, so it is disabled here.

The report has two halves. ``gcovr`` measures the files compiled into the test
binary. ``coverage_sources.py`` supplies the other half — the files that are not
compiled into it at all, and so cannot be measured by any coverage tool. Without
that second half the percentage is computed over whatever happens to be in the
test target, which today is 25 of 43 translation units.

Coverage is informational: this script does not fail on a low number, and there
is no threshold anywhere.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]

sys.path.insert(0, str(Path(__file__).resolve().parent))

from coverage_sources import classify, summarise  # noqa: E402

DEFAULT_BUILD_DIR = REPOSITORY_ROOT / "build-coverage"
DEFAULT_OUTPUT_DIR = REPOSITORY_ROOT / "build" / "coverage"


def run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[bytes]:
    print(f"$ {' '.join(command)}", flush=True)

    return subprocess.run(command, check=True, **kwargs)  # type: ignore[arg-type]


def configure(build_dir: Path, cmake: str) -> None:
    run(
        [
            cmake,
            "-S",
            str(REPOSITORY_ROOT),
            "-B",
            str(build_dir),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DBUILD_TESTING=ON",
            "-DPRACTICE_TAKES_ENABLE_COVERAGE=ON",
            # ccache and coverage counters do not mix.
            "-DCMAKE_C_COMPILER_LAUNCHER=",
            "-DCMAKE_CXX_COMPILER_LAUNCHER=",
        ]
    )


def build(build_dir: Path, cmake: str) -> None:
    run([cmake, "--build", str(build_dir), "--target", "PracticeTakesTests", "--parallel"])


def run_tests(build_dir: Path) -> None:
    binary = build_dir / "PracticeTakesTests"

    if not binary.is_file():
        binary = build_dir / "bin" / "PracticeTakesTests"

    if not binary.is_file():
        raise FileNotFoundError(f"No PracticeTakesTests binary under {build_dir}")

    # Counters are written even when a test fails, and a failing suite is still
    # worth a report -- the run that produced it is what CI reports separately.
    subprocess.run([str(binary)], check=False)


def run_gcovr(build_dir: Path, output_dir: Path) -> bool:
    """Produce the gcovr reports. False when gcovr is not installed."""
    gcovr = shutil.which("gcovr")

    if gcovr is None:
        return False

    output_dir.mkdir(parents=True, exist_ok=True)

    run(
        [
            gcovr,
            "--root",
            str(REPOSITORY_ROOT),
            "--filter",
            str(REPOSITORY_ROOT / "src"),
            # Everything fetched or generated is noise in a report about this
            # repository's own code.
            "--exclude",
            str(build_dir),
            # --filter and --exclude shape the report, but gcovr still parses
            # every .gcda it finds first. JUCE compiles its modules into the
            # test target, and gcov cannot resolve the generated Ragel sources
            # that HarfBuzz includes, which is a hard error rather than a
            # skipped file. Excluding the directory keeps those files from
            # being searched at all: same numbers, no error to suppress, and
            # roughly a third of the runtime.
            "--gcov-exclude-directories",
            ".*_deps.*",
            "--print-summary",
            "--xml",
            str(output_dir / "coverage.xml"),
            "--json-summary",
            str(output_dir / "coverage-summary.json"),
            "--html-details",
            str(output_dir / "coverage.html"),
            str(build_dir),
        ]
    )

    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Reuse an existing coverage tree instead of configuring and building.",
    )
    arguments = parser.parse_args(argv)

    if not arguments.skip_build:
        configure(arguments.build_dir, arguments.cmake)
        build(arguments.build_dir, arguments.cmake)
        run_tests(arguments.build_dir)

    arguments.output_dir.mkdir(parents=True, exist_ok=True)

    # The half no coverage tool can produce, written whether or not gcovr is
    # present -- it is the more important half.
    classified = classify(REPOSITORY_ROOT / "src", REPOSITORY_ROOT / "CMakeLists.txt")
    (arguments.output_dir / "test-build-membership.json").write_text(
        json.dumps(classified, indent=2), encoding="utf-8"
    )

    print()
    print(summarise(classified))
    print()

    if not run_gcovr(arguments.build_dir, arguments.output_dir):
        print(
            "gcovr is not installed, so no line-coverage report was produced. "
            "The test-build membership report above was still written to "
            f"{arguments.output_dir}.",
            file=sys.stderr,
        )

        # Not a failure. The membership half is the part that names the gap, and
        # coverage is informational either way.
        return 0

    print(f"\nReports written to {arguments.output_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
