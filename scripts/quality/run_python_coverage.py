#!/usr/bin/env python3
"""Measure coverage of the repository's Python scripts.

Runs ``scripts/run_tests.py`` under ``coverage`` and writes a machine-readable
report plus a summary.

``coverage`` is not a standard-library module and is deliberately not required.
The scripts that ``pre-commit`` invokes -- the two secrets-manager hooks and the
C++ formatter -- run on whatever interpreter a contributor happens to have, and
they stay standard-library only for that reason. This script is a reporting tool
that CI installs the dependency for; when it is absent, it says so and exits
successfully rather than failing a run over a report.

Coverage is informational. Nothing here applies a threshold.
"""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = REPOSITORY_ROOT / "build" / "coverage" / "python"


def coverage_is_available() -> bool:
    return importlib.util.find_spec("coverage") is not None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    arguments = parser.parse_args(argv)

    if not coverage_is_available():
        print(
            "The 'coverage' package is not installed, so no Python coverage "
            "report was produced. Install it with 'pip install coverage' to "
            "generate one locally; CI installs it for the published report.",
            file=sys.stderr,
        )

        return 0

    arguments.output_dir.mkdir(parents=True, exist_ok=True)

    data_file = arguments.output_dir / ".coverage"
    common = [sys.executable, "-m", "coverage"]

    # `scripts/` is the measured tree, and the test files themselves are
    # excluded: a test file is always fully executed by its own run, so
    # including them inflates the figure with a number that can only ever be
    # 100%.
    run = subprocess.run(
        [
            *common,
            "run",
            f"--data-file={data_file}",
            "--source",
            str(REPOSITORY_ROOT / "scripts"),
            "--omit",
            "*/test_*.py",
            str(REPOSITORY_ROOT / "scripts" / "run_tests.py"),
        ],
        cwd=REPOSITORY_ROOT,
        check=False,
    )

    if run.returncode != 0:
        # The suite failing is reported by the suite's own CI step. Counters are
        # still written, and a report from a failing run is still worth having.
        print(
            "The Python test suite failed; reporting coverage from that run anyway.",
            file=sys.stderr,
        )

    for report in (
        ["report", "--show-missing"],
        ["json", "-o", str(arguments.output_dir / "coverage.json")],
        ["xml", "-o", str(arguments.output_dir / "coverage.xml")],
    ):
        subprocess.run(
            [*common, *report, f"--data-file={data_file}"],
            cwd=REPOSITORY_ROOT,
            check=False,
        )

    print(f"\nReports written to {arguments.output_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
