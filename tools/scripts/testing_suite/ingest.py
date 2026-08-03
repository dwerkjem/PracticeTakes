#!/usr/bin/env python3
"""Folding evidence produced elsewhere into a run.

The suite does not measure performance and does not run the automated suites.
The Performance Lab measures; `ctest`, `vitest`, and `run_tests.py` run. What
this file does is attach their output to the run that a reviewer looked at, so
one record answers "what state was this build in" rather than three.

Ingestion is all-or-nothing. A half-ingested export leaves a run looking like it
measured less than it did, which is a worse outcome than a failed ingest that
says why.

Standard library only.
"""

from __future__ import annotations

import json
from pathlib import Path
import re
import xml.etree.ElementTree as ElementTree

from store import Store, StoreError

PERFORMANCE_LAB = "performance-lab"


class IngestError(RuntimeError):
    """The file could not be read as what it claimed to be."""


def measurements_from(document: dict) -> list[dict]:
    """The measurement list inside a Performance Lab export.

    Tolerant about where the list lives — top level, under "measurements", or
    under "results" — and strict about what a measurement is. A metric with no
    value is a malformed export, not a measurement of zero.
    """
    candidates = document

    for key in ("measurements", "results", "metrics"):
        if isinstance(document, dict) and isinstance(document.get(key), list):
            candidates = document[key]

            break

    if not isinstance(candidates, list):
        raise IngestError("the export contains no measurement list")

    measurements: list[dict] = []

    for entry in candidates:
        if not isinstance(entry, dict):
            raise IngestError(f"a measurement is not an object: {entry!r}")

        metric = entry.get("metric") or entry.get("name")
        value = entry.get("value") if "value" in entry else entry.get("mean")

        if not metric:
            raise IngestError(f"a measurement has no metric name: {entry!r}")

        if value is None:
            raise IngestError(f"'{metric}' has no value")

        try:
            numeric = float(value)
        except (TypeError, ValueError) as error:
            raise IngestError(f"'{metric}' has a non-numeric value {value!r}") from error

        measurements.append(
            {
                "metric": str(metric),
                "value": numeric,
                "unit": str(entry.get("unit", "")),
                "scenario": str(entry.get("scenario", entry.get("strategy", ""))),
            }
        )

    if not measurements:
        raise IngestError("the export contains no measurements")

    return measurements


def performance_export(store: Store, run_id: int, path: Path) -> int:
    """Ingest a Performance Lab export. Returns how many measurements landed."""
    try:
        document = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise IngestError(f"could not read {path}: {error}") from error

    measurements = measurements_from(document)

    try:
        return store.record_measurements(run_id, measurements, PERFORMANCE_LAB)
    except StoreError as error:
        raise IngestError(str(error)) from error


def ctest_summary(text: str) -> dict:
    """Cases, failures, and seconds out of `ctest --output-on-failure` output."""
    passed = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", text)
    seconds = re.search(r"Total Test time \(real\) =\s+([\d.]+) sec", text)

    if not passed:
        raise IngestError("that does not look like ctest output")

    return {
        "cases": int(passed.group(3)),
        "failures": int(passed.group(2)),
        "duration_seconds": float(seconds.group(1)) if seconds else 0.0,
    }


def junit_summary(path: Path) -> dict:
    """Cases, failures, and seconds out of a JUnit XML report."""
    try:
        root = ElementTree.parse(path).getroot()
    except (OSError, ElementTree.ParseError) as error:
        raise IngestError(f"could not read {path}: {error}") from error

    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))

    if not suites:
        raise IngestError(f"{path} contains no test suites")

    return {
        "cases": sum(int(suite.get("tests", 0)) for suite in suites),
        "failures": sum(
            int(suite.get("failures", 0)) + int(suite.get("errors", 0)) for suite in suites
        ),
        "duration_seconds": sum(float(suite.get("time", 0) or 0) for suite in suites),
    }


def test_result(
    store: Store,
    run_id: int,
    *,
    suite: str,
    path: Path | None = None,
    text: str = "",
    cases: int | None = None,
    failures: int | None = None,
    duration_seconds: float = 0.0,
) -> dict:
    """Record one automated suite's outcome, from a report or from counts."""
    if path is not None:
        summary = junit_summary(Path(path)) if Path(path).suffix == ".xml" else ctest_summary(
            Path(path).read_text(encoding="utf-8", errors="replace")
        )
    elif text:
        summary = ctest_summary(text)
    elif cases is not None and failures is not None:
        summary = {"cases": cases, "failures": failures, "duration_seconds": duration_seconds}
    else:
        raise IngestError("give a report to read or the counts to record")

    store.record_test_result(
        run_id,
        suite=suite,
        cases=summary["cases"],
        failures=summary["failures"],
        duration_seconds=summary["duration_seconds"],
        summary=str(path) if path else "",
    )

    return summary
