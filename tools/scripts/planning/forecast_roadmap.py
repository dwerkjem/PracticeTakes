#!/usr/bin/env python3
"""Re-forecast the roadmap from the issues' live state, and publish it.

The roadmap issue (#10) makes commitments: dates, each with an 80% chance of
being met if the future behaves like the measured past. A commitment is only
worth something if someone can see, the day it starts slipping, that it is
slipping. This script is how they see it. It runs whenever an issue is closed or
reopened. It reads the ledger (``roadmap_ledger.json``, every issue to 1.0 and
its size) and the issues' current state. It credits what has been closed as
completed, re-runs the same Monte Carlo forecast the commitments were made from
against what is left, and rewrites one marked block in the roadmap issue.

The commitments themselves never move here. The block shows them beside the
current forecast and says plainly whether each is on track, at risk or behind,
and where the monthly checkpoint stands. Moving a commitment is a decision a
person records in the roadmap issue, not something a workflow does.

Everything that decides anything (crediting, the simulation, the checkpoint
verdict, the rendering, splicing the block into the issue body) is a pure
function, tested without a network. Only ``main`` and the ``gh`` helpers talk
to GitHub.

Standard library only, so it runs on the runner's preinstalled interpreter.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import datetime as dt
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys

LEDGER_PATH = Path(__file__).resolve().parent / "roadmap_ledger.json"
BLOCK_START = "<!-- roadmap-forecast:start -->"
BLOCK_END = "<!-- roadmap-forecast:end -->"
QUANTILES = (0.5, 0.8, 0.9)


# --------------------------------------------------------------------------
# The ledger


@dataclass(frozen=True)
class Release:
    id: str
    name: str
    tracker: int
    target: dt.date
    commitment: dt.date


@dataclass(frozen=True)
class Item:
    issue: int
    size: float
    release: str


@dataclass(frozen=True)
class Checkpoint:
    date: dt.date
    floor: float
    target: float


@dataclass(frozen=True)
class Ledger:
    roadmap_issue: int
    baseline: dt.date
    trackers: tuple[int, ...]
    model: dict
    releases: tuple[Release, ...]
    checkpoints: tuple[Checkpoint, ...]
    items: tuple[Item, ...]
    excluded: frozenset[int]


class LedgerError(ValueError):
    """The ledger contradicts itself. Fixed by editing the ledger."""


def parse_ledger(raw: dict) -> Ledger:
    """Validate and load the ledger, refusing anything ambiguous.

    A ledger with a duplicate issue or an unknown release would make every number
    in the published block quietly wrong, so it fails here, loudly, instead.
    """
    date = dt.date.fromisoformat
    releases = tuple(
        Release(r["id"], r["name"], int(r["tracker"]), date(r["target"]), date(r["commitment"]))
        for r in raw["releases"]
    )
    release_ids = [r.id for r in releases]
    if len(set(release_ids)) != len(release_ids):
        raise LedgerError("release ids must be unique")

    items = tuple(Item(int(i["issue"]), float(i["size"]), i["release"]) for i in raw["items"])
    seen: set[int] = set()
    for item in items:
        if item.issue in seen:
            raise LedgerError(f"#{item.issue} appears in the ledger twice")
        seen.add(item.issue)
        if item.release not in release_ids:
            raise LedgerError(f"#{item.issue} names unknown release {item.release!r}")
        if not item.size > 0:
            raise LedgerError(f"#{item.issue} must have a positive size")

    excluded = frozenset(int(e["issue"]) for e in raw.get("excluded", []))
    if excluded & seen:
        raise LedgerError(f"issues both sized and excluded: {sorted(excluded & seen)}")

    checkpoints = tuple(
        sorted(
            (Checkpoint(date(c["date"]), float(c["floor"]), float(c["target"])) for c in raw["checkpoints"]),
            key=lambda c: c.date,
        )
    )
    return Ledger(
        roadmap_issue=int(raw["roadmap_issue"]),
        baseline=date(raw["baseline"]),
        trackers=tuple(int(t) for t in raw["trackers"]),
        model=dict(raw["model"]),
        releases=releases,
        checkpoints=checkpoints,
        items=items,
        excluded=excluded,
    )


def load_ledger(path: Path = LEDGER_PATH) -> Ledger:
    return parse_ledger(json.loads(path.read_text(encoding="utf-8")))


# --------------------------------------------------------------------------
# Crediting work against the issues' state


@dataclass(frozen=True)
class IssueState:
    number: int
    open: bool
    completed: bool  # closed as completed; closed as not planned is False
    closed_on: dt.date | None


def issue_state_from_api(issue: dict) -> IssueState:
    """Reduce one REST ``issues`` object to the fields the forecast reads."""
    closed_at = issue.get("closed_at")
    is_open = issue.get("state") == "open"
    return IssueState(
        number=int(issue["number"]),
        open=is_open,
        completed=(not is_open) and issue.get("state_reason") in (None, "completed"),
        closed_on=dt.date.fromisoformat(closed_at[:10]) if closed_at else None,
    )


@dataclass(frozen=True)
class Progress:
    credited: float  # estimate-weeks closed as completed since the baseline
    remaining_by_release: dict[str, float]  # open estimate-weeks, per release
    dropped: tuple[int, ...]  # closed as not planned: out of scope, no credit
    unknown: tuple[int, ...]  # in the ledger, but GitHub returned no such issue
    last_completed: dt.date | None


def assess(ledger: Ledger, states: dict[int, IssueState]) -> Progress:
    """Credit closed work and total what is left, per release.

    An issue counts as delivered only when closed as completed. Closed as not
    planned means the scope was cut: it leaves the remaining work without
    adding to the delivered total, so cutting scope can't pass for progress.
    """
    remaining = {r.id: 0.0 for r in ledger.releases}
    credited = 0.0
    dropped: list[int] = []
    unknown: list[int] = []
    last: dt.date | None = None
    for item in ledger.items:
        state = states.get(item.issue)
        if state is None:
            unknown.append(item.issue)
            remaining[item.release] += item.size
        elif state.open:
            remaining[item.release] += item.size
        elif state.completed:
            if state.closed_on is None or state.closed_on >= ledger.baseline:
                credited += item.size
            if state.closed_on and (last is None or state.closed_on > last):
                last = state.closed_on
        else:
            dropped.append(item.issue)
    return Progress(credited, remaining, tuple(dropped), tuple(unknown), last)


def unsized_open_issues(ledger: Ledger, tracker_children: dict[int, list[IssueState]]) -> list[int]:
    """Open sub-issues of a stage tracker that the ledger does not size.

    These are scope the forecast cannot see yet. Listing them is how new work
    gets sized rather than silently left out of every date.
    """
    known = {i.issue for i in ledger.items} | ledger.excluded | set(ledger.trackers)
    found = {
        child.number
        for children in tracker_children.values()
        for child in children
        if child.open and child.number not in known
    }
    return sorted(found)


# --------------------------------------------------------------------------
# The forecast


def cumulative_thresholds(ledger: Ledger, remaining: dict[str, float]) -> list[float]:
    """Remaining work that must be done before each release, in release order."""
    total = 0.0
    out = []
    for release in ledger.releases:
        total += remaining[release.id]
        out.append(total)
    return out


def starts_active(ledger: Ledger, progress: Progress, today: dt.date) -> bool:
    """Whether the forecast's first week is an active one.

    Active if roadmap work was closed recently, counting the baseline itself
    as activity, since the forecast was made in the middle of a burst.
    """
    anchor = max(d for d in (ledger.baseline, progress.last_completed) if d is not None)
    return (today - anchor).days <= int(ledger.model["active_window_days"])


def simulate(thresholds: list[float], model: dict, *, active: bool, runs: int | None = None) -> list[list[int]]:
    """Weeks until each threshold is reached, one list per run.

    The model is the one #10's commitments were made from. Weeks are active or
    idle, following a two-state chain whose transition rates are drawn per run
    from Beta priors fitted to the observed history. An active week closes a
    per-run mean rate with lognormal week-to-week noise, divided by a per-run
    estimate bias. Scope grows at a per-run rate between zero and the observed
    rate, and growth lands proportionally across the remaining releases.
    """
    rng = random.Random(int(model["seed"]))
    n = int(runs if runs is not None else model["runs"])
    horizon = int(model["horizon_weeks"])
    total = thresholds[-1] if thresholds else 0.0
    results: list[list[int]] = []
    for _ in range(n):
        p_ai = rng.betavariate(*model["active_to_idle_beta"])
        p_ia = rng.betavariate(*model["idle_to_active_beta"])
        rate = max(float(model["rate_floor"]), rng.gauss(model["rate_mean"], model["rate_sd"]))
        bias = rng.lognormvariate(0, model["bias_sigma"])
        growth = rng.uniform(0, model["growth_max"])
        is_active, done, grown, week = active, 0.0, 0.0, 0
        hit = [0 if t <= 0 else horizon for t in thresholds]
        pending = [i for i, t in enumerate(thresholds) if t > 0]
        while pending and week < horizon:
            if is_active:
                done += rate * rng.lognormvariate(0, model["weekly_sigma"]) / bias
            grown += growth
            week += 1
            scale = 1 + (grown / total if total else 0)
            while pending and done >= thresholds[pending[0]] * scale:
                hit[pending.pop(0)] = week
            is_active = (rng.random() > p_ai) if is_active else (rng.random() < p_ia)
        results.append(hit)
    return results


def quantile_weeks(results: list[list[int]], index: int, q: float) -> int:
    values = sorted(run[index] for run in results)
    return values[min(len(values) - 1, int(math.floor(q * len(values))))]


@dataclass(frozen=True)
class ReleaseForecast:
    release: Release
    remaining: float
    done: bool
    dates: dict[float, dt.date]
    status: str


def judge(release: Release, done: bool, p50: dt.date, p80: dt.date) -> str:
    """The verdict against the commitment, which this never changes."""
    if done:
        return "done"
    if p80 <= release.commitment:
        return "on track"
    if p50 <= release.commitment:
        return "at risk"
    return "behind"


def forecast(ledger: Ledger, progress: Progress, today: dt.date, *, runs: int | None = None) -> list[ReleaseForecast]:
    thresholds = cumulative_thresholds(ledger, progress.remaining_by_release)
    results = simulate(thresholds, ledger.model, active=starts_active(ledger, progress, today), runs=runs)
    out = []
    for i, release in enumerate(ledger.releases):
        dates = {q: today + dt.timedelta(weeks=quantile_weeks(results, i, q)) for q in QUANTILES}
        done = progress.remaining_by_release[release.id] == 0 and all(
            progress.remaining_by_release[r.id] == 0 for r in ledger.releases[: i + 1]
        )
        out.append(
            ReleaseForecast(
                release,
                progress.remaining_by_release[release.id],
                done,
                dates,
                judge(release, done, dates[0.5], dates[0.8]),
            )
        )
    return out


# --------------------------------------------------------------------------
# The checkpoint verdict


@dataclass(frozen=True)
class CheckpointView:
    last: Checkpoint | None
    last_met: bool | None
    next: Checkpoint | None
    floor_today: float | None
    target_today: float | None
    verdict: str | None


def checkpoint_view(ledger: Ledger, credited: float, today: dt.date) -> CheckpointView:
    """Where the monthly accountability number stands.

    The checkpoints are month-end values, so mid-month the credited total is
    judged against the floor and target pro-rated to today: a straight line
    from the previous checkpoint (or zero at the baseline) to the next one.
    Judging day three against the month-end floor would read "behind" every
    month, and a verdict that is always red stops being read.

    The last checkpoint is judged on today's credited total, which can only
    overstate it. The record of a checkpoint missed on the day is the roadmap
    issue's history, not this block.
    """
    past = [c for c in ledger.checkpoints if c.date < today]
    future = [c for c in ledger.checkpoints if c.date >= today]
    last = past[-1] if past else None
    nxt = future[0] if future else None
    floor_today = target_today = verdict = None
    if nxt is not None:
        start = last or Checkpoint(ledger.baseline, 0.0, 0.0)
        span = (nxt.date - start.date).days
        fraction = min(1.0, max(0.0, (today - start.date).days / span)) if span > 0 else 1.0
        floor_today = start.floor + fraction * (nxt.floor - start.floor)
        target_today = start.target + fraction * (nxt.target - start.target)
        verdict = (
            "on target" if credited >= target_today else "above the floor" if credited >= floor_today else "below the floor"
        )
    return CheckpointView(
        last, None if last is None else credited >= last.floor, nxt, floor_today, target_today, verdict
    )


# --------------------------------------------------------------------------
# Rendering


def fmt_date(d: dt.date) -> str:
    return f"{d.day} {d:%b %Y}"


def fmt_weeks(value: float) -> str:
    return f"{value:g}"


STATUS_MARK = {"done": "✅ done", "on track": "🟢 on track", "at risk": "🟠 at risk", "behind": "🔴 behind"}


def render_block(
    ledger: Ledger,
    progress: Progress,
    forecasts: list[ReleaseForecast],
    checkpoints: CheckpointView,
    unsized: list[int],
    *,
    today: dt.date,
    trigger: str | None = None,
    run_url: str | None = None,
) -> str:
    total = progress.credited + sum(progress.remaining_by_release.values())
    remaining = sum(progress.remaining_by_release.values())
    provenance = f"Updated {fmt_date(today)}"
    if trigger:
        provenance += f" after {trigger}"
    if run_url:
        provenance += f" · [run]({run_url})"
    lines = [
        BLOCK_START,
        "## Live forecast",
        "",
        f"*{provenance}. Written by `tools/scripts/planning/forecast_roadmap.py` from "
        "`tools/scripts/planning/roadmap_ledger.json`: change the ledger, not this block. "
        "Commitments never move here; only the forecast beside them does.*",
        "",
        f"**Delivered:** {fmt_weeks(progress.credited)} estimate-weeks closed as completed since "
        f"{fmt_date(ledger.baseline)}, of {fmt_weeks(total)} in the ledger. **Remaining:** {fmt_weeks(remaining)}.",
        "",
    ]
    if checkpoints.next is not None:
        c = checkpoints.next
        lines.append(
            f"**Next checkpoint, {fmt_date(c.date)}:** floor {fmt_weeks(c.floor)}, target {fmt_weeks(c.target)}. "
            f"Pro-rated to today that is floor {checkpoints.floor_today:.1f}, target {checkpoints.target_today:.1f}: "
            f"**{checkpoints.verdict}**."
        )
    if checkpoints.last is not None:
        c = checkpoints.last
        verdict = "met" if checkpoints.last_met else "**missed** — cut scope per the rule above"
        lines.append(f"Last checkpoint, {fmt_date(c.date)} (floor {fmt_weeks(c.floor)}): {verdict}.")
    lines += [
        "",
        "| Release | Remaining | Commitment | Forecast 50% | Forecast 80% | Status |",
        "|---|---|---|---|---|---|",
    ]
    for f in forecasts:
        if f.done:
            lines.append(f"| {f.release.name} | 0 | {fmt_date(f.release.commitment)} | — | — | {STATUS_MARK['done']} |")
        else:
            lines.append(
                f"| {f.release.name} | {fmt_weeks(f.remaining)} | {fmt_date(f.release.commitment)} | "
                f"{fmt_date(f.dates[0.5])} | {fmt_date(f.dates[0.8])} | {STATUS_MARK[f.status]} |"
            )
    lines += [
        "",
        "🟢 the 80% forecast meets the commitment · 🟠 only the 50% forecast does · 🔴 neither does.",
    ]
    if unsized:
        refs = ", ".join(f"#{n}" for n in unsized)
        lines += ["", f"**Not in the forecast yet:** {refs}. Open under a stage tracker but unsized; add each to the ledger."]
    if progress.dropped:
        refs = ", ".join(f"#{n}" for n in progress.dropped)
        lines += ["", f"Closed as not planned, so out of scope and uncredited: {refs}."]
    if progress.unknown:
        refs = ", ".join(f"#{n}" for n in progress.unknown)
        lines += ["", f"**In the ledger but not found on GitHub:** {refs}. Counted as open."]
    lines.append(BLOCK_END)
    return "\n".join(lines)


class MissingMarkersError(ValueError):
    pass


def splice_block(body: str, block: str) -> str:
    """Replace the marked block in the issue body, leaving everything else alone.

    The markers must already be there, exactly once each. Appending a block to
    an issue that has lost them would put a second, unanchored copy somewhere
    nobody looks, so a missing marker is an error a person fixes.
    """
    if body.count(BLOCK_START) != 1 or body.count(BLOCK_END) != 1:
        raise MissingMarkersError(
            f"the roadmap issue must contain {BLOCK_START} and {BLOCK_END} exactly once each"
        )
    head, rest = body.split(BLOCK_START, 1)
    _, tail = rest.split(BLOCK_END, 1)
    if not block.startswith(BLOCK_START) or not block.endswith(BLOCK_END):
        raise ValueError("the block must start and end with the markers")
    return head + block + tail


# --------------------------------------------------------------------------
# GitHub, through `gh api`


def _gh(args: list[str], stdin: str | None = None) -> str:
    completed = subprocess.run(
        ["gh", "api", *args], check=True, capture_output=True, text=True, encoding="utf-8", input=stdin
    )
    return completed.stdout


def _paged(path: str) -> list[dict]:
    out: list[dict] = []
    page = 1
    separator = "&" if "?" in path else "?"
    while True:
        batch = json.loads(_gh([f"{path}{separator}per_page=100&page={page}"]) or "[]")
        out.extend(batch)
        if len(batch) < 100:
            return out
        page += 1


def fetch_states(repo: str) -> dict[int, IssueState]:
    issues = _paged(f"repos/{repo}/issues?state=all")
    return {int(i["number"]): issue_state_from_api(i) for i in issues if "pull_request" not in i}


def fetch_tracker_children(repo: str, trackers: tuple[int, ...]) -> dict[int, list[IssueState]]:
    return {t: [issue_state_from_api(c) for c in _paged(f"repos/{repo}/issues/{t}/sub_issues")] for t in trackers}


def fetch_body(repo: str, number: int) -> str:
    return json.loads(_gh([f"repos/{repo}/issues/{number}"]))["body"] or ""


def write_body(repo: str, number: int, body: str) -> None:
    _gh(["-X", "PATCH", f"repos/{repo}/issues/{number}", "--input", "-"], stdin=json.dumps({"body": body}))


# --------------------------------------------------------------------------
# Entry point


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"), help="owner/name")
    parser.add_argument("--today", type=dt.date.fromisoformat, default=None, help="override today's date")
    parser.add_argument("--trigger", default=None, help='what caused this run, e.g. "#36 closed"')
    parser.add_argument("--run-url", default=None)
    parser.add_argument("--runs", type=int, default=None, help="override the simulation's run count")
    parser.add_argument(
        "--issues-json",
        type=Path,
        default=None,
        help="read issue states from a saved REST `issues` listing instead of GitHub (implies --dry-run)",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the block instead of editing the issue")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    ledger = load_ledger()
    today = arguments.today or dt.date.today()

    if arguments.issues_json is not None:
        listing = json.loads(arguments.issues_json.read_text(encoding="utf-8"))
        states = {int(i["number"]): issue_state_from_api(i) for i in listing if "pull_request" not in i}
        children: dict[int, list[IssueState]] = {}
        dry_run = True
    else:
        if not arguments.repo:
            print("error: --repo or GITHUB_REPOSITORY is required", file=sys.stderr)
            return 2
        states = fetch_states(arguments.repo)
        children = fetch_tracker_children(arguments.repo, ledger.trackers)
        dry_run = arguments.dry_run

    progress = assess(ledger, states)
    forecasts = forecast(ledger, progress, today, runs=arguments.runs)
    block = render_block(
        ledger,
        progress,
        forecasts,
        checkpoint_view(ledger, progress.credited, today),
        unsized_open_issues(ledger, children),
        today=today,
        trigger=arguments.trigger,
        run_url=arguments.run_url,
    )

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(block + "\n")

    if dry_run:
        print(block)
        return 0

    body = fetch_body(arguments.repo, ledger.roadmap_issue)
    try:
        updated = splice_block(body, block)
    except MissingMarkersError as error:
        print(f"error: #{ledger.roadmap_issue}: {error}", file=sys.stderr)
        return 1
    if updated == body:
        print(f"#{ledger.roadmap_issue} is already current")
        return 0
    write_body(arguments.repo, ledger.roadmap_issue, updated)
    print(f"Updated the forecast in #{ledger.roadmap_issue}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
