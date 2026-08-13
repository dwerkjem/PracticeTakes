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

import sqlite3

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


def verdict_of(questions: list[dict]) -> str:
    """One word for how a capture was judged.

    A failure anywhere wins: a surface with two passes and a fail is a failing
    surface, and a filter that called it "passed" would hide the thing worth
    looking at. Skips rank above passes for the same reason -- an area nobody
    examined is not a pass.
    """
    verdicts = {question["verdict"] for question in questions if question["verdict"]}

    if not verdicts:
        return "unanswered"

    if "fail" in verdicts:
        return "failed"

    if "skip" in verdicts:
        return "skipped"

    return "passed"


def review_state(questions: list[dict]) -> str:
    """How much of this capture has been looked at."""
    if not questions:
        return "nothing to answer"

    answered = sum(1 for question in questions if question["verdict"])

    if answered == 0:
        return "unreviewed"

    return "reviewed" if answered == len(questions) else "part reviewed"


def facets_of(capture, questions: list[dict] | None = None, tags: list[str] | None = None) -> dict:
    """What a reviewer can filter this capture by.

    Computed rather than stored, so adding a facet is an edit here and every run
    -- including ones captured months ago -- gains it. A capture whose surface
    has since been removed keeps the facets a capture always has (its palette,
    its size, how it was judged) and simply offers no others.
    """
    surface = surfaces.find(capture.surface_state, capture.surface_title)
    tools = list(surface.tools) if surface else []
    questions = questions or []

    return {
        "theme": capture.theme,
        "resolution": capture.geometry,
        "area": surface.area if surface else "",
        "presentation": (surface.presentation if surface else "") or "none",
        "tools": tools or ["none"],
        # A count is worth its own facet: "everything with three tools open" is
        # the question that found the clipped workspace.
        "tool_count": str(len(tools)),
        "state": capture.surface_state,
        # The three that make a second pass finishable: what is left to do, how
        # what is done came out, and what somebody marked on the way past.
        "review": review_state(questions),
        "verdict": verdict_of(questions),
        "tag": list(tags) if tags else ["untagged"],
        "capture": "failed" if capture.failure else ("flagged" if capture.notice else "ok"),
    }


def capture_view(store: Store, capture, *, include_attended: bool = True) -> dict:
    verdicts = {row["question"]: row for row in store.verdicts(capture.id)}
    attended = (
        _question_lookup(capture.surface_state, capture.surface_title, True)
        if include_attended
        else ()
    )
    questions = _question_lookup(capture.surface_state, capture.surface_title, False) + attended
    attended_ids = {question.id for question in attended}

    asked = [
        {
            "id": question.id,
            "prompt": question.prompt,
            "attended": question.id in attended_ids,
            "verdict": verdicts[question.id]["verdict"] if question.id in verdicts else "",
            "note": verdicts[question.id]["note"] if question.id in verdicts else "",
        }
        for question in questions
    ]
    tags = store.tags_for(capture.id)

    return {
        "id": capture.id,
        "surface": capture.surface_title,
        "state": capture.surface_state,
        "geometry": capture.geometry,
        "theme": capture.theme,
        "facets": facets_of(capture, asked, tags),
        "width": capture.width,
        "height": capture.height,
        "failure": capture.failure,
        "notice": capture.notice,
        "unavailable": image_available(capture),
        "tags": tags,
        # id alongside the text: the page has to be able to name the one to
        # delete, and position in a list is not a name.
        "comments": [
            {"id": row["id"], "body": row["body"]} for row in store.comments_for(capture.id)
        ],
        "questions": asked,
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

    # Every value present in this run, so the filter row offers exactly what is
    # there rather than a fixed list that might match nothing.
    available: dict[str, dict[str, int]] = {}

    for group in groups:
        for capture in group["captures"]:
            for name, value in capture["facets"].items():
                counts = available.setdefault(name, {})

                for entry in (value if isinstance(value, list) else [value]):
                    if entry:
                        counts[entry] = counts.get(entry, 0) + 1

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
        "facets": {
            name: sorted(counts.items(), key=lambda entry: entry[0])
            for name, counts in sorted(available.items())
        },
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
    verdict: str = "",
    note: str = "",
    *,
    overwrite: bool = False,
    axes: dict[str, str] | None = None,
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

    A note is optional, including on a failure.

    `axes` answers each question differently in one go -- pass what works, fail
    what looks wrong, leave the rest. Reviewing a row of images usually produces
    one opinion held about all of them, and that opinion is rarely "everything
    about these is fine": it is more often "these work and this one looks off".
    An axis missing from the map is left alone, whatever `overwrite` says,
    because not mentioning something is not a verdict about it.
    """
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
            wanted = axes.get(question.id, "") if axes is not None else verdict

            if not wanted:
                continue

            if question.id in answered and not overwrite:
                skipped += 1

                continue

            found = store.record_verdict(
                capture_id,
                question=question.id,
                prompt=question.prompt,
                verdict=wanted,
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


def add_comment_to_many(store: Store, capture_ids: list[int], body: str) -> dict:
    """One comment, written against every capture given.

    A row each rather than a comment that belongs to a selection: a capture read
    on its own still has to show what was said about it, and a shared comment
    would raise the question of what it shows outside the selection that made
    it.

    All or nothing is deliberately *not* the rule. A selection that has lost one
    capture since it was made should still comment on the rest, and say which it
    could not.
    """
    if not body.strip():
        return {"error": "a comment needs something in it"}

    if not capture_ids:
        return {"error": "no captures selected"}

    written: list[int] = []
    missed: list[int] = []

    for capture_id in capture_ids:
        try:
            store.add_comment(int(capture_id), body)
            written.append(int(capture_id))
        except (StoreError, sqlite3.Error):
            # A capture that has gone raises from the database rather than from
            # the store's own checks -- the foreign key is what refuses it -- so
            # both have to be caught, or one missing capture takes the whole
            # selection down with it.
            missed.append(int(capture_id))

    return {"written": len(written), "captures": written, "missed": missed}


def delete_comment(store: Store, comment_id: int) -> dict:
    if store.delete_comment(comment_id):
        return {"deleted": comment_id}

    return {"error": "that comment is not there"}


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
