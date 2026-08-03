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

    def make_job(self) -> runner_module.Job:
        outer = self

        class TestableJob(runner_module.Job):
            def _build(inner, target, *, floor, ceiling):  # noqa: N805 - test double
                outer.built.append(target)

            def _command(inner, arguments, *, label, floor, ceiling,  # noqa: N805 - test double
                         working_directory=None, capture=None):
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
        status = self.run_job(["python", "services"])

        self.assertEqual([entry[0] for entry in self.commands], ["python3", "npm"])
        self.assertEqual(len(status["results"]), 2)

    def test_what_a_suite_needs_is_built_first(self) -> None:
        self.run_job(["cpp"])

        self.assertIn("PracticeTakesTests", self.built)

    def test_a_target_is_built_once_for_several_suites(self) -> None:
        self.run_job(["cpp", "benchmarks"])

        self.assertEqual(self.built.count("PracticeTakesTests"), 1)

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
