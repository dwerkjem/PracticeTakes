#!/usr/bin/env python3
"""Turning a stored run into the record the release gate reads.

The store is local history. This file is the contract: JSON and Markdown under
docs/development/quality/manual-runs/, in the shape
tools/scripts/release/check_manual_verification.py already understands, sitting
in the same directory as every record the retired terminal harness wrote.

Two rules the export exists to enforce:

- **A run is complete only when nothing is outstanding.** Unscored captures and
  unanswered attended questions both make it incomplete, and the record names
  them. An unfinished review that exported as complete would read as a run in
  which the remaining surfaces passed.
- **Tags and comments are additional detail, never a verdict.** They export
  beside the three axes, so a record stays comparable against runs made before
  tagging existed.

Standard library only.
"""

from __future__ import annotations

import json
from pathlib import Path

import machine as machine_facts
import record
import review
from store import Store

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_RECORDS_DIRECTORY = REPOSITORY_ROOT / record.DEFAULT_RECORDS_DIRECTORY


def build(store: Store, run_id: int) -> record.Run:
    """The record for one stored run, as it will be written."""
    stored = store.run(run_id)
    stored_machine = store.machine(int(stored["machine_id"]))
    resolutions = _resolutions(stored["resolutions"])

    run = record.Run(
        commit=stored["commit_hash"],
        platform=stored["platform"],
        audio_device=stored["audio_device"],
        mode=stored["mode"],
        geometry_sweep=len(resolutions) > 1,
        started_at=stored["started_at"],
        finished_at=stored["finished_at"],
        resolutions=resolutions,
    )

    run.machine = {
        "identity": stored_machine["identity"],
        "description": machine_facts.describe(
            {
                "processor": stored_machine["processor"],
                "cores": stored_machine["cores"],
                "memory_bytes": stored_machine["memory_bytes"],
                "graphics": stored_machine["graphics"],
                "operating_system": stored_machine["operating_system"],
                "display": stored_machine["display"],
            }
        ),
    }

    for capture in store.captures(run_id):
        if capture.failure:
            # A surface nobody could look at is a finding, not an absence — the
            # same rule the terminal harness applied to a surface it could not
            # reach.
            run.unreachable.append(
                {
                    "surface": capture.surface_title,
                    "state": capture.surface_state,
                    "geometry": capture.geometry,
                    "reason": capture.failure,
                }
            )

        for row in store.verdicts(capture.id):
            run.add(
                record.Answer(
                    surface=capture.surface_title,
                    state=capture.surface_state,
                    geometry=capture.geometry,
                    question=row["question"],
                    prompt=row["prompt"],
                    verdict=row["verdict"],
                    note=row["note"],
                )
            )

        tags = store.tags_for(capture.id)
        comments = [comment["body"] for comment in store.comments_for(capture.id)]

        if tags or comments:
            run.image_notes.append(
                {
                    "surface": capture.surface_title,
                    "state": capture.surface_state,
                    "geometry": capture.geometry,
                    "tags": tags,
                    "comments": comments,
                }
            )

    run.measurements = [
        {
            "metric": row["metric"],
            "value": row["value"],
            "unit": row["unit"],
            "scenario": row["scenario"],
            "source": row["source"],
        }
        for row in store.measurements(run_id)
    ]
    run.test_results = [
        {
            "suite": row["suite"],
            "cases": row["cases"],
            "failures": row["failures"],
            "duration_seconds": row["duration_seconds"],
            "summary": row["summary"],
        }
        for row in store.test_results(run_id)
    ]

    outstanding = review.outstanding(store, run_id)
    run.unanswered = [
        {
            "surface": entry["surface"],
            "geometry": entry["geometry"],
            "question": entry["question"],
            "prompt": entry["prompt"],
            "attended": entry["attended"],
        }
        for entry in outstanding
    ]

    captures = store.captures(run_id)
    run.complete = bool(captures) and not outstanding

    return run


def problems(run: record.Run) -> list[str]:
    """What stops this run being written at all.

    Only one thing does: a failure with no note. It says something is wrong
    without saying what, so the surface has to be looked at again to learn
    anything — and by then the reviewer has closed the grid.
    """
    return run.problems()


def write(store: Store, run_id: int, directory: Path | None = None) -> tuple[Path, Path]:
    """Export one run. Raises when the run cannot honestly be written."""
    run = build(store, run_id)
    found = problems(run)

    if found:
        raise ValueError("\n".join(found))

    store.finish_run(run_id, complete=run.complete)

    return record.write(run, directory or DEFAULT_RECORDS_DIRECTORY)


def _resolutions(raw: str) -> list[str]:
    try:
        parsed = json.loads(raw)
    except (TypeError, ValueError):
        return []

    return [str(entry) for entry in parsed] if isinstance(parsed, list) else []
