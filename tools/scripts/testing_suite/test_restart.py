#!/usr/bin/env python3
"""Tests for restarting the hub from inside the hub.

`restart_now` replaces the process, so what is tested here is the command it
would exec -- which is the part that can be wrong in a way nobody notices until
the hub does not come back, at which point there is no page left to say so.
"""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import restart  # noqa: E402

PYTHON = "/usr/bin/python3"
SCRIPT = "/home/someone/project/.venv/bin/test-suite"


class RestartCommandTests(unittest.TestCase):
    def command(self, *arguments: str) -> list[str]:
        return restart.restart_command(PYTHON, [SCRIPT, *arguments])

    def test_the_interpreter_runs_the_same_script(self) -> None:
        """The console script is not executable by exec on its own."""
        command = self.command("hub", "--no-browser")

        self.assertEqual(command[:2], [PYTHON, SCRIPT])

    def test_a_bare_invocation_becomes_the_hub(self) -> None:
        """`test-suite` means the hub, but only because the parser says so."""
        self.assertEqual(self.command()[2], "hub")

    def test_options_without_a_subcommand_still_become_the_hub(self) -> None:
        command = self.command("--port", "8730")

        self.assertEqual(command[2], "hub")
        self.assertEqual(command[3:5], ["--port", "8730"])

    def test_the_subcommand_that_was_used_is_kept(self) -> None:
        """`review` serves the page too, and is not the same thing as `hub`."""
        self.assertEqual(self.command("review", "--port", "9000")[2], "review")

    def test_every_option_survives(self) -> None:
        command = self.command("hub", "--port", "9001", "--database", "/tmp/x.db")

        self.assertIn("--port", command)
        self.assertEqual(command[command.index("--port") + 1], "9001")
        self.assertEqual(command[command.index("--database") + 1], "/tmp/x.db")

    def test_a_restart_does_not_open_another_browser(self) -> None:
        """The reason to press it is the tab already open on the hub."""
        self.assertIn("--no-browser", self.command("hub"))

    def test_no_browser_is_not_repeated(self) -> None:
        command = self.command("hub", "--no-browser")

        self.assertEqual(command.count("--no-browser"), 1)


if __name__ == "__main__":
    unittest.main()
