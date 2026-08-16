#!/usr/bin/env python3
"""Tests for the private capture display.

Xvfb is not installed everywhere, and these run where it is not, so nothing here
starts a server. What is worth testing without one is the part that goes wrong
quietly: picking a display number that is already taken would make the run
photograph somebody else's screen, and a missing Xvfb has to say which package
to install rather than surfacing as a connection refusal from a tool three
layers down.
"""

from __future__ import annotations

import os
from pathlib import Path
import sys
import threading
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import display  # noqa: E402


class FindFreeDisplayTests(unittest.TestCase):
    def test_the_first_unused_number_is_chosen(self) -> None:
        with mock.patch.object(display, "display_in_use", lambda number: number < 93):
            self.assertEqual(display.find_free_display(90, 100), 93)

    def test_a_range_with_nothing_free_is_an_error_not_a_collision(self) -> None:
        """Returning a taken number would capture whatever is on that screen."""
        with mock.patch.object(display, "display_in_use", lambda number: True):
            with self.assertRaises(display.VirtualDisplayError):
                display.find_free_display(90, 92)

    def test_the_private_range_starts_above_real_sessions(self) -> None:
        """:0 is somebody's desktop. Never hand it out."""
        self.assertGreater(display.FIRST_DISPLAY_NUMBER, 0)


class AvailabilityTests(unittest.TestCase):
    def test_a_missing_xvfb_is_reported_with_the_way_to_fix_it(self) -> None:
        message = display.missing_message()

        self.assertIn("Xvfb", message)
        self.assertIn("check-linux-build-dependencies.sh", message)
        self.assertIn("--headless", message)

    def test_the_context_manager_refuses_rather_than_falling_back(self) -> None:
        """Silently using the desktop display is the one thing it must not do.

        A fallback would put windows on the screen of somebody who asked for
        headless precisely so that would not happen.
        """
        with mock.patch.object(display, "is_available", return_value=False):
            with self.assertRaises(display.VirtualDisplayError):
                with display.virtual_display():
                    self.fail("the body must not run without a display")


class ScreenSizeTests(unittest.TestCase):
    def test_the_screen_is_wider_than_the_widest_capture_geometry(self) -> None:
        """`maximised` asks JUCE for the display's user area.

        On a screen smaller than the ordinary window, the widest capture would
        quietly become the narrowest, and the resulting image would look right
        while testing the wrong thing.
        """
        self.assertGreaterEqual(display.DEFAULT_WIDTH, 1280)
        self.assertGreaterEqual(display.DEFAULT_HEIGHT, 800)


if __name__ == "__main__":
    unittest.main()


class SeveralDisplaysAtOnceTests(unittest.TestCase):
    """Choosing display numbers from more than one thread.

    No Xvfb: the server is replaced by a stand-in that registers its number as
    taken, which is the only part of a real one this needs. The race being
    guarded against has a window of milliseconds and only appears when captures
    run in parallel — the case where the symptom is a worker that quietly never
    started rather than an error anyone would see.
    """

    def setUp(self) -> None:
        self.taken: set[int] = set()
        self.lock = threading.Lock()
        self.started: list[int] = []

        original_in_use = display.display_in_use
        display.display_in_use = lambda number: number in self.taken
        self.addCleanup(setattr, display, "display_in_use", original_in_use)

        original_popen = display.subprocess.Popen
        display.subprocess.Popen = self.fake_server
        self.addCleanup(setattr, display.subprocess, "Popen", original_popen)

        original_ready = display.wait_until_ready
        display.wait_until_ready = lambda number, process, timeout=0: None
        self.addCleanup(setattr, display, "wait_until_ready", original_ready)

        original_available = display.is_available
        display.is_available = lambda: True
        self.addCleanup(setattr, display, "is_available", original_available)

    def fake_server(self, arguments, **_):
        """Stands in for Xvfb, and claims the number the way a real one would."""
        number = int(str(arguments[1]).lstrip(":"))
        # A real server binds its socket a moment after starting; the delay is
        # what makes the race reachable at all.
        time.sleep(0.005)

        with self.lock:
            self.taken.add(number)
            self.started.append(number)

        class Server:
            returncode = 0

            def poll(inner):  # noqa: N805 - test double
                return None

            def terminate(inner) -> None:  # noqa: N805 - test double
                return None

            def wait(inner, timeout=None) -> int:  # noqa: N805 - test double
                return 0

            def kill(inner) -> None:  # noqa: N805 - test double
                return None

        return Server()

    def test_two_at_once_get_different_numbers(self) -> None:
        names: list[str] = []
        barrier = threading.Barrier(4)

        def take() -> None:
            barrier.wait()

            with display.virtual_display(publish=False) as name:
                with self.lock:
                    names.append(name)

                time.sleep(0.02)

        threads = [threading.Thread(target=take) for _ in range(4)]

        for thread in threads:
            thread.start()

        for thread in threads:
            thread.join(10)

        self.assertEqual(len(names), 4)
        self.assertEqual(len(set(names)), 4, f"two workers were given one screen: {names}")

    def test_a_display_that_will_not_come_up_is_tried_again(self) -> None:
        """A number free when it was chosen can be taken a moment later.

        Xvfb says so by exiting immediately, which `wait_until_ready` reports.
        Giving up on the first one would lose a worker to a collision that
        costs nothing to retry.
        """
        refused: list[int] = []
        original = display.wait_until_ready

        def first_one_fails(number, process, timeout=0):
            if not refused:
                refused.append(number)

                raise display.VirtualDisplayError("that display is taken")

            return None

        display.wait_until_ready = first_one_fails
        self.addCleanup(setattr, display, "wait_until_ready", original)

        with display.virtual_display(publish=False) as name:
            self.assertEqual(len(refused), 1)
            self.assertNotEqual(name, f":{refused[0]}")

    def test_a_display_that_never_comes_up_gives_up_rather_than_spinning(self) -> None:
        original = display.wait_until_ready

        def never(number, process, timeout=0):
            raise display.VirtualDisplayError("Xvfb did not come up")

        display.wait_until_ready = never
        self.addCleanup(setattr, display, "wait_until_ready", original)

        with self.assertRaises(display.VirtualDisplayError):
            with display.virtual_display(publish=False):
                pass

        self.assertEqual(len(self.started), display.SELECTION_ATTEMPTS)

    def test_publishing_is_what_changes_the_environment(self) -> None:
        before = os.environ.get("DISPLAY")

        with display.virtual_display(publish=False) as name:
            self.assertEqual(os.environ.get("DISPLAY"), before)
            self.assertTrue(name.startswith(":"))

        with display.virtual_display(publish=True) as name:
            self.assertEqual(os.environ.get("DISPLAY"), name)

        self.assertEqual(os.environ.get("DISPLAY"), before)


class ScreenSizeGuardTests(unittest.TestCase):
    """A screen of no size is not a smaller problem than no screen.

    Xvfb accepts `0x0x24` and starts. Every window on it is then zero by zero,
    and nothing downstream questions that: the capture succeeds, the image is
    empty, and the run reports it captured.
    """

    def test_the_default_is_a_real_size(self) -> None:
        self.assertGreater(display.DEFAULT_WIDTH, 0)
        self.assertGreater(display.DEFAULT_HEIGHT, 0)

    def test_the_fallback_is_a_real_size(self) -> None:
        self.assertGreaterEqual(display.FALLBACK_WIDTH, 640)
        self.assertGreaterEqual(display.FALLBACK_HEIGHT, 480)

    def test_a_screen_with_no_size_falls_back(self) -> None:
        asked: list[tuple[int, int]] = []

        def record(width, height, depth):
            asked.append((width, height))

            raise display.VirtualDisplayError("not starting one in a test")

        original_start = display._start_server
        display._start_server = record
        self.addCleanup(setattr, display, "_start_server", original_start)

        original_available = display.is_available
        display.is_available = lambda: True
        self.addCleanup(setattr, display, "is_available", original_available)

        for size in ((0, 0), (0, 1200), (1920, 0), (-1, -1)):
            with self.subTest(size=size):
                asked.clear()

                with self.assertRaises(display.VirtualDisplayError):
                    with display.virtual_display(width=size[0], height=size[1]):
                        pass

                self.assertEqual(
                    asked, [(display.FALLBACK_WIDTH, display.FALLBACK_HEIGHT)]
                )

    def test_a_real_size_is_left_alone(self) -> None:
        asked: list[tuple[int, int]] = []

        def record(width, height, depth):
            asked.append((width, height))

            raise display.VirtualDisplayError("not starting one in a test")

        original_start = display._start_server
        display._start_server = record
        self.addCleanup(setattr, display, "_start_server", original_start)

        original_available = display.is_available
        display.is_available = lambda: True
        self.addCleanup(setattr, display, "is_available", original_available)

        with self.assertRaises(display.VirtualDisplayError):
            with display.virtual_display(width=1280, height=800):
                pass

        self.assertEqual(asked, [(1280, 800)])
