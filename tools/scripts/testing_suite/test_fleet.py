#!/usr/bin/env python3
"""Tests for keeping track of the applications a run started.

The rule being enforced is the one that is easy to get wrong and expensive to
get wrong: a run ends what it started, by process id, and never anything that
merely has the same name. Somebody is using this computer, and Practice Takes
being open on it is the ordinary case rather than an interference to clean up.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fleet as fleet_module  # noqa: E402


class FakeDriver:
    """Enough of an application to be tracked and ended."""

    def __init__(self, pid: int | None) -> None:
        self.pid = pid
        self.killed = 0

    def kill(self) -> None:
        self.killed += 1


class ProcessTableTests(unittest.TestCase):
    """Reading /proc, against a fake one."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def process(self, pid: int, name: str) -> None:
        entry = self.root / str(pid)
        entry.mkdir()
        (entry / "comm").write_text(f"{name}\n")

    def test_instances_are_found_by_name(self) -> None:
        self.process(10, "PracticeTakes")
        self.process(11, "firefox")
        self.process(12, "PracticeTakes")

        self.assertEqual(fleet_module.running_instances(self.root), {10, 12})

    def test_something_that_merely_mentions_it_is_not_an_instance(self) -> None:
        """A build, an editor, or this suite itself would match a command line."""
        self.process(20, "cmake")
        self.process(21, "test-suite")
        self.process(22, "python3")

        self.assertEqual(fleet_module.running_instances(self.root), set())

    def test_a_process_that_vanishes_mid_read_is_not_an_error(self) -> None:
        entry = self.root / "30"
        entry.mkdir()  # no `comm`, as though it exited between listing and reading

        self.assertEqual(fleet_module.running_instances(self.root), set())

    def test_an_unreadable_process_table_finds_nothing_rather_than_raising(self) -> None:
        self.assertEqual(fleet_module.running_instances(self.root / "nowhere"), set())


class FleetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.fleet = fleet_module.Fleet()

    def process(self, pid: int, name: str = "PracticeTakes") -> None:
        entry = self.root / str(pid)
        entry.mkdir()
        (entry / "comm").write_text(f"{name}\n")

    def test_only_what_was_started_is_ended(self) -> None:
        mine = FakeDriver(100)
        self.fleet.add(mine)
        theirs = FakeDriver(200)  # never registered

        self.assertEqual(self.fleet.shut_down(), 1)
        self.assertEqual(mine.killed, 1)
        self.assertEqual(theirs.killed, 0)

    def test_one_that_has_been_handed_back_is_not_ended_twice(self) -> None:
        done = FakeDriver(100)
        self.fleet.add(done)
        self.fleet.remove(done)

        self.assertEqual(self.fleet.shut_down(), 0)
        self.assertEqual(done.killed, 0)

    def test_somebody_elses_application_is_named_not_touched(self) -> None:
        """The whole point: it is normal to have the application open."""
        self.process(300)
        self.process(301)
        mine = FakeDriver(300)
        self.fleet.add(mine)

        self.assertEqual(self.fleet.foreign(self.root), {301})
        self.fleet.shut_down()
        self.assertEqual(mine.killed, 1)

    def test_one_this_run_started_and_finished_with_is_still_not_foreign(self) -> None:
        """It may not have exited yet; blaming it on somebody else would be wrong."""
        self.process(400)
        was_mine = FakeDriver(400)
        self.fleet.add(was_mine)
        self.fleet.remove(was_mine)

        self.assertEqual(self.fleet.foreign(self.root), set())

    def test_a_driver_with_no_process_is_ignored(self) -> None:
        self.fleet.add(FakeDriver(None))

        self.assertEqual(self.fleet.pids(), set())

    def test_one_that_will_not_die_does_not_stop_the_rest(self) -> None:
        class Stubborn(FakeDriver):
            def kill(inner) -> None:  # noqa: N805 - test double
                raise OSError("no")

        self.fleet.add(Stubborn(500))
        willing = FakeDriver(501)
        self.fleet.add(willing)

        self.fleet.shut_down()

        self.assertEqual(willing.killed, 1)


class ContentionMessageTests(unittest.TestCase):
    def test_nothing_running_says_nothing(self) -> None:
        self.assertEqual(fleet_module.contention_warning(set()), "")

    def test_the_message_names_what_is_running_and_what_it_costs(self) -> None:
        message = fleet_module.contention_warning({4242})

        self.assertIn("4242", message)
        self.assertIn("input device", message)

    def test_it_promises_not_to_close_them(self) -> None:
        """Said explicitly, because a tool that might close your editor is a
        tool you run differently."""
        self.assertIn("nothing here will", fleet_module.contention_warning({1, 2}))

    def test_it_counts_correctly(self) -> None:
        self.assertIn("instance is", fleet_module.contention_warning({1}))
        self.assertIn("instances are", fleet_module.contention_warning({1, 2}))


if __name__ == "__main__":
    unittest.main()
