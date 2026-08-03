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

        # The one dependency the pass has on a real image library.
        self.original_convert = images.convert
        images.convert = self.fake_convert
        self.addCleanup(setattr, images, "convert", self.original_convert)

    def fake_convert(self, source: Path, png_path: Path, thumbnail_path: Path):
        png_path.write_bytes(b"png")
        thumbnail_path.write_bytes(b"thumb")

        return 1280, 800, "digest"

    def make_pass(self, sizes: dict[str, tuple[int, int]] | None = None, **overrides):
        sizes = sizes or {"default": (1280, 800), "constrained": (800, 600), "maximised": (2560, 1440)}
        requested: list[str] = []

        class TestablePass(capture_module.CapturePass):
            def park_pointer(inner) -> None:  # noqa: N805 - test double
                return

            def read_size(inner):  # noqa: N805 - test double
                return sizes.get(requested[-1] if requested else "default")

            def _capture_to(inner, destination: Path) -> str:  # noqa: N805 - test double
                destination.write_bytes(b"P6 fake")

                return overrides.get("capture_failure", "")

            def move_to(inner, surface, geometry):  # noqa: N805 - test double
                requested.append(geometry)

                return capture_module.CapturePass.move_to(inner, surface, geometry)

        return TestablePass(
            store=self.store,
            run_id=self.run_id,
            driver=self.driver,
            tooling=capture_module.Tooling(Path("capture"), Path("control"), Path("pointer")),
            image_directory=self.root / "images",
            settle_seconds=2.0,
            poll_interval=0.0,
        )

    def plan(self) -> tuple:
        return surfaces.plan(surfaces.QUICK, ("default", "constrained"))

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
        self.assertEqual(capture.digest, "digest")

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

    def test_a_resumed_pass_does_not_recapture(self) -> None:
        self.make_pass().run(self.plan())
        result = self.make_pass().run(self.plan())

        self.assertEqual(result["captured"], 0)
        self.assertEqual(result["already_captured"], len(self.plan()))

    def test_a_fixed_geometry_surface_is_captured_once(self) -> None:
        """Resizing it would destroy the thing under test."""
        fullscreen = next(s for s in surfaces.SURFACES if s.state == "fullscreen")
        plan = surfaces.plan(surfaces.FULL, ("default", "constrained", "maximised"))
        entries = [geometry for surface, geometry in plan if surface is fullscreen]

        self.assertEqual(entries, ["default"])

    def test_a_surface_that_wants_a_restart_gets_one(self) -> None:
        restarting = next(s for s in surfaces.SURFACES if s.restart_before)
        self.make_pass().capture_one(restarting, "default", {})

        self.assertEqual(self.driver.restarts, 1)


if __name__ == "__main__":
    unittest.main()
