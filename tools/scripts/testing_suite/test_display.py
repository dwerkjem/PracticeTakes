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

from pathlib import Path
import sys
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
