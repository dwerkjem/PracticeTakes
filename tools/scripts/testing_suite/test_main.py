#!/usr/bin/env python3
"""Tests for the command line entry point.

`__main__.py` is loaded by file path under a name of its own rather than
`import __main__` -- that name is already bound to whatever script is actually
running (this test runner), and importing over it would be reaching into a
module a great deal of other code depends on.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import surfaces  # noqa: E402
from store import Store  # noqa: E402

_SPEC = importlib.util.spec_from_file_location(
    "practice_takes_testing_suite_cli", Path(__file__).resolve().parent / "__main__.py"
)
main_module = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = main_module
_SPEC.loader.exec_module(main_module)

PROVENANCE = {
    "identity": "machine-one",
    "processor": "Test CPU",
    "cores": 8,
    "memory_bytes": 16 * 1024**3,
    "graphics": "Test GPU",
    "operating_system": "Linux",
    "display": "2560x1440",
    "attributes": {},
}
SHELL = next(s for s in surfaces.SURFACES if s.title == "The shell with no tool open")


class BareInvocationTests(unittest.TestCase):
    """A bare `test-suite --option ...`, with no subcommand, implies `hub`."""

    def test_a_global_option_before_a_bare_invocation_is_not_dropped(self) -> None:
        """The real entry point calls `main()` with no argv, so this only
        reproduces against `sys.argv` -- passing argv explicitly bypasses the
        bug entirely, which is why it is patched here instead.
        """
        seen: dict = {}

        def fake_hub(arguments) -> int:
            seen["database"] = arguments.database

            return 0

        with patch.object(main_module, "command_hub", fake_hub), patch.object(
            sys, "argv", ["test-suite", "--database", "/tmp/example.db"]
        ):
            result = main_module.main()

        self.assertEqual(result, 0)
        self.assertEqual(seen.get("database"), Path("/tmp/example.db"))


class HistoryLimitTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.store = Store.open(self.root / "verification.db")
        self.addCleanup(self.store.close)

        for index in range(3):
            run_id = self.store.start_run(
                provenance=PROVENANCE, commit=f"commit{index}", mode=surfaces.FULL,
                resolutions=("default",),
            )
            self.store.execute(
                "UPDATE run SET started_at = ? WHERE id = ?",
                (f"2026-08-0{index + 1}T10:00:00", run_id),
            )
            self.store.commit()
            capture_id = self.store.record_capture(
                run_id, state=SHELL.state, title=SHELL.title, geometry="default",
                image_path=str(self.root / f"commit{index}.png"),
            )
            self.store.record_verdict(capture_id, question="q0", prompt="?", verdict="pass")

    def run_history(self, limit: int) -> list[str]:
        arguments = SimpleNamespace(
            database=self.root / "verification.db",
            directory=self.root / "run-history",
            machine="",
            limit=limit,
        )
        printed: list[str] = []

        with patch("builtins.print", side_effect=lambda *args: printed.append(" ".join(map(str, args)))):
            main_module.command_history(arguments)

        # Every printed run line is indented two spaces; the header and the
        # blank line after it are not.
        return [line for line in printed if line.startswith("  ") and "scored run" not in line]

    def test_a_limit_of_zero_shows_no_runs(self) -> None:
        """`-arguments.limit:` with `arguments.limit == 0` slices from index
        zero -- `-0 == 0` in Python -- and showed every run instead of none.
        """
        self.assertEqual(self.run_history(0), [])

    def test_a_positive_limit_still_shows_the_most_recent_runs(self) -> None:
        self.assertEqual(len(self.run_history(2)), 2)


if __name__ == "__main__":
    unittest.main()
