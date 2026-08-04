#!/usr/bin/env python3
"""History across runs — the reason the store exists at all.

A single run answers "is this build all right". The interesting questions are
the ones only a sequence answers: is presentation regressing, is launch slower
than it was in June, did that optimisation actually help.

**History lives in two places on purpose.**

- The **local store** has everything, immediately, including the run you are in
  the middle of.
- A **git-tracked directory of one JSON file per run** is how that history
  leaves the machine. Text, one file per run, so two machines committing
  different runs merge without conflict — which a shared SQLite file could never
  do. Pull the repository and another machine's runs are simply there.

`collect` reads both and merges them by run key, so a graph shows synced history
plus whatever is local and not yet synced, without double-counting the overlap.

Standard library only.
"""

from __future__ import annotations

import json
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]

# Under docs/ beside the run records, because it is the same kind of thing: the
# durable, readable, committed evidence of what was verified and measured.
DEFAULT_HISTORY_DIRECTORY = REPOSITORY_ROOT / "docs" / "development" / "quality" / "run-history"

PASS = "pass"
FAIL = "fail"
SKIP = "skip"


def run_key(machine_identity: str, started_at: str, commit: str) -> str:
    """What makes two records the same run.

    Machine plus start time plus commit: a run cannot start twice at the same
    second on the same machine, and including the commit keeps a re-run of the
    same build distinguishable in the file name.
    """
    return f"{machine_identity}:{started_at}:{commit}"


def _entry_from_store(store, row) -> dict:
    run_id = int(row["id"])
    machine = store.machine(int(row["machine_id"]))
    verdicts = store.verdicts_for_run(run_id)
    captures = store.captures(run_id)
    counted = {PASS: 0, FAIL: 0, SKIP: 0}

    for verdict in verdicts:
        if verdict["verdict"] in counted:
            counted[verdict["verdict"]] += 1

    return {
        "key": run_key(machine["identity"], row["started_at"], row["commit_hash"]),
        "run_id": run_id,
        "started_at": row["started_at"],
        "commit": row["commit_hash"],
        "mode": row["mode"],
        "complete": bool(row["complete"]),
        "machine": machine["identity"],
        "machine_description": f"{machine['processor']} · {machine['display']}",
        "passed": counted[PASS],
        "failed": counted[FAIL],
        "skipped": counted[SKIP],
        "captures": len(captures),
        "capture_failures": sum(1 for capture in captures if capture.failure),
        "measurements": [
            {
                "metric": entry["metric"],
                "value": float(entry["value"]),
                "unit": entry["unit"],
                "scenario": entry["scenario"],
            }
            for entry in store.measurements(run_id)
        ],
        "suites": [
            {
                "suite": entry["suite"],
                "cases": int(entry["cases"]),
                "failures": int(entry["failures"]),
                "duration_seconds": float(entry["duration_seconds"]),
            }
            for entry in store.test_results(run_id)
        ],
        "source": "local",
    }


def pass_percent(entry: dict) -> float | None:
    """Share of answered questions that passed, or None when nothing was scored.

    Skips are deliberately in the denominator: an area nobody examined is not a
    pass, and letting skips vanish would make a barely-reviewed run look
    perfect.
    """
    answered = entry["passed"] + entry["failed"] + entry["skipped"]

    return round(100.0 * entry["passed"] / answered, 1) if answered else None


def write_run(entry: dict, directory: Path) -> Path:
    """One run, one file — which is what makes this mergeable."""
    directory.mkdir(parents=True, exist_ok=True)
    stamp = entry["started_at"].replace(":", "").replace("-", "").replace("T", "-")
    path = directory / f"{stamp}-{entry['machine'][:8]}.json"
    shareable = {key: value for key, value in entry.items() if key not in ("run_id", "source")}
    path.write_text(json.dumps(shareable, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    return path


def read_directory(directory: Path) -> dict[str, dict]:
    entries: dict[str, dict] = {}

    if not directory.is_dir():
        return entries

    for path in sorted(directory.glob("*.json")):
        try:
            entry = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue

        if isinstance(entry, dict) and entry.get("key"):
            entry["source"] = "synced"
            entries[entry["key"]] = entry

    return entries


def collect(store, directory: Path | None = None) -> list[dict]:
    """Every run known here, oldest first, from git and from this machine."""
    directory = directory if directory is not None else DEFAULT_HISTORY_DIRECTORY
    entries = read_directory(directory)

    for row in store.runs():
        entry = _entry_from_store(store, row)
        # The local copy wins: it is the same run, and it may have gained
        # answers since it was last written out.
        entries[entry["key"]] = entry

    return sorted(entries.values(), key=lambda entry: entry["started_at"])


def sync(store, directory: Path | None = None) -> dict:
    """Write local runs into the git-tracked directory. Returns what changed."""
    directory = directory if directory is not None else DEFAULT_HISTORY_DIRECTORY
    existing = read_directory(directory)
    written: list[str] = []
    unchanged = 0

    for row in store.runs():
        entry = _entry_from_store(store, row)
        previous = existing.get(entry["key"])
        shareable = {key: value for key, value in entry.items() if key not in ("run_id", "source")}

        if previous is not None:
            comparable = {key: value for key, value in previous.items() if key != "source"}

            if comparable == shareable:
                unchanged += 1

                continue

        written.append(str(write_run(entry, directory)))

    return {
        "written": written,
        "unchanged": unchanged,
        "directory": str(directory),
        "total": len(read_directory(directory)),
    }


def series(entries: list[dict], machine: str = "") -> dict:
    """The shapes the graphs draw.

    Filtered to one machine by default and never merged across machines: a
    launch time from another processor is not a point on this machine's line,
    and plotting them together would make every trend meaningless.
    """
    chosen = [entry for entry in entries if not machine or entry["machine"] == machine]

    quality = [
        {
            "started_at": entry["started_at"],
            "commit": entry["commit"],
            "mode": entry["mode"],
            "pass_percent": pass_percent(entry),
            "passed": entry["passed"],
            "failed": entry["failed"],
            "skipped": entry["skipped"],
            "capture_failures": entry["capture_failures"],
        }
        for entry in chosen
        if pass_percent(entry) is not None
    ]

    metrics: dict[str, dict] = {}

    for entry in chosen:
        for measurement in entry["measurements"]:
            metric = metrics.setdefault(
                measurement["metric"], {"metric": measurement["metric"], "unit": measurement["unit"], "points": []}
            )
            metric["points"].append(
                {
                    "started_at": entry["started_at"],
                    "commit": entry["commit"],
                    "value": measurement["value"],
                }
            )

    suites: dict[str, dict] = {}

    for entry in chosen:
        for result in entry["suites"]:
            suite = suites.setdefault(result["suite"], {"suite": result["suite"], "points": []})
            suite["points"].append(
                {
                    "started_at": entry["started_at"],
                    "commit": entry["commit"],
                    "cases": result["cases"],
                    "failures": result["failures"],
                    "duration_seconds": result["duration_seconds"],
                }
            )

    machines = sorted(
        {
            (entry["machine"], entry.get("machine_description", ""))
            for entry in entries
        }
    )

    return {
        "machine": machine,
        "machines": [
            {"identity": identity, "description": description} for identity, description in machines
        ],
        "runs": quality,
        # Metrics with a single point cannot show a trend, but they are still
        # the first point of one, so they are kept and drawn as a lone marker.
        "metrics": sorted(metrics.values(), key=lambda entry: entry["metric"]),
        "suites": sorted(suites.values(), key=lambda entry: entry["suite"]),
        "synced": sum(1 for entry in entries if entry.get("source") == "synced"),
        "local_only": sum(1 for entry in entries if entry.get("source") == "local"),
    }


def trend(points: list[dict], key: str = "value") -> dict:
    """Latest against the previous point, and against the best and worst seen."""
    values = [point[key] for point in points if point.get(key) is not None]

    if not values:
        return {}

    latest = values[-1]
    previous = values[-2] if len(values) > 1 else None

    return {
        "latest": latest,
        "previous": previous,
        "delta": None if previous is None else round(latest - previous, 4),
        "best": min(values),
        "worst": max(values),
        "count": len(values),
    }
