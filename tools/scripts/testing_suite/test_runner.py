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
import threading
import time
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
        self.environments: list[dict] = []
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
                         working_directory=None, capture=None, sanitised=True,
                         environment=None):
                outer.environments.append(dict(environment or {}))
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

    # --- Stopping -----------------------------------------------------------

    def stopping_job(self, stop_during: str) -> runner_module.Job:
        """A job whose stop is pressed while `stop_during` is the running suite."""
        outer = self

        class TestableJob(runner_module.Job):
            def _build(inner, target, *, floor, ceiling):  # noqa: N805 - test double
                outer.built.append(target)

            def _command(inner, arguments, *, label, floor, ceiling,  # noqa: N805 - test double
                         working_directory=None, capture=None, sanitised=True,
                         environment=None):
                outer.environments.append(dict(environment or {}))
                outer.commands.append(list(arguments))

                if label == stop_during:
                    inner.stop()

                    return -15  # what a signalled process reports

                if capture is not None:
                    capture.extend(outer.output.splitlines())

                return outer.exit_code

        return TestableJob(store=self.store)

    def test_a_stopped_suite_is_not_recorded_as_a_failure(self) -> None:
        """A killed process exits non-zero. That is not a test failure.

        The store feeds the export the release gate reads, so a stop that wrote
        failures into it would either block a release or teach everyone that
        failures can be ignored.
        """
        job = self.stopping_job("Python script tests")
        self.assertTrue(job.start(["python"]))
        job.wait(30)
        status = job.status()

        self.assertEqual(status["results"]["python"]["state"], "stopped")
        self.assertEqual(self.store.test_results(status["run_id"]), [])

    def test_a_stopped_run_is_finished_rather_than_failed(self) -> None:
        job = self.stopping_job("Python script tests")
        self.assertTrue(job.start(["python"]))
        job.wait(30)

        self.assertEqual(job.status()["state"], runner_module.FINISHED)
        self.assertIn("stopped", job.status()["message"])

    def test_the_suites_after_a_stop_do_not_run(self) -> None:
        """The bug: the loop had no check, so the next suite started anyway."""
        job = self.stopping_job("Python script tests")
        self.assertTrue(job.start(["python", "services", "cpp"]))
        job.wait(30)
        results = job.status()["results"]

        self.assertEqual(results["python"]["state"], "stopped")
        self.assertEqual(results["services"]["state"], "stopped")
        self.assertEqual(results["cpp"]["state"], "stopped")
        self.assertEqual(len(self.commands), 1)

    def test_what_ran_before_the_stop_is_kept(self) -> None:
        self.install_dependencies()
        job = self.stopping_job("C++ unit tests")
        self.assertTrue(job.start(["python", "cpp"]))
        job.wait(30)
        status = job.status()

        self.assertEqual(status["results"]["python"]["state"], "passed")
        self.assertEqual(status["results"]["cpp"]["state"], "stopped")
        self.assertEqual(len(self.store.test_results(status["run_id"])), 1)

    def test_a_stop_during_the_build_runs_no_suite(self) -> None:
        outer = self

        class StoppingBuild(runner_module.Job):
            def _build(inner, target, *, floor, ceiling):  # noqa: N805 - test double
                outer.built.append(target)
                inner.stop()

            def _command(inner, arguments, *, label, floor, ceiling,  # noqa: N805 - test double
                         working_directory=None, capture=None, sanitised=True,
                         environment=None):
                outer.environments.append(dict(environment or {}))
                outer.commands.append(list(arguments))

                return 0

        job = StoppingBuild(store=self.store)
        self.assertTrue(job.start(["cpp"]))
        job.wait(30)

        self.assertEqual(self.built, ["PracticeTakesTests"])
        self.assertEqual(self.commands, [])
        self.assertEqual(job.status()["results"]["cpp"]["state"], "stopped")

    def test_a_build_on_its_own_stops_between_targets(self) -> None:
        outer = self

        class StoppingBuild(runner_module.Job):
            def _build(inner, target, *, floor, ceiling):  # noqa: N805 - test double
                outer.built.append(target)
                inner.stop()

        job = StoppingBuild(store=self.store)
        self.assertTrue(job.start_build(["PracticeTakes", "PracticeTakesTests"]))
        job.wait(30)

        self.assertEqual(self.built, ["PracticeTakes"])
        self.assertEqual(job.status()["state"], runner_module.FINISHED)
        self.assertIn("stopped", job.status()["message"])

    # --- Building on its own ------------------------------------------------

    def build_job(self, targets: list[str] | None = None) -> dict:
        job = self.make_job()
        self.assertTrue(job.start_build(targets))
        job.wait(30)

        return job.status()

    def test_a_build_on_its_own_builds_what_it_was_asked_for(self) -> None:
        status = self.build_job(["PracticeTakes"])

        self.assertEqual(self.built, ["PracticeTakes"])
        self.assertEqual(status["state"], runner_module.FINISHED)
        self.assertEqual(status["percent"], 100)

    def test_a_build_with_no_targets_builds_everything_buildable(self) -> None:
        """Everything this machine can build. A target needing a compiler that
        is not here, or one too old for the sanitizer it asks for, is skipped
        rather than attempted and failed halfway."""
        self.build_job()

        expected = sorted(
            target for target in runner_module.BUILD_TARGETS
            if not runner_module.missing_compiler(target)
        )

        self.assertEqual(sorted(self.built), expected)

    def test_a_build_leaves_no_run_behind_it(self) -> None:
        """A build verified nothing, and history is a record of verification."""
        status = self.build_job(["PracticeTakes"])

        self.assertIsNone(status["run_id"])
        self.assertEqual(self.store.runs(), [])

    def test_a_build_runs_no_suite(self) -> None:
        self.build_job(["PracticeTakes"])

        self.assertEqual(self.commands, [])
        self.assertEqual(self.build_job(["PracticeTakes"])["results"], {})

    def test_an_unknown_target_is_not_built(self) -> None:
        job = self.make_job()

        self.assertFalse(job.start_build(["NotATarget"]))
        self.assertEqual(self.built, [])

    def test_a_failing_build_is_reported_rather_than_raised(self) -> None:
        outer = self

        class ExplodingJob(runner_module.Job):
            def _build(inner, target, *, floor, ceiling):  # noqa: N805 - test double
                outer.built.append(target)

                raise RuntimeError("cmake said no")

        job = ExplodingJob(store=self.store)

        self.assertTrue(job.start_build(["PracticeTakes"]))
        job.wait(30)

        self.assertEqual(job.status()["state"], runner_module.FAILED)
        self.assertIn("cmake said no", job.status()["message"])

    def test_nothing_else_starts_while_a_build_is_running(self) -> None:
        """One job at a time is the existing rule; a build is a job."""
        started = threading.Event()
        release = threading.Event()
        outer = self

        class BlockingJob(runner_module.Job):
            def _build(inner, target, *, floor, ceiling):  # noqa: N805 - test double
                outer.built.append(target)
                started.set()
                release.wait(10)

        job = BlockingJob(store=self.store)
        self.assertTrue(job.start_build(["PracticeTakes"]))
        self.assertTrue(started.wait(10))

        try:
            self.assertFalse(job.start_build(["PracticeTakesTests"]))
            self.assertFalse(job.start(["python"]))
        finally:
            release.set()
            job.wait(30)


class EndingACommandTests(unittest.TestCase):
    """Stopping a command that is actually running.

    Real processes here rather than doubles: the bug being fixed was that the
    stop flag was never looked at while a command ran, and no amount of faking
    `_command` can show whether the real one ends a real build. The commands
    are shell loops rather than anything of the project's, so this stays under
    a second.
    """

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = Store.open(Path(self.directory.name) / "verification.db")
        self.addCleanup(self.store.close)
        self.job = runner_module.Job(store=self.store)
        # `stop` refuses when nothing is running, and what is running here is a
        # bare command rather than a job -- so say so, and use the real stop.
        self.job.state = runner_module.RUNNING

    def run_in_background(self, command: list[str]) -> dict:
        outcome: dict = {}

        def work() -> None:
            outcome["code"] = self.job._command(  # noqa: SLF001 - the unit under test
                command, label="a long command", floor=0, ceiling=10,
                working_directory=Path(self.directory.name),
            )

        thread = threading.Thread(target=work, daemon=True)
        thread.start()

        return {"thread": thread, "outcome": outcome}

    def test_a_command_ends_when_the_job_is_stopped(self) -> None:
        started = self.run_in_background(["/bin/sh", "-c", "sleep 60"])
        time.sleep(0.4)
        self.assertTrue(self.job.stop())
        started["thread"].join(runner_module.STOP_GRACE_SECONDS + 10)

        self.assertFalse(started["thread"].is_alive(), "the command outlived the stop")

    def test_everything_the_command_started_ends_too(self) -> None:
        """cmake is make is a compiler. Signalling only cmake stops nothing."""
        marker = Path(self.directory.name) / "ticks"
        grandchild = f"while true; do echo tick >> {marker}; sleep 0.05; done"
        started = self.run_in_background(
            ["/bin/sh", "-c", f"/bin/sh -c '{grandchild}' & wait"]
        )
        time.sleep(0.5)
        self.assertTrue(marker.is_file(), "the grandchild never started")

        self.assertTrue(self.job.stop())
        started["thread"].join(runner_module.STOP_GRACE_SECONDS + 10)
        time.sleep(0.4)
        settled = marker.stat().st_size
        time.sleep(0.5)

        self.assertEqual(marker.stat().st_size, settled, "the grandchild kept running")

    def test_a_command_is_not_started_once_the_job_is_stopping(self) -> None:
        self.assertTrue(self.job.stop())
        marker = Path(self.directory.name) / "ran"

        code = self.job._command(  # noqa: SLF001 - the unit under test
            ["/bin/sh", "-c", f"touch {marker}"], label="should not run",
            floor=0, ceiling=1, working_directory=Path(self.directory.name),
        )

        self.assertEqual(code, runner_module.STOPPED_CODE)
        self.assertFalse(marker.exists())

    def test_a_command_that_finishes_normally_is_unaffected(self) -> None:
        """The watcher must not touch a command nobody stopped."""
        code = self.job._command(  # noqa: SLF001 - the unit under test
            ["/bin/sh", "-c", "echo hello"], label="quick", floor=0, ceiling=1,
            working_directory=Path(self.directory.name),
        )

        self.assertEqual(code, 0)


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


class RemainingTimeTests(unittest.TestCase):
    """What the bar claims about how much longer this will take.

    Counting surfaces makes it lie: the ones carrying a tone wait a warmup and
    then settle, so a run is past halfway by count long before it is by time.
    The plan's weights say which surfaces are expensive; the run's own measured
    rate says what a weight is worth on this machine today.
    """

    def entry(self, done: float, total: float) -> dict:
        return {"cost_done": done, "cost_total": total}

    def test_nothing_is_claimed_before_there_is_a_rate(self) -> None:
        """A number that swings from four minutes to forty is worse than none."""
        self.assertIsNone(runner_module.remaining_seconds(self.entry(0, 100), 1.0))
        self.assertIsNone(
            runner_module.remaining_seconds(
                self.entry(runner_module.ESTIMATE_AFTER_COST / 2, 100), 5.0
            )
        )

    def test_the_estimate_scales_by_what_has_been_measured(self) -> None:
        # A tenth of the work in 10s: ninety percent left, so ninety seconds.
        self.assertAlmostEqual(
            runner_module.remaining_seconds(self.entry(10, 100), 10.0), 90.0
        )

    def test_a_slower_machine_gets_a_longer_estimate(self) -> None:
        quick = runner_module.remaining_seconds(self.entry(10, 100), 10.0)
        slow = runner_module.remaining_seconds(self.entry(10, 100), 30.0)

        self.assertGreater(slow, quick)

    def test_the_end_of_a_run_is_not_negative(self) -> None:
        self.assertEqual(runner_module.remaining_seconds(self.entry(100, 100), 50.0), 0.0)
        self.assertEqual(runner_module.remaining_seconds(self.entry(120, 100), 50.0), 0.0)

    def test_a_plan_with_no_weights_claims_nothing(self) -> None:
        """Rather than dividing by zero or promising zero seconds."""
        self.assertIsNone(runner_module.remaining_seconds({}, 10.0))

    def test_durations_are_rounded_to_what_they_can_honestly_claim(self) -> None:
        self.assertEqual(runner_module.describe_remaining(3), "under a minute left")
        self.assertEqual(runner_module.describe_remaining(44), "under a minute left")
        self.assertEqual(runner_module.describe_remaining(75), "about 1 minute left")
        self.assertEqual(runner_module.describe_remaining(300), "about 5 minutes left")


class SanitizerSuiteTests(unittest.TestCase):
    """The checks CI runs, offered here too.

    They were only ever in a workflow, which is the wrong way round: they are
    the slow ones, and finding out on a pull request that a change races is
    finding out an hour after writing it.

    What these guard is the thing that quietly goes wrong — the local suite and
    the CI job drifting apart until "the race check passed" means two different
    things.
    """

    def workflow(self, name: str) -> str:
        return (runner_module.REPOSITORY_ROOT / ".github" / "workflows" / name).read_text()

    def suite(self, suite_id: str):
        found = suites_module.by_id(suite_id)
        self.assertIsNotNone(found, f"no suite {suite_id}")

        return found

    def test_each_sanitizer_has_a_build_tree_of_its_own(self) -> None:
        """Instrumentation is a compile-time decision; one tree cannot serve two."""
        directories = {
            runner_module.BUILD_TARGETS[name]["directory"]
            for name in ("PracticeTakesTests-asan", "PracticeTakesTests-tsan",
                         "PracticeTakesTests-rtsan")
        }

        self.assertEqual(len(directories), 3)

    def test_the_build_directories_match_the_ones_ci_uses(self) -> None:
        """A local run and a CI run of the same name must be the same run."""
        workflows = self.workflow("sanitizers.yml") + self.workflow("sanitizers-scheduled.yml")

        for name in ("build-asan", "build-tsan", "build-rtsan"):
            with self.subTest(tree=name):
                self.assertIn(name, workflows)

        for target, tree in (
            ("PracticeTakesTests-asan", "build-asan"),
            ("PracticeTakesTests-tsan", "build-tsan"),
            ("PracticeTakesTests-rtsan", "build-rtsan"),
        ):
            with self.subTest(target=target):
                self.assertEqual(
                    runner_module.BUILD_TARGETS[target]["directory"].name, tree
                )

    def test_the_sanitizer_each_tree_asks_for_matches_ci(self) -> None:
        for target, flavour in (
            ("PracticeTakesTests-asan", "address"),
            ("PracticeTakesTests-tsan", "thread"),
            ("PracticeTakesTests-rtsan", "realtime"),
        ):
            with self.subTest(target=target):
                self.assertIn(
                    f"-DPRACTICE_TAKES_SANITIZE={flavour}",
                    runner_module.BUILD_TARGETS[target]["options"],
                )

    def test_they_all_build_the_test_binary_rather_than_a_target_of_their_own(self) -> None:
        for target in ("PracticeTakesTests-asan", "PracticeTakesTests-tsan",
                       "PracticeTakesTests-rtsan"):
            with self.subTest(target=target):
                self.assertEqual(
                    runner_module.BUILD_TARGETS[target]["builds"], "PracticeTakesTests"
                )

    def test_the_realtime_build_uses_clang(self) -> None:
        """RealtimeSanitizer is Clang's, and so is the annotation it reads."""
        for pair in runner_module.BUILD_TARGETS["PracticeTakesTests-rtsan"]["compilers"]:
            with self.subTest(pair=pair):
                self.assertTrue(all("clang" in name for name in pair))

    def test_a_versioned_clang_is_tried_before_the_default(self) -> None:
        """Debian's `clang` is whatever version is the default -- 19 on trixie
        -- and installing clang-20 beside it leaves `/usr/bin/clang` pointing at
        the old one. Hardcoding `clang` would keep refusing to build on a
        machine that had just been given everything it needed."""
        pairs = runner_module.BUILD_TARGETS["PracticeTakesTests-rtsan"]["compilers"]
        names = [pair[1] for pair in pairs]

        self.assertIn("clang++-20", names)
        self.assertLess(names.index("clang++-20"), names.index("clang++"))

    def test_the_other_builds_keep_the_system_toolchain(self) -> None:
        """A Nix profile ahead of the system one is what kills juceaide."""
        for target in ("PracticeTakes", "PracticeTakesTests", "PracticeTakesTests-asan"):
            with self.subTest(target=target):
                self.assertNotIn("compilers", runner_module.BUILD_TARGETS[target])

    def test_leak_detection_is_actually_switched_on(self) -> None:
        """Without it AddressSanitizer finds errors and not leaks, quietly."""
        options = self.suite("asan").environment["ASAN_OPTIONS"]

        self.assertIn("detect_leaks=1", options)
        self.assertIn("halt_on_error=1", options)

    def test_the_race_suite_runs_only_the_concurrent_cases(self) -> None:
        """The rest is single-threaded; the slowdown would buy nothing."""
        self.assertIn("[.load]", self.suite("tsan").command)

    def test_the_realtime_suite_runs_only_the_callback_cases(self) -> None:
        self.assertIn("[callback]", self.suite("rtsan").command)

    def test_the_sanitizer_options_match_the_ones_ci_uses(self) -> None:
        workflows = self.workflow("sanitizers.yml") + self.workflow("sanitizers-scheduled.yml")

        # YAML quotes a bare number, so compare against both spellings.
        for suite_id in ("asan", "tsan", "rtsan"):
            for name, value in self.suite(suite_id).environment.items():
                with self.subTest(suite=suite_id, option=name):
                    self.assertTrue(
                        f"{name}: {value}" in workflows
                        or f'{name}: "{value}"' in workflows,
                        f"{name} differs between the hub and CI",
                    )

    def test_each_needs_the_tree_it_runs_out_of(self) -> None:
        for suite_id, target in (
            ("asan", "PracticeTakesTests-asan"),
            ("tsan", "PracticeTakesTests-tsan"),
            ("rtsan", "PracticeTakesTests-rtsan"),
        ):
            with self.subTest(suite=suite_id):
                self.assertEqual(self.suite(suite_id).needs, (target,))

    def test_they_are_their_own_kind_rather_than_ordinary_unit_tests(self) -> None:
        """They take minutes and need their own build; grouping them with the
        fast ones would make "run the tests" mean something nobody wanted."""
        for suite_id in ("asan", "tsan", "rtsan"):
            with self.subTest(suite=suite_id):
                self.assertEqual(self.suite(suite_id).kind, suites_module.SAFETY)


class SanitizerEnvironmentTests(JobTests):
    """That the options actually reach the process, not just the definition."""

    def test_the_environment_reaches_the_command(self) -> None:
        self.present.add("PracticeTakesTests-tsan")
        self.run_job(["tsan"])

        used = [env for env in self.environments if "TSAN_OPTIONS" in env]

        self.assertTrue(used, "the race suite ran without its sanitizer options")
        self.assertEqual(used[0]["TSAN_OPTIONS"], suites_module.by_id("tsan").environment["TSAN_OPTIONS"])

    def test_an_ordinary_suite_gets_no_extra_environment(self) -> None:
        self.run_job(["python"])

        self.assertEqual(self.environments, [{}])


class CommandResolutionTests(unittest.TestCase):
    """`{Target}` in a suite's command becomes wherever that target was built."""

    def job(self) -> runner_module.Job:
        return runner_module.Job(store=None)

    def test_a_hyphenated_target_resolves(self) -> None:
        """The sanitizer builds are named for their tree, not their CMake target.

        `\\w+` does not match a hyphen, so `{PracticeTakesTests-tsan}` was passed
        through as a literal and the race suite tried to execute a file called
        `{PracticeTakesTests-tsan}`.
        """
        original = runner_module.binary_path
        runner_module.binary_path = lambda target: Path(f"/build/{target}")
        self.addCleanup(setattr, runner_module, "binary_path", original)

        self.assertEqual(
            self.job()._resolve(("{PracticeTakesTests-tsan}", "[.load]")),
            ["/build/PracticeTakesTests-tsan", "[.load]"],
        )

    def test_every_sanitizer_suite_resolves(self) -> None:
        original = runner_module.binary_path
        runner_module.binary_path = lambda target: Path(f"/build/{target}")
        self.addCleanup(setattr, runner_module, "binary_path", original)

        for suite in suites_module.SUITES:
            with self.subTest(suite=suite.id):
                for argument in self.job()._resolve(suite.command):
                    self.assertNotIn("{", argument, f"{suite.id} left a placeholder")

    def test_an_unbuilt_target_says_so_rather_than_running_a_placeholder(self) -> None:
        original = runner_module.binary_path
        runner_module.binary_path = lambda target: None
        self.addCleanup(setattr, runner_module, "binary_path", original)

        with self.assertRaises(RuntimeError):
            self.job()._resolve(("{PracticeTakesTests-asan}",))


class MissingCompilerTests(unittest.TestCase):
    """A build this machine cannot do is not a failing test.

    RealtimeSanitizer is Clang's, and the annotation the audio callback carries
    is a Clang attribute. On a machine with only GCC the build does not produce
    a wrong answer — it does not produce anything, and reporting that as "the
    audio callback check failed" is a lie that costs somebody an afternoon.
    """

    def test_a_target_with_no_compiler_of_its_own_is_never_blocked(self) -> None:
        for target in ("PracticeTakes", "PracticeTakesTests", "PracticeTakesTests-asan"):
            with self.subTest(target=target):
                self.assertEqual(runner_module.missing_compiler(target), "")

    def test_an_absent_compiler_is_named(self) -> None:
        original = runner_module.shutil.which
        runner_module.shutil.which = lambda name, path=None: None
        self.addCleanup(setattr, runner_module.shutil, "which", original)

        reason = runner_module.missing_compiler("PracticeTakesTests-rtsan")

        # Names every candidate, so the answer is "install one of these" rather
        # than "something about clang".
        self.assertIn("clang++-20", reason)
        self.assertIn("PracticeTakesTests-rtsan", reason)

    def test_a_present_compiler_that_takes_the_flag_blocks_nothing(self) -> None:
        original = runner_module.shutil.which
        runner_module.shutil.which = lambda name, path=None: f"/usr/bin/{name}"
        self.addCleanup(setattr, runner_module.shutil, "which", original)

        runner_module._flag_support.clear()
        self.addCleanup(runner_module._flag_support.clear)

        for pair in runner_module.BUILD_TARGETS["PracticeTakesTests-rtsan"]["compilers"]:
            runner_module._flag_support[(pair[1], "-fsanitize=realtime")] = True

        self.assertEqual(runner_module.missing_compiler("PracticeTakesTests-rtsan"), "")
        self.assertIsNotNone(runner_module.usable_compilers("PracticeTakesTests-rtsan"))

    def test_an_unknown_target_is_not_blocked(self) -> None:
        self.assertEqual(runner_module.missing_compiler("invented"), "")


class BlockedSuiteTests(JobTests):
    def block_clang(self) -> None:
        original = runner_module.shutil.which
        runner_module.shutil.which = lambda name, path=None: (
            None if "clang" in name else f"/usr/bin/{name}"
        )
        self.addCleanup(setattr, runner_module.shutil, "which", original)

    def test_a_suite_that_cannot_be_built_is_unavailable_not_failed(self) -> None:
        self.block_clang()
        status = self.run_job(["rtsan"])

        self.assertEqual(status["results"]["rtsan"]["state"], "unavailable")
        self.assertEqual(status["state"], runner_module.FINISHED)

    def test_nothing_is_built_for_a_suite_that_cannot_run(self) -> None:
        """Discovered before the build, not as a wall of CMake output part way."""
        self.block_clang()
        self.run_job(["rtsan"])

        self.assertEqual(self.built, [])
        self.assertEqual(self.commands, [])

    def test_the_other_suites_still_run(self) -> None:
        self.block_clang()
        status = self.run_job(["rtsan", "python"])

        self.assertEqual(status["results"]["rtsan"]["state"], "unavailable")
        self.assertEqual(status["results"]["python"]["state"], "passed")


class CompilerCapabilityTests(unittest.TestCase):
    """Having the compiler is not the requirement; the flag working is.

    Clang has been installed on this machine the whole time, at version 19.
    RealtimeSanitizer arrived in Clang 20 — so the presence check passed, the
    build started, and clang refused `-fsanitize=realtime` partway through. What
    came back was a complaint about a missing binary.
    """

    def setUp(self) -> None:
        runner_module._flag_support.clear()
        self.addCleanup(runner_module._flag_support.clear)

    def accept(self, works: bool) -> None:
        class Completed:
            returncode = 0 if works else 1

        original = runner_module.subprocess.run
        runner_module.subprocess.run = lambda *a, **k: Completed()
        self.addCleanup(setattr, runner_module.subprocess, "run", original)

        original_which = runner_module.shutil.which
        runner_module.shutil.which = lambda name, path=None: f"/usr/bin/{name}"
        self.addCleanup(setattr, runner_module.shutil, "which", original_which)

    def test_a_compiler_that_refuses_the_flag_blocks_the_build(self) -> None:
        self.accept(False)
        reason = runner_module.missing_compiler("PracticeTakesTests-rtsan")

        self.assertIn("-fsanitize=realtime", reason)
        self.assertIn("Clang 20", reason)
        # And names what it tried, so the answer is "install one of these".
        self.assertIn("clang++-20", reason)

    def test_the_first_compiler_that_takes_the_flag_is_the_one_used(self) -> None:
        original_which = runner_module.shutil.which
        runner_module.shutil.which = lambda name, path=None: f"/usr/bin/{name}"
        self.addCleanup(setattr, runner_module.shutil, "which", original_which)

        runner_module._flag_support.clear()
        self.addCleanup(runner_module._flag_support.clear)
        runner_module._flag_support[("clang++-21", "-fsanitize=realtime")] = False
        runner_module._flag_support[("clang++-20", "-fsanitize=realtime")] = True
        runner_module._flag_support[("clang++", "-fsanitize=realtime")] = True

        self.assertEqual(
            runner_module.usable_compilers("PracticeTakesTests-rtsan"),
            ("clang-20", "clang++-20"),
        )

    def test_a_compiler_that_takes_it_blocks_nothing(self) -> None:
        self.accept(True)

        self.assertEqual(runner_module.missing_compiler("PracticeTakesTests-rtsan"), "")

    def test_the_answer_is_asked_for_once(self) -> None:
        asked = []

        class Completed:
            returncode = 0

        original = runner_module.subprocess.run
        runner_module.subprocess.run = lambda *a, **k: (asked.append(a), Completed())[1]
        self.addCleanup(setattr, runner_module.subprocess, "run", original)

        runner_module._compiler_accepts("clang++", "-fsanitize=realtime")
        runner_module._compiler_accepts("clang++", "-fsanitize=realtime")

        self.assertEqual(len(asked), 1)

    def test_a_compiler_that_cannot_be_run_is_a_no(self) -> None:
        original = runner_module.subprocess.run

        def explode(*a, **k):
            raise OSError("no such file")

        runner_module.subprocess.run = explode
        self.addCleanup(setattr, runner_module.subprocess, "run", original)

        self.assertFalse(runner_module._compiler_accepts("clang++", "-fsanitize=realtime"))

    def test_targets_with_no_flag_requirement_are_unaffected(self) -> None:
        for target in ("PracticeTakes", "PracticeTakesTests", "PracticeTakesTests-asan"):
            with self.subTest(target=target):
                self.assertNotIn("needs_flag", runner_module.BUILD_TARGETS[target])
