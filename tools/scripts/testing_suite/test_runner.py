#!/usr/bin/env python3
"""Tests for the suite registry and the background job.

No suite is actually run here — the point is the machinery around them: what
gets built, what gets skipped, what a verdict is, and what lands in the store.
Real commands are replaced by a fake `_command`, so this stays a fast unit test
rather than a ten-minute build.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import runner as runner_module  # noqa: E402
import suites as suites_module  # noqa: E402
import surfaces  # noqa: E402
from store import Store  # noqa: E402

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


class SuiteListTests(unittest.TestCase):
    def test_the_list_is_coherent(self) -> None:
        self.assertEqual(suites_module.validate(), [])

    def test_every_kind_has_a_suite(self) -> None:
        """A kind with nothing in it renders as an empty group in the hub."""
        for kind in suites_module.KINDS:
            with self.subTest(kind=kind):
                self.assertTrue(suites_module.of_kind(kind))

    def test_the_suites_that_need_a_display_are_marked(self) -> None:
        needing = {suite.id for suite in suites_module.SUITES if suite.needs_display}

        self.assertEqual(needing, {"smoke", "ui-golden", "ui-capture"})

    def test_an_unknown_id_is_none(self) -> None:
        self.assertIsNone(suites_module.by_id("invented"))


class ParserTests(unittest.TestCase):
    def test_ctest_output(self) -> None:
        parsed = suites_module.parse_ctest(
            "98% tests passed, 1 tests failed out of 44\n\n"
            "Total Test time (real) =   3.51 sec\n"
        )

        self.assertEqual(parsed, {"cases": 44, "failures": 1, "duration_seconds": 3.51})

    def test_unittest_output(self) -> None:
        parsed = suites_module.parse_unittest(
            "Ran 292 tests in 1.054s\n\nFAILED (failures=2, errors=1)\n"
        )

        self.assertEqual(parsed["cases"], 292)
        self.assertEqual(parsed["failures"], 3)

    def test_a_passing_unittest_run(self) -> None:
        parsed = suites_module.parse_unittest("Ran 292 tests in 1.054s\n\nOK\n")

        self.assertEqual(parsed["failures"], 0)

    def test_output_that_cannot_be_read_returns_nothing(self) -> None:
        """Not a zero — a count nobody could read must not look like a clean run."""
        self.assertEqual(suites_module.parse_ctest("something else entirely"), {})
        self.assertEqual(suites_module.parse_unittest("something else entirely"), {})

    def test_catch2_benchmarks_become_measurements(self) -> None:
        parsed = suites_module.parse_catch2_benchmarks(
            "harmonic analyzer per-frame                     100          1     4.2 ms\n"
            "                                        1.23 ms      1.20 ms      1.31 ms\n"
        )

        self.assertEqual(len(parsed["measurements"]), 1)
        self.assertEqual(parsed["measurements"][0]["metric"], "harmonic analyzer per-frame")
        self.assertEqual(parsed["measurements"][0]["value"], 1.23)
        self.assertEqual(parsed["measurements"][0]["unit"], "ms")


class JobTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = Store.open(Path(self.directory.name) / "verification.db")
        self.addCleanup(self.store.close)

        # Nothing here shells out or builds: the job's plumbing is what is under
        # test, not cmake.
        self.commands: list[list[str]] = []
        self.built: list[str] = []
        self.exit_code = 0
        self.output = "Ran 10 tests in 0.5s\n\nOK\n"

        # Provenance shells out to glxinfo, lspci, and xdpyinfo; a fixed machine
        # keeps this a unit test rather than a survey of the host.
        original = runner_module.machine_module.provenance
        runner_module.machine_module.provenance = lambda: PROVENANCE
        self.addCleanup(setattr, runner_module.machine_module, "provenance", original)

        # Everything below is a fact about the developer's disk -- whether a
        # target is built, whether node_modules is installed, whether npm is on
        # the PATH -- so it is controlled here rather than inherited. Inheriting
        # it meant these passed on a machine that had done the work and failed
        # on CI, which had not.
        self.root = Path(self.directory.name)
        self.present: set[str] = set()

        original_state = runner_module.build_state
        runner_module.build_state = lambda target="PracticeTakes": {
            "target": target,
            "present": target in self.present,
            "stale": False,
            "reason": "",
        }
        self.addCleanup(setattr, runner_module, "build_state", original_state)

        # A built binary exists wherever the test says it does, so a command
        # naming a target resolves without a real build tree.
        binary = self.root / "PracticeTakesTests"
        binary.write_text("#!/bin/sh\n")
        binary.chmod(0o755)
        original_binary = runner_module.binary_path
        runner_module.binary_path = lambda target: binary
        self.addCleanup(setattr, runner_module, "binary_path", original_binary)

        # Programs are installed unless a test says otherwise.
        original_missing = runner_module.missing_program
        runner_module.missing_program = lambda command: ""
        self.addCleanup(setattr, runner_module, "missing_program", original_missing)

        # A repository root of its own, so "is node_modules there?" is this
        # test's decision rather than the checkout's.
        original_root = runner_module.REPOSITORY_ROOT
        runner_module.REPOSITORY_ROOT = self.root
        self.addCleanup(setattr, runner_module, "REPOSITORY_ROOT", original_root)

    def install_dependencies(self) -> None:
        """Make a suite's prepare step unnecessary."""
        (self.root / "src" / "services" / "node_modules").mkdir(parents=True, exist_ok=True)

    def make_job(self) -> runner_module.Job:
        outer = self

        class TestableJob(runner_module.Job):
            def _build(inner, target, *, floor, ceiling):  # noqa: N805 - test double
                outer.built.append(target)

            def _command(inner, arguments, *, label, floor, ceiling,  # noqa: N805 - test double
                         working_directory=None, capture=None, sanitised=True):
                outer.commands.append(list(arguments))

                if capture is not None:
                    capture.extend(outer.output.splitlines())

                return outer.exit_code

        return TestableJob(store=self.store)

    def run_job(self, suite_ids: list[str], **kwargs) -> dict:
        job = self.make_job()
        self.assertTrue(job.start(suite_ids, **kwargs))
        job.wait(30)

        return job.status()

    def test_a_passing_suite_is_recorded_against_the_run(self) -> None:
        status = self.run_job(["python"])

        self.assertEqual(status["state"], runner_module.FINISHED)
        self.assertEqual(status["results"]["python"]["state"], "passed")

        results = self.store.test_results(status["run_id"])

        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["cases"], 10)
        self.assertEqual(results[0]["failures"], 0)

    def test_a_non_zero_exit_is_a_failure_whatever_the_output_said(self) -> None:
        """The exit code is the verdict; scraped counts are best effort."""
        self.exit_code = 1
        status = self.run_job(["python"])

        self.assertEqual(status["state"], runner_module.FAILED)
        self.assertEqual(status["results"]["python"]["state"], "failed")
        self.assertEqual(self.store.test_results(status["run_id"])[0]["failures"], 1)

    def test_unreadable_output_still_reports_the_verdict(self) -> None:
        self.output = "nothing parseable here"
        status = self.run_job(["python"])

        self.assertEqual(status["results"]["python"]["state"], "passed")
        self.assertEqual(self.store.test_results(status["run_id"])[0]["cases"], 0)

    def test_several_suites_run_in_order(self) -> None:
        self.install_dependencies()
        status = self.run_job(["python", "services"])

        self.assertEqual([entry[0] for entry in self.commands], ["python3", "npm"])
        self.assertEqual(len(status["results"]), 2)

    def test_what_a_suite_needs_is_built_first(self) -> None:
        self.run_job(["cpp"])

        self.assertIn("PracticeTakesTests", self.built)

    def test_a_target_is_built_once_for_several_suites(self) -> None:
        self.run_job(["cpp", "benchmarks"])

        self.assertEqual(self.built.count("PracticeTakesTests"), 1)

    def test_an_existing_build_is_not_rebuilt(self) -> None:
        self.present.add("PracticeTakesTests")
        self.run_job(["cpp"])

        self.assertEqual(self.built, [])

    def test_rebuild_forces_a_build_that_already_exists(self) -> None:
        self.present.add("PracticeTakesTests")
        self.run_job(["cpp"], rebuild=True)

        self.assertEqual(self.built, ["PracticeTakesTests"])

    def test_benchmark_measurements_reach_the_store(self) -> None:
        self.output = (
            "pitch detector                                 100          1     4.2 ms\n"
            "                                        2.50 ms      2.40 ms      2.61 ms\n"
        )
        status = self.run_job(["benchmarks"])

        measurements = self.store.measurements(status["run_id"])

        self.assertEqual(len(measurements), 1)
        self.assertEqual(measurements[0]["metric"], "pitch detector")

    def test_a_display_suite_is_skipped_rather_than_failed_without_one(self) -> None:
        original = runner_module.display_available
        runner_module.display_available = lambda: False
        self.addCleanup(setattr, runner_module, "display_available", original)

        status = self.run_job(["smoke"])

        self.assertEqual(status["results"]["smoke"]["state"], "skipped")
        self.assertEqual(status["state"], runner_module.FINISHED)

    def test_a_missing_program_is_unavailable_rather_than_failed(self) -> None:
        """`npm` not installed is not a failing test — the suite never ran."""
        runner_module.missing_program = lambda command: (
            "`npm` is not installed, or is not on your PATH" if command == "npm" else ""
        )

        status = self.run_job(["services"])

        self.assertEqual(status["results"]["services"]["state"], "unavailable")
        self.assertIn("npm", status["results"]["services"]["message"])
        self.assertEqual(status["state"], runner_module.FINISHED)

    def test_a_failure_with_no_output_says_so(self) -> None:
        self.exit_code = 127
        self.output = ""
        status = self.run_job(["python"])

        self.assertIn("printed nothing", status["results"]["python"]["message"])

    def test_dependencies_are_installed_when_missing(self) -> None:
        self.run_job(["services"])

        self.assertIn(["npm", "ci"], self.commands)

    def test_dependencies_already_installed_are_not_reinstalled(self) -> None:
        self.install_dependencies()
        self.run_job(["services"])

        self.assertNotIn(["npm", "ci"], self.commands)

    def test_a_failed_preparation_makes_the_suite_unavailable(self) -> None:
        self.exit_code = 1

        status = self.run_job(["services"])

        self.assertEqual(status["results"]["services"]["state"], "unavailable")

    def test_suites_run_in_the_developers_own_environment(self) -> None:
        """The sanitised PATH is right for compiling and wrong for npm and zsh.

        Asserted against the ambient environment rather than against
        BUILD_PATH: on a machine whose PATH happens to be exactly the sanitised
        one, the two are equal and comparing them proves nothing.
        """
        import os

        self.assertEqual(runner_module.suite_environment().get("PATH"), os.environ.get("PATH"))
        self.assertEqual(runner_module.build_environment()["PATH"], runner_module.BUILD_PATH)

    def test_one_job_at_a_time(self) -> None:
        job = self.make_job()
        job.state = runner_module.RUNNING

        self.assertFalse(job.start(["python"]))

    def test_an_unknown_suite_starts_nothing(self) -> None:
        job = self.make_job()

        self.assertFalse(job.start(["invented"]))

    def test_everything_lands_on_one_run(self) -> None:
        """A run is one build under test — that is what makes it answerable."""
        status = self.run_job(["python", "services", "cpp"])
        runs = self.store.runs()

        self.assertEqual(len(runs), 1)
        self.assertEqual(len(self.store.test_results(status["run_id"])), 3)

    def test_the_log_says_what_happened(self) -> None:
        status = self.run_job(["python"])

        self.assertTrue(any("Python script tests" in line for line in status["log"]))


class BinaryDiscoveryTests(unittest.TestCase):
    """Where a built target actually is.

    CMake puts `PracticeTakes` in `bin/` because it sets an output directory,
    and `PracticeTakesTests` in the build root because it does not. Assuming
    either layout for both turned a successful build into "the build finished
    but the binary is not there".
    """

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.original = dict(runner_module.BUILD_TARGETS["PracticeTakesTests"])
        runner_module.BUILD_TARGETS["PracticeTakesTests"] = {
            **self.original, "directory": self.root,
        }
        self.addCleanup(
            runner_module.BUILD_TARGETS.__setitem__, "PracticeTakesTests", self.original
        )

    def place(self, relative: str) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#!/bin/sh\n")
        path.chmod(0o755)

        return path

    def test_a_binary_in_the_build_root_is_found(self) -> None:
        expected = self.place("PracticeTakesTests")

        self.assertEqual(runner_module.binary_path("PracticeTakesTests"), expected)

    def test_a_binary_under_bin_is_found(self) -> None:
        expected = self.place("bin/PracticeTakesTests")

        self.assertEqual(runner_module.binary_path("PracticeTakesTests"), expected)

    def test_bin_wins_when_both_exist(self) -> None:
        expected = self.place("bin/PracticeTakesTests")
        self.place("PracticeTakesTests")

        self.assertEqual(runner_module.binary_path("PracticeTakesTests"), expected)

    def test_a_target_that_was_never_built_is_none(self) -> None:
        self.assertIsNone(runner_module.binary_path("PracticeTakesTests"))

    def test_a_non_executable_file_does_not_count(self) -> None:
        path = self.root / "PracticeTakesTests"
        path.write_text("not a program")
        path.chmod(0o644)

        self.assertIsNone(runner_module.binary_path("PracticeTakesTests"))

    def test_an_unknown_target_is_none(self) -> None:
        self.assertIsNone(runner_module.binary_path("SomethingElse"))

    def test_a_command_naming_a_target_is_resolved(self) -> None:
        expected = self.place("PracticeTakesTests")
        job = runner_module.Job(store=None)

        self.assertEqual(
            job._resolve(("{PracticeTakesTests}", "[.benchmark]")),
            [str(expected), "[.benchmark]"],
        )

    def test_a_command_naming_an_unbuilt_target_says_so(self) -> None:
        job = runner_module.Job(store=None)

        with self.assertRaises(RuntimeError):
            job._resolve(("{PracticeTakesTests}",))


class BuildStateTests(unittest.TestCase):
    def test_an_unknown_target_is_not_reported_as_missing(self) -> None:
        state = runner_module.build_state("SomethingElse")

        self.assertTrue(state["present"])

    def test_the_known_targets_are_described(self) -> None:
        for target in runner_module.BUILD_TARGETS:
            with self.subTest(target=target):
                state = runner_module.build_state(target)

                self.assertEqual(state["target"], target)
                self.assertIn("present", state)

    def test_the_build_environment_is_sanitised(self) -> None:
        """Nix's loader in front of the system one breaks juceaide mid-build."""
        environment = runner_module.build_environment()

        self.assertEqual(environment["PATH"], runner_module.BUILD_PATH)
        self.assertNotIn("LD_LIBRARY_PATH", environment)


if __name__ == "__main__":
    unittest.main()
