"""The coverage build's job count.

The build used a bare ``--parallel``, which CMake hands to make as an unbounded
``-j`` because this tree uses the Makefile generator. Under coverage
instrumentation that ran the runner out of memory, and the whole build was
terminated before any test ran. These pin that the count is always explicit.
"""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_coverage  # noqa: E402


class BuildCommandTests(unittest.TestCase):
    def command(self, jobs: int = 4) -> list[str]:
        return run_coverage.build_command(Path("build-coverage"), "cmake", jobs)

    def test_the_job_count_follows_parallel(self) -> None:
        command = self.command(4)
        flag = command.index("--parallel")

        self.assertEqual(command[flag + 1], "4")

    def test_parallel_is_never_left_bare(self) -> None:
        """A bare --parallel is make's unbounded -j: the failure this replaced."""
        command = self.command()

        self.assertNotEqual(command[-1], "--parallel")
        self.assertTrue(command[command.index("--parallel") + 1].isdigit())

    def test_the_default_is_at_least_one(self) -> None:
        self.assertGreaterEqual(run_coverage.build_jobs(), 1)

    def test_it_still_builds_the_test_target(self) -> None:
        command = self.command()

        self.assertEqual(command[command.index("--target") + 1], "PracticeTakesTests")


if __name__ == "__main__":
    unittest.main()
