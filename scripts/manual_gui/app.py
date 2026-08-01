#!/usr/bin/env python3
"""The terminal UI for a manual verification run.

Deliberately thin. Every decision about what happens next lives in
``session.py``, which is testable without a terminal; this module renders the
current step and hands answers back. Requires Textual — install with
``pip install -e '.[manual-gui]'`` or ``uv sync --extra manual-gui``.
"""

from __future__ import annotations

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.widgets import Button, Footer, Header, Input, Label, Static

import record
import session as session_module


class VerificationApp(App[None]):
    """Presents one question at a time and records the answer."""

    CSS = """
    #surface-title { text-style: bold; padding: 1 2 0 2; }
    #instruction { color: $warning; padding: 0 2; }
    #question { padding: 1 2; text-style: bold; }
    #progress, #state { color: $text-muted; padding: 0 2; }
    #problem { color: $error; padding: 0 2; }
    #verdicts, #area-verdicts { padding: 1 2 0 2; height: auto; }
    #verdicts Button, #area-verdicts Button { margin-right: 2; }
    #note { margin: 0 2; }
    """

    # Function keys and modifiers, never bare letters.
    #
    # The note field is focused the whole time, so a bare `p` or `f` would be
    # typed into the note instead of answering -- which made failing impossible,
    # because a failure needs a note and you could not have both. Keys that the
    # Input does not consume are the whole fix.
    BINDINGS = [
        Binding("f1", "verdict('pass')", "Pass"),
        Binding("f2", "verdict('fail')", "Fail"),
        Binding("f3", "verdict('skip')", "Skip"),
        Binding("f4", "unreachable", "Can't reach"),
        # Answer the whole surface at once. The common case: you look, nothing
        # is wrong, and answering three questions separately is friction for no
        # information.
        Binding("f5", "area('pass')", "Pass area"),
        # For an area a change did not touch and you did not examine. Recording
        # a pass for something unlooked-at is what turns a record into a rubber
        # stamp, so this is deliberately a separate key.
        Binding("f6", "area('skip')", "Skip area"),
        # Quitting mid-run is expected, not exceptional -- a full run is long.
        # What must not happen is the partial run looking like a passing one,
        # which is why the record is written and marked incomplete on the way
        # out.
        Binding("ctrl+q", "stop", "Stop and save"),
    ]

    def __init__(self, session: session_module.Session, on_move) -> None:
        super().__init__()
        self._session = session
        self._on_move = on_move

    def compose(self) -> ComposeResult:
        yield Header()
        yield Vertical(
            Static("", id="progress"),
            Static("", id="surface-title"),
            Static("", id="state"),
            Static("", id="instruction"),
            Static("", id="question"),
            Static("", id="problem"),
            Horizontal(
                Button("Pass (F1)", id="pass", variant="success"),
                Button("Fail (F2)", id="fail", variant="error"),
                Button("Skip (F3)", id="skip"),
                Button("Can't reach (F4)", id="unreachable", variant="warning"),
                id="verdicts",
            ),
            Horizontal(
                Button("Pass whole area (F5)", id="area-pass", variant="success"),
                Button("Skip whole area (F6)", id="area-skip"),
                id="area-verdicts",
            ),
            Label("Note (type here; required for a failure):"),
            Input(placeholder="What did you see?", id="note"),
        )
        yield Footer()

    def on_mount(self) -> None:
        self._advance_application()
        self._render()
        # Focused from the start so a note can simply be typed. Every binding
        # above uses a key the Input does not consume, so this costs nothing.
        self.query_one("#note", Input).focus()

    # --- Rendering ----------------------------------------------------------

    def _render(self) -> None:
        step = self._session.current()

        if step is None:
            self._finish()
            return

        self.query_one("#progress", Static).update(
            f"Step {step.index} of {step.total}  ·  mode: {self._session.run.mode}"
        )
        self.query_one("#surface-title", Static).update(step.surface.title)
        self.query_one("#state", Static).update(
            f"state: {step.surface.state}  ·  geometry: {step.geometry}"
        )
        self.query_one("#instruction", Static).update(
            f"Do this first: {step.surface.instruction}" if step.surface.instruction else ""
        )
        self.query_one("#question", Static).update(step.question.prompt)

    def _clear_problem(self) -> None:
        self.query_one("#problem", Static).update("")

    def _note(self) -> str:
        return self.query_one("#note", Input).value

    def _reset_note(self) -> None:
        self.query_one("#note", Input).value = ""

    # --- Driving the application -------------------------------------------

    def _advance_application(self) -> None:
        """Move the application when the surface or geometry changes."""
        if not self._session.surface_changed():
            return

        step = self._session.current()

        if step is None:
            return

        problem = self._on_move(step.surface, step.geometry)

        if problem:
            # Recorded as a failure of that surface, and the run continues.
            self._session.skip_surface(problem)
            self._advance_application()

    # --- Actions ------------------------------------------------------------

    def action_verdict(self, verdict: str) -> None:
        problems = self._session.answer(verdict, self._note())

        if problems:
            self.query_one("#problem", Static).update(problems[0])
            return

        self._clear_problem()
        self._reset_note()
        self._advance_application()
        self._render()

    def action_area(self, verdict: str) -> None:
        problems = self._session.answer_rest_of_surface(verdict, self._note())

        if problems:
            self.query_one("#problem", Static).update(problems[0])
            self._render()
            return

        self._clear_problem()
        self._reset_note()
        self._advance_application()
        self._render()

    def action_unreachable(self) -> None:
        note = self._note() or "Reported unreachable by the tester."
        self._session.skip_surface(note)
        self._clear_problem()
        self._reset_note()
        self._advance_application()
        self._render()

    def action_stop(self) -> None:
        self._finish()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "unreachable":
            self.action_unreachable()
        elif event.button.id == "area-pass":
            self.action_area(record.PASS)
        elif event.button.id == "area-skip":
            self.action_area(record.SKIP)
        elif event.button.id in (record.PASS, record.FAIL, record.SKIP):
            self.action_verdict(event.button.id)

        # Clicking a button moves focus to it; put it back so the next note can
        # be typed without reaching for the mouse again.
        self.query_one("#note", Input).focus()

    def _finish(self) -> None:
        self._session.finish()
        self.exit()
