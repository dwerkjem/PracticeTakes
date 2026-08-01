#!/usr/bin/env python3
"""The sequencing of a manual verification run, with no UI and no application.

Everything about *what happens next* lives here so it can be tested without a
display, a terminal, or a built binary. The Textual layer renders this and
feeds answers back into it; the driver moves the application. Neither decides
anything.

Standard library only.
"""

from __future__ import annotations

from dataclasses import dataclass

import record
import surfaces
from surfaces import Question, Surface


@dataclass
class Step:
    """One prompt: a surface, at a geometry, asking one question."""

    surface: Surface
    geometry: str
    question: Question
    index: int
    total: int

    @property
    def is_first_of_surface(self) -> bool:
        return self.question.id == surfaces.FIXED_AXES[0].id


class Session:
    """Walks the plan, collecting answers into a run."""

    def __init__(self, run: record.Run, mode: str, sweep: bool) -> None:
        self.run = run
        self._plan = surfaces.plan(mode, sweep)
        self._steps: list[tuple[Surface, str, Question]] = [
            (surface, geometry, question)
            for surface, geometry in self._plan
            for question in surfaces.questions_for(surface)
        ]
        self._position = 0

    @property
    def total_steps(self) -> int:
        return len(self._steps)

    @property
    def finished(self) -> bool:
        return self._position >= len(self._steps)

    def current(self) -> Step | None:
        if self.finished:
            return None

        surface, geometry, question = self._steps[self._position]

        return Step(surface, geometry, question, self._position + 1, len(self._steps))

    def surface_changed(self) -> bool:
        """Whether the next step is on a different surface or geometry.

        The caller uses this to know when to move the application, rather than
        re-establishing the same state for every question on it.
        """
        if self.finished:
            return False

        if self._position == 0:
            return True

        surface, geometry, _ = self._steps[self._position]
        previous_surface, previous_geometry, _ = self._steps[self._position - 1]

        return surface is not previous_surface or geometry != previous_geometry

    def answer(self, verdict: str, note: str = "") -> list[str]:
        """Record an answer and advance. Returns problems, and does not advance
        when there are any -- a failure with no note must be corrected, not
        stored."""
        step = self.current()

        if step is None:
            return ["The run has already finished."]

        entry = record.Answer(
            surface=step.surface.title,
            state=step.surface.state,
            geometry=step.geometry,
            question=step.question.id,
            prompt=step.question.prompt,
            verdict=verdict,
            note=note,
        )

        problems = entry.problems()

        if problems:
            return problems

        self.run.add(entry)
        self._position += 1

        return []

    def answer_rest_of_surface(self, verdict: str, note: str = "") -> list[str]:
        """Apply one verdict to every remaining question on this surface.

        The common case by far: you look at the surface, nothing is wrong, and
        answering three or four questions individually is friction for no
        information. One keystroke instead.

        `skip` is the honest choice for an area a change did not touch and you
        did not actually examine — recording a pass for something unexamined is
        what turns a record into a rubber stamp.
        """
        step = self.current()

        if step is None:
            return ["The run has already finished."]

        surface = step.surface
        geometry = step.geometry
        problems: list[str] = []

        while not self.finished:
            current = self._steps[self._position]

            if current[0] is not surface or current[1] != geometry:
                break

            problems = self.answer(verdict, note)

            if problems:
                # A failure with no note stops the sweep at the offending
                # question rather than applying a half-recorded verdict.
                break

        return problems

    def skip_surface(self, reason: str) -> None:
        """Abandon the current surface, recording it as unreachable.

        Used when the application could not be driven to it. Every remaining
        question on that surface is recorded as failed rather than skipped: a
        surface nobody could look at is a finding, not an absence.
        """
        step = self.current()

        if step is None:
            return

        surface = step.surface
        geometry = step.geometry

        self.run.unreachable.append(
            {"surface": surface.title, "state": surface.state,
             "geometry": geometry, "reason": reason}
        )

        while not self.finished:
            current = self._steps[self._position]

            if current[0] is not surface or current[1] != geometry:
                break

            self.run.add(
                record.Answer(
                    surface=surface.title,
                    state=surface.state,
                    geometry=geometry,
                    question=current[2].id,
                    prompt=current[2].prompt,
                    verdict=record.FAIL,
                    note=f"Surface could not be reached: {reason}",
                )
            )
            self._position += 1

    def finish(self) -> None:
        """Mark the run complete. Only truthful when every step was answered."""
        self.run.complete = self.finished
