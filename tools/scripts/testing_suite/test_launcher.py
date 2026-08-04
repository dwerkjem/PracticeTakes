#!/usr/bin/env python3
"""Tests for opening a live application on a surface.

No application is started here: the driver is replaced, so what is under test is
the sequencing — state, then palette, then size, and only one instance at a
time.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import launcher as launcher_module  # noqa: E402
import surfaces  # noqa: E402
from driver import Reply  # noqa: E402


class FakeDriver:
    started = 0

    def __init__(self, executable, *_: object) -> None:
        self.executable = executable
        self.pid: int | None = None
        self.opened: list[str] = []
        self.themes: list[str] = []
        self.geometries: list[str] = []
        self.stopped = False
        self.order: list[str] = []
        FakeDriver.started += 1

    def start(self) -> None:
        self.pid = 4242

    def stop(self) -> None:
        self.stopped = True
        self.pid = None

    def open_state(self, state: str) -> Reply:
        self.opened.append(state)
        self.order.append("state")

        return Reply(state != "refused", [], "" if state != "refused" else "no such state")

    def set_theme(self, theme: str) -> str:
        self.themes.append(theme)
        self.order.append("theme")

        return "" if theme != "broken" else "cannot apply"

    def set_geometry(self, geometry: str) -> str:
        self.geometries.append(geometry)
        self.order.append("geometry")

        return ""


class LaunchTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.executable = Path(self.directory.name) / "PracticeTakes"
        self.executable.write_text("#!/bin/sh\n")
        self.executable.chmod(0o755)

        self.drivers: list[FakeDriver] = []

        def make(executable, *rest):
            driver = FakeDriver(executable, *rest)
            self.drivers.append(driver)

            return driver

        original = launcher_module.ApplicationDriver
        launcher_module.ApplicationDriver = make
        self.addCleanup(setattr, launcher_module, "ApplicationDriver", original)

        self.launch = launcher_module.ManualLaunch(self.executable)
        self.addCleanup(self.launch.close)

    def test_a_surface_opens_and_stays_open(self) -> None:
        """Nobody quits it: that is the whole difference from a capture."""
        status = self.launch.open(state="tuner-docked", geometry="default", theme="dark")

        self.assertTrue(status["running"])
        self.assertEqual(self.drivers[0].opened, ["tuner-docked"])
        self.assertFalse(self.drivers[0].stopped)

    def test_the_palette_is_applied_after_the_state(self) -> None:
        self.launch.open(state="tuner-docked", theme="light", geometry="constrained")

        self.assertEqual(self.drivers[0].order, ["state", "theme", "geometry"])

    def test_the_default_size_needs_no_resize(self) -> None:
        """The window opens at it, and asking again is a resize for nothing."""
        self.launch.open(state="tuner-docked", geometry="default", theme="dark")

        self.assertEqual(self.drivers[0].geometries, [])

    def test_opening_another_surface_replaces_the_first(self) -> None:
        self.launch.open(state="tuner-docked")
        self.launch.open(state="empty")

        self.assertTrue(self.drivers[0].stopped)
        self.assertFalse(self.drivers[1].stopped)
        self.assertEqual(self.launch.status()["state"], "empty")

    def test_a_surface_the_suite_does_not_know_is_refused(self) -> None:
        with self.assertRaises(launcher_module.LaunchError):
            self.launch.open(state="invented")

        self.assertEqual(self.drivers, [])

    def test_a_state_the_application_refuses_stops_the_instance(self) -> None:
        """Otherwise a failed launch leaves a stray application running."""
        known = next(iter(surfaces.required_states()))
        original_states = surfaces.required_states

        surfaces.required_states = lambda: frozenset({*original_states(), "refused"})
        self.addCleanup(setattr, surfaces, "required_states", original_states)

        with self.assertRaises(launcher_module.LaunchError):
            self.launch.open(state="refused")

        self.assertTrue(self.drivers[0].stopped)
        self.assertFalse(self.launch.running)
        self.assertTrue(known)

    def test_a_palette_that_cannot_be_applied_stops_the_instance(self) -> None:
        with self.assertRaises(launcher_module.LaunchError):
            self.launch.open(state="tuner-docked", theme="broken")

        self.assertTrue(self.drivers[0].stopped)

    def test_closing_stops_it(self) -> None:
        self.launch.open(state="tuner-docked")
        status = self.launch.close()

        self.assertFalse(status["running"])
        self.assertTrue(self.drivers[0].stopped)

    def test_closing_when_nothing_is_open_is_harmless(self) -> None:
        self.assertFalse(self.launch.close()["running"])

    def test_a_missing_build_says_so_rather_than_failing_obscurely(self) -> None:
        self.executable.unlink()

        with self.assertRaises(launcher_module.LaunchError) as raised:
            self.launch.open(state="tuner-docked")

        self.assertIn("PRACTICE_TAKES_ENABLE_TEST_CONTROL", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
