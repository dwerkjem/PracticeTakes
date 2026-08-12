#!/usr/bin/env python3
"""Tests for the control channel to a running application.

A shell script stands in for Practice Takes: the protocol is line in, lines
out, so anything that speaks it is enough to test the parts that can be wrong
here -- which are about waiting, not about windows.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import driver  # noqa: E402

# Stands in for the application: line in, "ok" out, and gone on `quit` -- which
# the real one does too, and which keeps these tests from spending ten seconds
# each waiting for a process that was never going to leave.
ANSWERS = 'while read line; do echo ok; [ "$line" = quit ] && exit 0; done'
SLOW = 'while read line; do sleep 0.3; echo ok; [ "$line" = quit ] && exit 0; done'

class ReplyTimeoutTests(unittest.TestCase):
    """An application that stops answering must not stop the run.

    This is what two instances did: the second blocked inside ALSA opening a
    device the first already held, on the very thread that answers this
    channel. The capture waited on the pipe with no deadline, and nothing --
    no image, no log line, no failed row -- said so.
    """

    def driver_talking_to(self, script: str) -> driver.ApplicationDriver:
        source = Path(self.directory.name) / "app.sh"
        source.write_text(f"#!/bin/sh\n{script}\n")
        source.chmod(0o755)
        made = driver.ApplicationDriver(source)
        made.start()
        self.addCleanup(made.stop)

        return made

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)

    def test_an_application_that_never_answers_is_reported(self) -> None:
        talking = self.driver_talking_to("sleep 3")

        with self.assertRaises(driver.ChannelError) as raised:
            talking.send("status", timeout=0.4)

        self.assertIn("not responding", str(raised.exception))
        self.assertIn("status", str(raised.exception))

    def test_an_application_that_answers_is_untouched(self) -> None:
        talking = self.driver_talking_to(ANSWERS)
        reply = talking.send("status", timeout=5)

        self.assertTrue(reply.success)

    def test_a_slow_answer_inside_the_deadline_is_waited_for(self) -> None:
        """A state change legitimately takes seconds; a false timeout would
        report a working build as broken."""
        talking = self.driver_talking_to(SLOW)

        self.assertTrue(talking.send("status", timeout=10).success)

    def test_a_closed_channel_is_told_apart_from_a_slow_one(self) -> None:
        talking = self.driver_talking_to("exit 0")

        with self.assertRaises(driver.ChannelError) as raised:
            talking.send("status", timeout=10)

        self.assertNotIn("not responding", str(raised.exception))


class StoppingTests(unittest.TestCase):
    """Leaving the application the way a user would."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)

    def application(self, script: str) -> driver.ApplicationDriver:
        source = Path(self.directory.name) / "app.sh"
        source.write_text(f"#!/bin/sh\n{script}\n")
        source.chmod(0o755)
        made = driver.ApplicationDriver(source)
        made.start()

        return made

    def test_quit_is_actually_sent(self) -> None:
        """It was not. `stop` cleared the process first, so `send` refused
        against the very application it was closing -- and every stop instead
        waited ten seconds and killed it."""
        seen = Path(self.directory.name) / "commands"
        talking = self.application(
            f'while read line; do echo "$line" >> {seen}; echo ok; '
            f'[ "$line" = quit ] && exit 0; done'
        )
        talking.send("status")
        talking.stop()

        self.assertEqual(seen.read_text().split(), ["status", "quit"])

    def test_stopping_is_prompt(self) -> None:
        talking = self.application(
            'while read line; do echo ok; [ "$line" = quit ] && exit 0; done'
        )
        started = time.monotonic()
        talking.stop()

        self.assertLess(time.monotonic() - started, 5.0)

    def test_an_application_that_will_not_leave_is_killed(self) -> None:
        talking = self.application("trap '' TERM; while true; do sleep 0.2; done")
        # A short grace: the wait is the behaviour under test, not its length.
        talking.stop(grace=0.5)

        self.assertIsNone(talking.pid)

    def test_stopping_twice_is_not_an_error(self) -> None:
        talking = self.application('while read line; do echo ok; done')
        talking.stop(grace=0.5)
        talking.stop(grace=0.5)


class ConcurrentChannelTests(unittest.TestCase):
    """Several drivers at once, which is what a parallel run is.

    Each owns its own process and its own reader thread. The thing that would
    go wrong invisibly is replies crossing between them — worker A reading
    worker B's `ok` and both believing they were answered.
    """

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)

    def application(self, name: str, answer: str) -> driver.ApplicationDriver:
        source = Path(self.directory.name) / f"{name}.sh"
        source.write_text(
            f'#!/bin/sh\nwhile read line; do echo "item {answer}"; echo ok; '
            f'[ "$line" = quit ] && exit 0; done\n'
        )
        source.chmod(0o755)
        made = driver.ApplicationDriver(source)
        made.start()
        self.addCleanup(made.stop)

        return made

    def test_replies_do_not_cross_between_drivers(self) -> None:
        drivers = {name: self.application(name, name) for name in ("one", "two", "three")}
        wrong: list[str] = []
        guard = threading.Lock()
        ready = threading.Barrier(len(drivers))

        def ask(name: str) -> None:
            ready.wait(10)

            for _ in range(30):
                reply = drivers[name].send("status", timeout=10)

                if reply.items != [name]:
                    with guard:
                        wrong.append(f"{name} got {reply.items}")

        threads = [threading.Thread(target=ask, args=(name,)) for name in drivers]

        for thread in threads:
            thread.start()

        for thread in threads:
            thread.join(30)

        self.assertEqual(wrong, [])

    def test_one_driver_hanging_does_not_hold_up_another(self) -> None:
        """A worker whose application stops answering must not stop the run."""
        # Never reads its stdin, which is what an application blocked inside
        # the audio device looks like from here: alive, drawn, and silent.
        silent = Path(self.directory.name) / "silent.sh"
        # Short, because the test is about the other driver continuing, and
        # the shell keeps this alive until it ends whatever we do to it.
        silent.write_text("#!/bin/sh\nsleep 4\n")
        silent.chmod(0o755)
        stuck = driver.ApplicationDriver(silent)
        stuck.start()
        self.addCleanup(stuck.kill)

        working = self.application("working", "working")

        # The reader is per driver, so a deadline on one is not a deadline on
        # anything else.
        began = time.monotonic()
        answers: list[bool] = []

        def ask_the_working_one() -> None:
            for _ in range(5):
                answers.append(working.send("status", timeout=5).success)

        thread = threading.Thread(target=ask_the_working_one)
        thread.start()

        with self.assertRaises(driver.ChannelError):
            # Nothing is reading `stuck`'s stdin, so this waits out its deadline
            # while the other driver keeps working.
            stuck.send("status", timeout=1.0)

        thread.join(30)

        self.assertEqual(answers, [True] * 5)
        self.assertLess(time.monotonic() - began, 4)


if __name__ == "__main__":
    unittest.main()
