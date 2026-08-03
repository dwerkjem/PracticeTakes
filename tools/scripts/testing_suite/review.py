#!/usr/bin/env python3
"""What the review does, separately from how it is displayed.

Every decision a review makes lives here: which questions a capture still owes
an answer to, what a tag applied to a selection means, whether a run is finished.
The browser fetches and renders; it decides nothing. That split is deliberate —
it is the same reason the retired terminal harness kept its sequencing out of
its renderer, and it is what makes the review testable without a display.

Standard library only.
"""

from __future__ import annotations

from pathlib import Path

import surfaces
from store import FAIL, Store, StoreError


def _question_lookup(state: str, title: str, attended: bool) -> tuple[surfaces.Question, ...]:
    surface = surfaces.find(state, title)

    if surface is None:
        # A capture from a surface list that has since changed. Its stored
        # answers stay readable; it simply asks nothing further.
        return ()

    return (
        surfaces.attended_questions(surface) if attended else surfaces.review_questions(surface)
    )


def questions_for_capture(state: str, title: str) -> tuple[surfaces.Question, ...]:
    """Everything this capture can be asked, whichever pass asks it."""
    return _question_lookup(state, title, attended=False) + _question_lookup(
        state, title, attended=True
    )


def attended_capture_ids(store: Store, run_id: int) -> dict[str, int]:
    """Which capture each surface's behavioural questions hang off.

    One capture per surface — the first one — rather than all of them. "Does the
    tuner respond to sound?" has the same answer at every window size, so asking
    it three times is friction that buys nothing, and a reviewer who answers it
    once should not see a run reported as incomplete.
    """
    chosen: dict[str, int] = {}

    for capture in store.captures(run_id):
        chosen.setdefault(capture.surface_title, capture.id)

    return chosen


def image_available(capture) -> str:
    """Why an image cannot be shown, or "" when it can.

    A capture with no image is never rendered as an empty tile: the reviewer has
    to be able to tell "this failed to capture" from "this looks fine" from
    "the pixels were pruned months ago".
    """
    if capture.failure:
        return "failed"

    if capture.pruned:
        return "pruned"

    if not capture.image_path or not Path(capture.image_path).exists():
        return "missing"

    return ""


def capture_view(store: Store, capture, *, include_attended: bool = True) -> dict:
    verdicts = {row["question"]: row for row in store.verdicts(capture.id)}
    attended = (
        _question_lookup(capture.surface_state, capture.surface_title, True)
        if include_attended
        else ()
    )
    questions = _question_lookup(capture.surface_state, capture.surface_title, False) + attended
    attended_ids = {question.id for question in attended}

    return {
        "id": capture.id,
        "surface": capture.surface_title,
        "state": capture.surface_state,
        "geometry": capture.geometry,
        "width": capture.width,
        "height": capture.height,
        "failure": capture.failure,
        "unavailable": image_available(capture),
        "tags": store.tags_for(capture.id),
        "comments": [row["body"] for row in store.comments_for(capture.id)],
        "questions": [
            {
                "id": question.id,
                "prompt": question.prompt,
                "attended": question.id in attended_ids,
                "verdict": verdicts[question.id]["verdict"] if question.id in verdicts else "",
                "note": verdicts[question.id]["note"] if question.id in verdicts else "",
            }
            for question in questions
        ],
    }


def run_view(store: Store, run_id: int) -> dict:
    """Everything the grid needs for one run, grouped as it is displayed."""
    run = store.run(run_id)
    machine = store.machine(int(run["machine_id"]))
    attended_on = attended_capture_ids(store, run_id)
    groups: list[dict] = []
    index: dict[str, dict] = {}

    for capture in store.captures(run_id):
        group = index.get(capture.surface_title)

        if group is None:
            group = {"surface": capture.surface_title, "state": capture.surface_state, "captures": []}
            index[capture.surface_title] = group
            groups.append(group)

        group["captures"].append(
            capture_view(
                store,
                capture,
                include_attended=attended_on.get(capture.surface_title) == capture.id,
            )
        )

    return {
        "run": {
            "id": run_id,
            "commit": run["commit_hash"],
            "mode": run["mode"],
            "started_at": run["started_at"],
            "finished_at": run["finished_at"],
            "complete": bool(run["complete"]),
            "resolutions": run["resolutions"],
            "audio_device": run["audio_device"],
        },
        "machine": {
            "processor": machine["processor"],
            "cores": machine["cores"],
            "graphics": machine["graphics"],
            "operating_system": machine["operating_system"],
            "display": machine["display"],
        },
        "groups": groups,
        "tags": [{"name": row["name"], "description": row["description"]} for row in store.tags()],
        "outstanding": outstanding(store, run_id),
    }


def outstanding(store: Store, run_id: int, *, attended: bool | None = None) -> list[dict]:
    """Questions this run still owes an answer to.

    `attended=None` means both kinds; True or False narrows it to one pass. A
    run with anything outstanding is incomplete, which is what stops an
    unfinished review from exporting as though the unscored surfaces passed.
    """
    pending: list[dict] = []
    attended_on = attended_capture_ids(store, run_id)

    for capture in store.captures(run_id):
        answered = {row["question"] for row in store.verdicts(capture.id)}

        for is_attended in (False, True):
            if attended is not None and attended != is_attended:
                continue

            if is_attended and attended_on.get(capture.surface_title) != capture.id:
                continue

            for question in _question_lookup(
                capture.surface_state, capture.surface_title, is_attended
            ):
                if question.id in answered:
                    continue

                pending.append(
                    {
                        "capture_id": capture.id,
                        "surface": capture.surface_title,
                        "state": capture.surface_state,
                        "geometry": capture.geometry,
                        "question": question.id,
                        "prompt": question.prompt,
                        "attended": is_attended,
                    }
                )

    return pending


def score(
    store: Store, capture_id: int, question_id: str, verdict: str, note: str = "", *, attended: bool = False
) -> list[str]:
    """Answer one question about one capture. Returns problems, storing nothing when there are any."""
    capture = store.capture(capture_id)

    if capture is None:
        return [f"No capture {capture_id}."]

    prompt = next(
        (
            question.prompt
            for question in questions_for_capture(capture.surface_state, capture.surface_title)
            if question.id == question_id
        ),
        "",
    )

    if not prompt:
        return [f"'{capture.surface_title}' does not ask '{question_id}'."]

    return store.record_verdict(
        capture_id,
        question=question_id,
        prompt=prompt,
        verdict=verdict,
        note=note,
        attended=attended,
    )


def score_many(
    store: Store,
    capture_ids: list[int],
    verdict: str,
    note: str = "",
    *,
    overwrite: bool = False,
) -> dict:
    """Apply one verdict to every reviewable question on every selected capture.

    The bulk case is the common one by far: most surfaces are untouched by most
    changes, you look at a row of them, nothing is wrong, and answering three
    axes each individually is friction for no information. The terminal harness
    had the same shortcut for the same reason.

    Only questions answerable from the image are touched — the attended ones
    need a live application, and passing them from a grid would be a claim
    nobody made. By default only unanswered questions are filled in, so a bulk
    pass cannot quietly overwrite a considered verdict; `overwrite` says to
    replace them anyway.
    """
    # Checked up front rather than per question: with everything already
    # answered, a note-less bulk fail would otherwise touch nothing, find no
    # problem to report, and come back as a success that changed nothing.
    if verdict == FAIL and not note.strip():
        return {"scored": 0, "left_alone": 0,
                "problems": ["A bulk failure needs a note saying what is wrong."]}

    scored = 0
    skipped = 0
    problems: list[str] = []

    for capture_id in capture_ids:
        capture = store.capture(capture_id)

        if capture is None:
            problems.append(f"No capture {capture_id}.")

            continue

        answered = {row["question"] for row in store.verdicts(capture_id)}

        for question in _question_lookup(capture.surface_state, capture.surface_title, False):
            if question.id in answered and not overwrite:
                skipped += 1

                continue

            found = store.record_verdict(
                capture_id,
                question=question.id,
                prompt=question.prompt,
                verdict=verdict,
                note=note,
            )

            if found:
                problems.extend(found)
            else:
                scored += 1

    return {"scored": scored, "left_alone": skipped, "problems": problems}


def apply_tag(store: Store, capture_ids: list[int], tag: str) -> dict:
    if not capture_ids:
        return {"tagged": 0}

    return {"tagged": store.apply_tag(capture_ids, tag)}


def remove_tag(store: Store, capture_ids: list[int], tag: str) -> dict:
    store.remove_tag(capture_ids, tag)

    return {"removed": len(capture_ids)}


def add_comment(store: Store, capture_id: int, body: str) -> dict:
    try:
        return {"id": store.add_comment(capture_id, body)}
    except StoreError as error:
        return {"error": str(error)}


def failures(store: Store, run_id: int) -> list[dict]:
    """Everything wrong with this run: failed captures and failed answers."""
    found: list[dict] = []

    for capture in store.captures(run_id):
        if capture.failure:
            found.append(
                {
                    "surface": capture.surface_title,
                    "geometry": capture.geometry,
                    "reason": capture.failure,
                    "kind": "capture",
                }
            )

        for row in store.verdicts(capture.id):
            if row["verdict"] == FAIL:
                found.append(
                    {
                        "surface": capture.surface_title,
                        "geometry": capture.geometry,
                        "question": row["question"],
                        "prompt": row["prompt"],
                        "note": row["note"],
                        "kind": "answer",
                    }
                )

    return found
