#!/usr/bin/env python3
"""The terminal UI for a manual verification run.

Deliberately thin. Every decision about what happens next lives in
``session.py``, which is testable without a terminal; this module renders the
current step and hands typed lines back. Requires Textual.

Answers are **typed, not hotkeyed**. Two earlier attempts failed for opposite
reasons: bare letters were swallowed by the note field, so a failure could never
be given the reason it requires; function keys are bound by the terminal's host
(VS Code takes F1-F12), so they never arrived at all. A line of text collides
with nothing, works in any terminal, and carries the reason in the same breath
as the verdict.
"""

from __future__ import annotations

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import VerticalScroll
from textual.widgets import Footer, Header, Input, Static

import session as session_module


class VerificationApp(App[None]):
    """Presents one question at a time and records what is typed."""

    # Laid out for a small terminal as much as a large one. The body scrolls
    # and the entry field is docked, so the one thing you must always be able to
    # reach cannot be pushed off-screen by a long question or a short window.
    CSS = """
    #body { height: 1fr; }
    #surface-title { text-style: bold; padding: 1 2 0 2; }
    #instruction { color: $warning; padding: 0 2; }
    #question { padding: 1 2; text-style: bold; }
    #progress, #state { color: $text-muted; padding: 0 2; }
    #legend { color: $text-muted; padding: 0 2; }
    #problem { color: $error; padding: 0 2; }
    #entry { dock: bottom; margin: 0 2 0 2; }

    /* Below roughly 24 rows there is no room for both the legend and the
       surface detail, so the legend goes first -- it is a reminder, and `?`
       still prints it on demand. */
    #legend.hidden, #state.hidden { display: none; }
    """

    # ctrl+q only, and only as a safety net. Everything else is typed, so
    # nothing here can be intercepted by the terminal's host.
    BINDINGS = [Binding("ctrl+q", "stop", "Stop and save")]

    def __init__(self, session: session_module.Session, on_move) -> None:
        super().__init__()
        self._session = session
        self._on_move = on_move

    def compose(self) -> ComposeResult:
        yield Header()
        yield VerticalScroll(
            Static("", id="progress"),
            Static("", id="surface-title"),
            Static("", id="state"),
            Static("", id="instruction"),
            Static("", id="question"),
            Static("", id="problem"),
            Static(session_module.HELP_TEXT, id="legend"),
            id="body",
        )
        yield Input(
            placeholder="Enter = pass · f <reason> · s · a · as · u <reason> · q · ?",
            id="entry",
        )
        yield Footer()

    def on_mount(self) -> None:
        self._advance_application()
        self._render()
        self.query_one("#entry", Input).focus()

    def on_resize(self, event) -> None:
        self._apply_size(event.size.height, event.size.width)

    def _apply_size(self, height: int, width: int) -> None:
        """Shed the least important lines when the terminal is short."""
        legend = self.query_one("#legend", Static)
        legend.set_class(height < 24, "hidden")

        # The state line is diagnostic rather than something a tester acts on,
        # so it is the next to go.
        self.query_one("#state", Static).set_class(height < 18, "hidden")

        # A narrow terminal cannot show the long legend on one line; the short
        # form still names every command.
        legend.update(
            session_module.SHORT_HELP_TEXT if width < 90 else session_module.HELP_TEXT
        )

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

    def _say(self, message: str) -> None:
        self.query_one("#problem", Static).update(message)

    def _clear_entry(self) -> None:
        self.query_one("#entry", Input).value = ""

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

    # --- Input --------------------------------------------------------------

    def on_input_submitted(self, event: Input.Submitted) -> None:
        entry = session_module.parse_entry(event.value)

        if entry.action == "help":
            self._say(entry.message)
            self._clear_entry()
            return

        if entry.action == "error":
            # The typed line stays, so a mistyped verb can be corrected rather
            # than retyped.
            self._say(entry.message)
            return

        if entry.action == "stop":
            self._finish()
            return

        if entry.action == "unreachable":
            self._session.skip_surface(entry.note)
        elif entry.action == "area":
            problems = self._session.answer_rest_of_surface(entry.verdict, entry.note)

            if problems:
                self._say(problems[0])
                return
        else:
            problems = self._session.answer(entry.verdict, entry.note)

            if problems:
                self._say(problems[0])
                return

        self._say("")
        self._clear_entry()
        self._advance_application()
        self._render()

    def action_stop(self) -> None:
        self._finish()

    def _finish(self) -> None:
        self._session.finish()
        self.exit()
