#!/usr/bin/env python3
"""Tests for the unattended capture pass.

No X server, no application, no Pillow: the pass talks to a fake driver and a
fake window, so the rules that matter — a geometry has to be confirmed, a
failure is a row rather than a gap, a resumed pass does not recapture — are
verified in the ordinary Python suite.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import capture as capture_module  # noqa: E402
import images  # noqa: E402
import surfaces  # noqa: E402
from driver import Reply  # noqa: E402
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


class FakeDriver:
    """Answers the control channel without an application behind it."""

    def __init__(self, *, refuse: set[str] | None = None) -> None:
        self.pid = 4242
        self.refuse = refuse or set()
        self.opened: list[str] = []
        self.geometries: list[str] = []
        self.themes: list[str] = []
        self.restarts = 0

    def restart(self) -> None:
        self.restarts += 1

    def open_state(self, state: str) -> Reply:
        self.opened.append(state)

        if state in self.refuse:
            return Reply(False, [], f"no approved state '{state}'")

        return Reply(True, [])

    def set_geometry(self, geometry: str) -> str:
        self.geometries.append(geometry)

        return ""

    def set_theme(self, theme: str) -> str:
        self.themes.append(theme)

        return ""


class SettlingTests(unittest.TestCase):
    def test_a_size_that_settles_is_returned(self) -> None:
        sizes = iter([(800, 600), (1280, 800), (1280, 800), (1280, 800), (1280, 800)])
        clock = iter(range(100))

        result = capture_module.settled_size(
            lambda: next(sizes, (1280, 800)),
            sleep=lambda _: None,
            clock=lambda: next(clock) * 0.1,
        )

        self.assertEqual(result, (1280, 800))

    def test_a_window_that_never_settles_gives_up(self) -> None:
        """Better a recorded failure than a capture of a window mid-resize."""
        widths = iter(range(400, 4000, 7))
        clock = iter([index * 0.5 for index in range(100)])

        result = capture_module.settled_size(
            lambda: (next(widths), 600),
            settle_seconds=2.0,
            sleep=lambda _: None,
            clock=lambda: next(clock),
        )

        self.assertIsNone(result)

    def test_a_window_that_cannot_be_read_gives_up(self) -> None:
        clock = iter([index * 0.5 for index in range(100)])

        result = capture_module.settled_size(
            lambda: None, settle_seconds=2.0, sleep=lambda _: None, clock=lambda: next(clock)
        )

        self.assertIsNone(result)


class GeometryVerificationTests(unittest.TestCase):
    def test_a_confirmed_geometry_is_no_problem(self) -> None:
        self.assertEqual(
            capture_module.geometry_problem("constrained", (800, 600), {"default": (1280, 800)}),
            "",
        )

    def test_a_window_that_ignored_the_request_is_a_failure(self) -> None:
        """The bug this exists for: three sizes captured, one window size."""
        problem = capture_module.geometry_problem(
            "constrained", (1280, 800), {"default": (1280, 800)}
        )

        self.assertIn("stayed at 1280x800", problem)
        self.assertIn("constrained", problem)

    def test_an_unsettled_window_is_a_failure(self) -> None:
        self.assertIn(
            "never settled", capture_module.geometry_problem("default", None, {})
        )

    def test_the_first_resolution_has_nothing_to_contradict(self) -> None:
        self.assertEqual(capture_module.geometry_problem("default", (1280, 800), {}), "")


class DuplicateCaptureTests(unittest.TestCase):
    """The backstop for photographing the wrong window."""

    def test_two_states_with_identical_pixels_are_flagged(self) -> None:
        problem = capture_module.duplicate_problem(
            "settings-open", "default", "abc", {("default", "abc"): "empty"}
        )

        self.assertIn("identical to 'empty'", problem)
        self.assertIn("photographed that window", problem)

    def test_the_same_state_twice_is_expected(self) -> None:
        """One state serves several surfaces; those images should match."""
        self.assertEqual(
            capture_module.duplicate_problem(
                "settings-open", "default", "abc", {("default", "abc"): "settings-open"}
            ),
            "",
        )

    def test_the_same_picture_at_another_resolution_is_not_a_duplicate(self) -> None:
        """`narrow-window` *is* the tuner at the narrow geometry, which is what
        the sweep's constrained resolution asks for. Those two captures are the
        same picture by definition, and flagging it failed a good capture."""
        self.assertEqual(
            capture_module.duplicate_problem(
                "narrow-window", "default", "abc", {("constrained", "abc"): "tuner-docked"}
            ),
            "",
        )

    def test_a_new_image_is_no_problem(self) -> None:
        self.assertEqual(
            capture_module.duplicate_problem("empty", "default", "abc", {("default", "def"): "other"}),
            "",
        )


class WindowTargetingTests(unittest.TestCase):
    """Surfaces whose subject is not the main window."""

    def test_the_settings_surfaces_name_their_window(self) -> None:
        for surface in surfaces.SURFACES:
            if surface.state.startswith("settings"):
                with self.subTest(surface=surface.title):
                    self.assertEqual(surface.window_title, surfaces.SETTINGS_WINDOW)

    def test_a_surface_with_its_own_window_is_captured_once(self) -> None:
        """The geometry command resizes the main window, not this one."""
        for surface in surfaces.SURFACES:
            if surface.window_title:
                with self.subTest(surface=surface.title):
                    self.assertEqual(
                        surfaces.resolutions_for_surface(surface, surfaces.SWEEP_GEOMETRIES),
                        (surfaces.DEFAULT_GEOMETRY,),
                    )


class CapturePassTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.store = Store.open(self.root / "verification.db")
        self.addCleanup(self.store.close)
        self.run_id = self.store.start_run(
            provenance=PROVENANCE,
            commit="abc123",
            mode=surfaces.QUICK,
            resolutions=("default", "constrained"),
        )
        self.driver = FakeDriver()
        self.converted = 0

        # The one dependency the pass has on a real image library.
        self.original_convert = images.convert
        images.convert = self.fake_convert
        self.addCleanup(setattr, images, "convert", self.original_convert)

    def fake_convert(self, source: Path, png_path: Path, thumbnail_path: Path):
        png_path.write_bytes(b"png")
        thumbnail_path.write_bytes(b"thumb")
        self.converted += 1

        # Distinct per capture: identical digests across surfaces are what the
        # duplicate check treats as a capture of the wrong window.
        return 1280, 800, f"digest-{self.converted}"

    def make_pass(self, sizes: dict[str, tuple[int, int]] | None = None, **overrides):
        sizes = sizes or {"default": (1280, 800), "constrained": (800, 600), "maximised": (2560, 1440)}
        requested: list[str] = []

        class TestablePass(capture_module.CapturePass):
            def read_size(inner, title: str = ""):  # noqa: N805 - test double
                return sizes.get(requested[-1] if requested else "default")

            def _capture_to(inner, destination: Path, title: str = "") -> str:  # noqa: N805
                destination.write_bytes(b"P6 fake")

                return overrides.get("capture_failure", "")

            def move_to(inner, surface, geometry, theme=""):  # noqa: N805 - test double
                requested.append(geometry)

                return capture_module.CapturePass.move_to(inner, surface, geometry, theme)

        return TestablePass(
            store=self.store,
            run_id=self.run_id,
            driver=self.driver,
            tooling=capture_module.Tooling(Path("capture"), Path("control")),
            image_directory=self.root / "images",
            settle_seconds=2.0,
            poll_interval=0.0,
        )

    def plan(self) -> tuple:
        return surfaces.plan(surfaces.QUICK, ("default", "constrained"), (surfaces.DARK,))

    def test_every_surface_is_captured_at_every_resolution(self) -> None:
        result = self.make_pass().run(self.plan())
        captures = self.store.captures(self.run_id)

        self.assertEqual(result["failed"], 0)
        self.assertEqual(len(captures), len(self.plan()))
        self.assertEqual({capture.geometry for capture in captures}, {"default", "constrained"})

    def test_a_capture_records_its_image_and_size(self) -> None:
        self.make_pass().run(self.plan())
        capture = self.store.captures(self.run_id)[0]

        self.assertTrue(Path(capture.image_path).exists())
        self.assertTrue(Path(capture.thumbnail_path).exists())
        self.assertTrue(capture.digest.startswith("digest-"))

    def test_a_window_that_never_resized_is_recorded_as_a_failure(self) -> None:
        stuck = self.make_pass(sizes={"default": (1280, 800), "constrained": (1280, 800)})
        result = stuck.run(self.plan())
        failures = [capture for capture in self.store.captures(self.run_id) if capture.failed]

        self.assertTrue(result["failed"])
        self.assertTrue(failures)
        self.assertIn("stayed at 1280x800", failures[0].failure)

    def test_an_unreachable_surface_is_a_failure_and_the_pass_continues(self) -> None:
        self.driver.refuse = {"settings-open"}
        result = self.make_pass().run(self.plan())
        captures = self.store.captures(self.run_id)
        failures = [capture for capture in captures if capture.failed]

        self.assertTrue(failures)
        self.assertIn("could not reach the surface", failures[0].failure)
        self.assertEqual(len(captures), len(self.plan()))
        self.assertTrue(result["captured"])

    def test_a_capture_utility_failure_is_recorded(self) -> None:
        broken = self.make_pass(capture_failure="no matching top-level window")
        broken.run(self.plan())
        failures = [capture for capture in self.store.captures(self.run_id) if capture.failed]

        self.assertEqual(len(failures), len(self.plan()))
        self.assertIn("could not be captured", failures[0].failure)

    def test_an_identical_capture_is_kept_with_a_notice(self) -> None:
        """It began as a failure and threw away good images."""
        self.converted_digest = "same"
        images.convert = lambda source, png, thumb: (
            png.write_bytes(b"png"), thumb.write_bytes(b"thumb"), (1280, 800, "same"))[2]

        self.make_pass().run(self.plan())
        captures = self.store.captures(self.run_id)
        flagged = [capture for capture in captures if capture.notice]

        self.assertTrue(flagged)
        self.assertFalse([capture for capture in captures if capture.failed])
        self.assertTrue(all(Path(capture.image_path).exists() for capture in captures))
        self.assertIn("identical to", flagged[0].notice)

    def test_a_resumed_pass_does_not_recapture(self) -> None:
        self.make_pass().run(self.plan())
        result = self.make_pass().run(self.plan())

        self.assertEqual(result["captured"], 0)
        self.assertEqual(result["already_captured"], len(self.plan()))

    def test_a_fixed_geometry_surface_is_captured_once(self) -> None:
        """Resizing it would destroy the thing under test."""
        fullscreen = next(s for s in surfaces.SURFACES if s.state == "fullscreen")
        plan = surfaces.plan(surfaces.FULL, ("default", "constrained", "maximised"),
                             (surfaces.DARK,))
        entries = [geometry for surface, geometry, _ in plan if surface is fullscreen]

        self.assertEqual(entries, ["default"])

    def test_a_surface_that_wants_a_restart_gets_one(self) -> None:
        restarting = next(s for s in surfaces.SURFACES if s.restart_before)
        self.make_pass().capture_one(restarting, "default", {})

        self.assertEqual(self.driver.restarts, 1)

    def test_the_palette_is_applied_after_the_state(self) -> None:
        """Opening a state rebuilds the workspace; a palette set first goes stale."""
        surface = surfaces.SURFACES[0]
        self.make_pass().capture_one(surface, "default", {}, {}, surfaces.LIGHT)

        self.assertEqual(self.driver.themes, [surfaces.LIGHT])
        self.assertEqual(self.driver.opened, [surface.state])

    def test_each_palette_is_captured_separately(self) -> None:
        plan = surfaces.plan(surfaces.QUICK, ("default",), surfaces.THEMES)
        self.make_pass().run(plan)
        captures = self.store.captures(self.run_id)

        self.assertEqual(len(captures), len(plan))
        self.assertEqual({capture.theme for capture in captures}, set(surfaces.THEMES))


if __name__ == "__main__":
    unittest.main()
