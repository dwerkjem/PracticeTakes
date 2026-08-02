#!/usr/bin/env python3
"""The record a manual verification run produces.

Written by the harness, never transcribed by hand. Two forms are written side
by side: JSON, which is diffable and lets two runs be compared question by
question, and a rendered Markdown summary, which is what a person actually reads
when asking "what did we verify before v0.5.7".

The rules that matter here are about honesty rather than formatting:

- An interrupted run is marked incomplete and keeps what was answered, so a long
  run is never lost and a partial run is never mistaken for a passing one.
- A failed answer requires a note, so a recorded failure is actionable without
  re-running the surface.
- The mode and the geometry sweep are recorded, so a quick run cannot later be
  read as a release check.

Standard library only.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
import json
from pathlib import Path

PASS = "pass"
FAIL = "fail"
SKIP = "skip"

VERDICTS = (PASS, FAIL, SKIP)

# Runs live in the repository so "what did we verify, and when" is answerable
# from a clone. A directory of dated files rather than one appended log: a log
# conflicts on every concurrent edit and is harder to diff.
DEFAULT_RECORDS_DIRECTORY = Path("docs/development/quality/manual-runs")


@dataclass
class Answer:
    surface: str
    state: str
    geometry: str
    question: str
    prompt: str
    verdict: str
    note: str = ""

    def problems(self) -> list[str]:
        problems: list[str] = []

        if self.verdict not in VERDICTS:
            problems.append(f"'{self.verdict}' is not one of {', '.join(VERDICTS)}.")

        # A failure with no detail costs more than it saves: it says something
        # is wrong without saying what, so the surface has to be run again to
        # learn anything.
        if self.verdict == FAIL and not self.note.strip():
            problems.append(f"'{self.question}' on '{self.surface}' failed without a note.")

        return problems


@dataclass
class Run:
    commit: str
    platform: str
    audio_device: str
    mode: str
    geometry_sweep: bool
    started_at: str
    finished_at: str = ""
    complete: bool = False
    answers: list[Answer] = field(default_factory=list)

    # Surfaces the harness could not reach. Recorded as failures of those
    # surfaces rather than skipped, because a surface nobody could look at is a
    # finding, not an absence.
    unreachable: list[dict[str, str]] = field(default_factory=list)

    # Failures that are allowed to ship anyway, each with a written reason.
    #
    # Deliberately not something the harness offers during a run: waiving is a
    # decision made after seeing the result, not while looking at the screen.
    # Adding one means editing this file, which makes it a reviewable commit
    # rather than a keystroke -- and it stays in the release's history.
    #
    # Each entry needs "surface", "question", and "reason".
    waivers: list[dict[str, str]] = field(default_factory=list)

    def is_waived(self, answer: "Answer") -> bool:
        return any(
            waiver.get("surface") == answer.surface
            and waiver.get("question") == answer.question
            and waiver.get("reason", "").strip()
            for waiver in self.waivers
        )

    def unwaived_failures(self) -> list["Answer"]:
        return [answer for answer in self.failures() if not self.is_waived(answer)]

    def add(self, answer: Answer) -> None:
        self.answers.append(answer)

    def failures(self) -> list[Answer]:
        return [answer for answer in self.answers if answer.verdict == FAIL]

    def problems(self) -> list[str]:
        problems: list[str] = []

        for answer in self.answers:
            problems.extend(answer.problems())

        return problems


def to_json(run: Run) -> str:
    return json.dumps(asdict(run), indent=2, sort_keys=True) + "\n"


def to_markdown(run: Run) -> str:
    """A rendered summary, for reading rather than diffing."""
    status = "complete" if run.complete else "INCOMPLETE"
    lines = [
        f"# Manual GUI verification — {run.started_at}",
        "",
        f"- **Mode**: {run.mode}",
        f"- **Status**: {status}",
        f"- **Commit**: `{run.commit}`",
        f"- **Platform**: {run.platform}",
        f"- **Audio device**: {run.audio_device}",
        f"- **Geometry sweep**: {'yes' if run.geometry_sweep else 'no (default size only)'}",
        "",
    ]

    if not run.complete:
        lines += [
            "> This run was interrupted. The answers below are the ones that were",
            "> given; the remaining surfaces were **not** verified, and their absence",
            "> must not be read as a pass.",
            "",
        ]

    failures = run.failures()

    if failures:
        lines += [f"## Failures ({len(failures)})", ""]
        lines += [
            f"- **{answer.surface}** ({answer.geometry}) — {answer.prompt} — {answer.note}"
            for answer in failures
        ]
        lines.append("")
    elif run.complete:
        lines += ["No failures.", ""]

    if run.unreachable:
        lines += [f"## Unreachable surfaces ({len(run.unreachable)})", ""]
        lines += [
            f"- **{entry.get('surface', '?')}** — {entry.get('reason', 'no reason recorded')}"
            for entry in run.unreachable
        ]
        lines.append("")

    lines += ["## All answers", "", "| Surface | Geometry | Question | Verdict | Note |",
              "|---|---|---|---|---|"]

    for answer in run.answers:
        note = answer.note.replace("|", "\\|")
        lines.append(
            f"| {answer.surface} | {answer.geometry} | {answer.prompt} "
            f"| {answer.verdict} | {note} |"
        )

    return "\n".join(lines) + "\n"


def record_paths(run: Run, directory: Path) -> tuple[Path, Path]:
    """Where this run's two files go.

    The mode is in the filename so a directory listing distinguishes a quick run
    from a release check without opening anything.
    """
    stamp = run.started_at.replace(":", "").replace("-", "").replace("T", "-")
    stem = f"{stamp}-{run.mode}"

    return directory / f"{stem}.json", directory / f"{stem}.md"


def write(run: Run, directory: Path) -> tuple[Path, Path]:
    """Write both forms, creating the directory if needed."""
    directory.mkdir(parents=True, exist_ok=True)
    json_path, markdown_path = record_paths(run, directory)

    json_path.write_text(to_json(run), encoding="utf-8")
    markdown_path.write_text(to_markdown(run), encoding="utf-8")

    return json_path, markdown_path
