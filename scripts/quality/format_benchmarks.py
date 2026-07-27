#!/usr/bin/env python3
"""Format Catch2 XML benchmark results as a GitHub step summary markdown table.

Usage:
    format_benchmarks.py <benchmark.xml> [startup.json]

The output is written to stdout and is intended to be appended to
$GITHUB_STEP_SUMMARY inside a GitHub Actions workflow step.
"""

from __future__ import annotations

import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def ns_to_ms(value: float) -> float:
    return value / 1_000_000.0


def format_ms(value: float) -> str:
    if value < 1.0:
        return f"{value * 1000:.3f} µs"
    return f"{value:.3f} ms"


def parse_benchmarks(xml_path: Path) -> list[dict]:
    tree = ET.parse(xml_path)
    root = tree.getroot()

    results = []
    for bench in root.iter("BenchmarkResults"):
        name = bench.get("name", "unknown")
        mean_elem = bench.find("mean")
        stddev_elem = bench.find("standardDeviation")

        if mean_elem is None or stddev_elem is None:
            continue

        mean_ns = float(mean_elem.get("value", 0))
        stddev_ns = float(stddev_elem.get("value", 0))
        mean_ms = ns_to_ms(mean_ns)
        stddev_ms = ns_to_ms(stddev_ns)
        rel_pct = (stddev_ms / mean_ms * 100.0) if mean_ms > 0 else 0.0

        results.append(
            {
                "name": name,
                "mean_ms": mean_ms,
                "stddev_ms": stddev_ms,
                "rel_pct": rel_pct,
            }
        )

    return results


def render_benchmark_table(results: list[dict]) -> str:
    if not results:
        return "_No benchmark results found._\n"

    lines = [
        "| Benchmark | Mean | Stddev | Stddev % |",
        "| --- | ---: | ---: | ---: |",
    ]
    for r in results:
        lines.append(
            f"| {r['name']} "
            f"| {format_ms(r['mean_ms'])} "
            f"| {format_ms(r['stddev_ms'])} "
            f"| {r['rel_pct']:.1f}% |"
        )
    return "\n".join(lines) + "\n"


def render_startup_section(startup_path: Path) -> str:
    try:
        data = json.loads(startup_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return f"_Startup timing unavailable: {exc}_\n"

    launch_ms = data.get("launch_ms")
    exit_ms = data.get("exit_ms")

    lines = [
        "| Metric | Value |",
        "| --- | ---: |",
    ]
    if launch_ms is not None:
        lines.append(f"| Time to launch (process start → first output) | {launch_ms:.1f} ms |")
    if exit_ms is not None:
        lines.append(f"| Total startup lifetime (self-test exit) | {exit_ms:.1f} ms |")

    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(f"Usage: {argv[0]} <benchmark.xml> [startup.json]", file=sys.stderr)
        return 1

    xml_path = Path(argv[1])
    startup_path = Path(argv[2]) if len(argv) >= 3 else None

    if not xml_path.is_file():
        print(f"Error: {xml_path} not found", file=sys.stderr)
        return 1

    results = parse_benchmarks(xml_path)

    output_lines = [
        "## Performance benchmark results",
        "",
        "### DSP analysis pipelines",
        "",
        render_benchmark_table(results),
    ]

    if startup_path is not None and startup_path.is_file():
        output_lines += [
            "",
            "### Startup timing",
            "",
            render_startup_section(startup_path),
        ]

    output_lines += [
        "",
        "> Times are mean ± stddev over 100 samples. "
        "CI runner variance is expected; flag regressions of >2× the baseline mean.",
    ]

    print("\n".join(output_lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
