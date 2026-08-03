#!/usr/bin/env python3
"""The short attended pass: the questions a photograph cannot answer.

"Does the tuner respond to sound from the microphone?" is not a question about
pixels, and a capture-then-review workflow that quietly stopped asking it would
be verifying that the application *looks* right while saying nothing about
whether it works. So the behavioural questions keep a person and a live
application, and everything else — which is most of it — moved to the grid.

Kept deliberately small: only surfaces that declare a behavioural question, only
one pass over them, one question at a time. A run whose attended questions are
unanswered is incomplete, and its record says which ones.

Standard library only, and no terminal UI framework — a prompt and a line of
input is the whole interaction.
"""

from __future__ import annotations

from collections.abc import Callable

import review
import surfaces
from driver import ApplicationDriver, ChannelError
from store import FAIL, PASS, SKIP, Store

PROMPT_HELP = "Enter = pass · f <reason> = fail · s = skip · q = stop"


def parse_answer(text: str) -> tuple[str, str, str]:
    """One typed line into (verdict, note, error).

    A line of text rather than a hotkey: it carries the reason in the same
    breath as the verdict, and a failure without a reason is the one thing this
    pass must not accept.
    """
    stripped = text.strip()

    if not stripped:
        return PASS, "", ""

    verb, _, rest = stripped.partition(" ")
    rest = rest.strip()
    verb = verb.lower()

    if verb in ("q", "quit", "stop"):
        return "", "", "stop"

    if verb in ("p", "pass"):
        return PASS, rest, ""

    if verb in ("s", "skip"):
        return SKIP, rest, ""

    if verb in ("f", "fail"):
        if not rest:
            return "", "", "A failure needs a reason: f <what you saw>"

        return FAIL, rest, ""

    return "", "", f"'{verb}' is not one of: {PROMPT_HELP}"


class AttendedPass:
    """Walks the outstanding behavioural questions with the application live."""

    def __init__(
        self,
        *,
        store: Store,
        run_id: int,
        driver: ApplicationDriver,
        ask: Callable[[str], str] = input,
        say: Callable[[str], None] = print,
    ) -> None:
        self.store = store
        self.run_id = run_id
        self.driver = driver
        self.ask = ask
        self.say = say

    def move_to(self, state: str, restart: bool) -> str:
        try:
            if restart:
                self.driver.restart()

            reply = self.driver.open_state(state)

            return "" if reply.success else reply.error
        except ChannelError as error:
            return str(error)

    def run(self) -> dict:
        pending = review.outstanding(self.store, self.run_id, attended=True)
        answered = 0
        current_state = ""

        for entry in pending:
            surface = surfaces.find(entry["state"], entry["surface"])

            if surface is None:
                continue

            if entry["state"] != current_state or surface.restart_before:
                reason = self.move_to(entry["state"], surface.restart_before)

                if reason:
                    self.say(f"! {entry['surface']}: could not be reached — {reason}")

                    continue

                current_state = entry["state"]

            self.say(f"\n{entry['surface']}")

            if surface.instruction:
                self.say(f"  First: {surface.instruction}")

            while True:
                self.say(f"  {entry['prompt']}")
                verdict, note, error = parse_answer(self.ask(f"  [{PROMPT_HELP}] "))

                if error == "stop":
                    return {"answered": answered, "remaining": len(pending) - answered}

                if error:
                    self.say(f"  {error}")

                    continue

                problems = review.score(
                    self.store,
                    entry["capture_id"],
                    entry["question"],
                    verdict,
                    note,
                    attended=True,
                )

                if problems:
                    self.say("  " + "; ".join(problems))

                    continue

                answered += 1

                break

        return {"answered": answered, "remaining": len(pending) - answered}
