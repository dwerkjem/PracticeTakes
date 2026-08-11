#!/usr/bin/env python3
"""Tests for the unattended capture pass.

No X server, no application, no Pillow: the pass talks to a fake driver and a
fake window, so the rules that matter — a geometry has to be confirmed, a
failure is a row rather than a gap, a resumed pass does not recapture — are
verified in the ordinary Python suite.
"""

from __future__ import annotations

from contextlib import contextmanager
from pathlib import Path
import sys
import tempfile
import threading
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


class StoppingTests(unittest.TestCase):
    """A run that is stopped part way.

    The distinction these protect is that a stopped run reports no failures. The
    export feeds a release gate, so a run full of failures that only means
    somebody pressed stop would either block a release or teach everyone that
    failures can be ignored -- and the second is the expensive one, because it
    lasts.
    """

    def plan(self, length: int):
        surface = surfaces.surfaces_for_mode(surfaces.FULL)[0]

        return tuple(
            (surface, geometry, surfaces.DARK)
            for geometry in (surfaces.SWEEP_GEOMETRIES * length)[:length]
        )

    def test_a_run_stopped_before_it_starts_captures_nothing(self) -> None:
        captured: list = []

        result = _StubPass(captured).run(self.plan(4), should_stop=lambda: True)

        self.assertEqual(captured, [])
        self.assertEqual(result["captured"], 0)
        self.assertEqual(result["failed"], 0)
        self.assertTrue(result["stopped"])

    def test_stopping_part_way_keeps_what_came_before(self) -> None:
        captured: list = []
        calls = {"n": 0}

        def should_stop() -> bool:
            calls["n"] += 1

            # Stop once two surfaces have been taken.
            return calls["n"] > 2

        result = _StubPass(captured).run(self.plan(4), should_stop=should_stop)

        self.assertEqual(len(captured), 2)
        self.assertEqual(result["captured"], 2)
        self.assertTrue(result["stopped"])

    def test_a_stopped_run_reports_no_failures(self) -> None:
        result = _StubPass([]).run(self.plan(4), should_stop=lambda: True)

        self.assertEqual(result["failed"], 0)
        self.assertGreater(result["not_reached"], 0)

    def test_a_finished_run_is_not_marked_stopped(self) -> None:
        captured: list = []

        result = _StubPass(captured).run(self.plan(3), should_stop=lambda: False)

        self.assertEqual(len(captured), 3)
        self.assertFalse(result["stopped"])
        self.assertEqual(result["not_reached"], 0)

    def test_no_stop_callback_behaves_as_before(self) -> None:
        captured: list = []

        result = _StubPass(captured).run(self.plan(3))

        self.assertEqual(len(captured), 3)
        self.assertFalse(result["stopped"])


class _StubPass(capture_module.CapturePass):
    """A pass that records which surfaces it was asked for and captures none.

    Only the loop is under test here: whether it stops where it was told to and
    what it says afterwards. Photographing anything would need a display.
    """

    def __init__(self, captured: list) -> None:  # noqa: D107 - test double
        self.captured = captured
        self.store = _StubStore()
        self.run_id = 1
        self.lock = threading.Lock()

    def capture_one(self, surface, geometry, seen, digests, theme):  # type: ignore[override]
        self.captured.append((surface.state, geometry, theme))

        return (1280, 800)


class _StubStore:
    def captured_keys(self, run_id: int) -> set:
        return set()


class SharingThePlanTests(unittest.TestCase):
    """Splitting a plan between workers without losing or repeating anything."""

    def plan(self, count: int = 6) -> tuple:
        return surfaces.plan(surfaces.QUICK, ("default", "constrained"), (surfaces.DARK,))[:count]

    def test_one_worker_gets_the_whole_plan_unchanged(self) -> None:
        plan = self.plan()

        self.assertEqual(capture_module.share_plan(plan, 1), [list(plan)])

    def test_every_entry_lands_exactly_once(self) -> None:
        plan = surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, surfaces.THEMES)

        for workers in (1, 2, 3, 4, 8):
            with self.subTest(workers=workers):
                shares = capture_module.share_plan(plan, workers)
                dealt = [entry for share in shares for entry in share]

                self.assertEqual(len(dealt), len(plan))
                self.assertEqual(
                    sorted((s.state, s.title, g, t) for s, g, t in dealt),
                    sorted((s.state, s.title, g, t) for s, g, t in plan),
                )

    def test_a_surface_keeps_its_resolutions_together(self) -> None:
        """Opening the state is the expensive part; four workers would do it four times."""
        plan = surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, surfaces.THEMES)

        for workers in (2, 4):
            with self.subTest(workers=workers):
                where: dict[tuple[str, str, str], set[int]] = {}

                for index, share in enumerate(capture_module.share_plan(plan, workers)):
                    for surface, _, theme in share:
                        where.setdefault((surface.state, surface.title, theme), set()).add(index)

                split = {key for key, workers_seen in where.items() if len(workers_seen) > 1}

                self.assertEqual(split, set(), "a surface was split across workers")

    def test_more_workers_than_groups_leaves_some_empty(self) -> None:
        shares = capture_module.share_plan(self.plan(2), 8)

        self.assertEqual(sum(len(share) for share in shares), 2)

    def test_no_workers_is_refused(self) -> None:
        for workers in (0, -1):
            with self.subTest(workers=workers):
                with self.assertRaises(ValueError):
                    capture_module.share_plan(self.plan(), workers)


class ParallelCaptureTests(unittest.TestCase):
    """Several passes, one run.

    The point of the tests here is not that it is faster -- it is that a run
    split between workers is the same run. Two of the pass's checks work by
    comparing captures against *other* captures, and both would develop holes if
    each worker could only see its own share while still reporting success.
    """

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
        self.converted = 0
        self.digest_per_capture = True
        self.sizes = {"default": (1280, 800), "constrained": (800, 600)}
        self.displays: list[str] = []
        self.broken: set[int] = set()

        self.original_convert = images.convert
        images.convert = self.fake_convert
        self.addCleanup(setattr, images, "convert", self.original_convert)

    def fake_convert(self, source: Path, png_path: Path, thumbnail_path: Path):
        png_path.write_bytes(b"png")
        thumbnail_path.write_bytes(b"thumb")
        self.converted += 1
        digest = f"digest-{self.converted}" if self.digest_per_capture else "one-digest"

        return 1280, 800, digest

    @contextmanager
    def worker(self, index: int, lock):
        """A screen and an application of this worker's own, faked."""
        outer = self
        display = f":{90 + len(self.displays)}"
        self.displays.append(display)
        requested: list[str] = []

        class TestablePass(capture_module.CapturePass):
            def read_size(inner, title: str = ""):  # noqa: N805 - test double
                return outer.sizes.get(requested[-1] if requested else "default")

            def _capture_to(inner, destination: Path, title: str = "") -> str:  # noqa: N805
                destination.write_bytes(b"P6 fake")

                return ""

            def move_to(inner, surface, geometry, theme=""):  # noqa: N805 - test double
                requested.append(geometry)

                return capture_module.CapturePass.move_to(inner, surface, geometry, theme)

        if index in self.broken:
            raise RuntimeError("this worker's display never came up")

        yield TestablePass(
            store=self.store,
            run_id=self.run_id,
            driver=FakeDriver(),
            tooling=capture_module.Tooling(Path("capture"), Path("control")),
            image_directory=self.root / "images",
            settle_seconds=2.0,
            poll_interval=0.0,
            # The warmup is a real sleep for the tone surfaces, and none of
            # what is under test here needs it to have happened.
            sleep=lambda _: None,
            display=display,
            lock=lock,
        )

    def plan(self) -> tuple:
        return surfaces.plan(surfaces.QUICK, ("default", "constrained"), (surfaces.DARK,))

    def capture_with(self, workers: int) -> dict:
        return capture_module.run_in_parallel(
            self.plan(), workers=workers, worker=self.worker
        )

    def rows(self) -> list[tuple]:
        return sorted(
            (row.surface_state, row.surface_title, row.geometry, row.theme)
            for row in self.store.captures(self.run_id)
        )

    def test_a_parallel_run_captures_what_a_sequential_one_does(self) -> None:
        alone = self.capture_with(1)
        sequential = self.rows()

        self.setUp()
        together = self.capture_with(3)

        self.assertEqual(self.rows(), sequential)
        self.assertEqual(together["captured"], alone["captured"])
        self.assertEqual(together["failed"], 0)

    def test_nothing_is_captured_twice(self) -> None:
        self.capture_with(4)
        rows = self.rows()

        self.assertEqual(len(rows), len(set(rows)))

    def test_each_worker_gets_a_display_of_its_own(self) -> None:
        self.capture_with(3)

        self.assertEqual(len(self.displays), len(set(self.displays)))
        self.assertEqual(len(self.displays), 3)

    def test_the_duplicate_check_still_sees_across_workers(self) -> None:
        """The check this change most risks weakening.

        Built so the collision can *only* be found across workers: two states,
        one capture each, one worker each, and the same pixels from both. A
        worker keeping its own map has nothing of the other's to compare
        against, so it reports success on exactly the pair the check exists to
        catch.

        Written first with a plan big enough that each worker also collided with
        itself, which passed with the maps split -- and proved nothing.
        """
        self.digest_per_capture = False
        two_states = surfaces.plan(surfaces.QUICK, ("default",), (surfaces.DARK,))[:2]
        self.assertNotEqual(two_states[0][0].state, two_states[1][0].state)

        capture_module.run_in_parallel(two_states, workers=2, worker=self.worker)

        rows = list(self.store.captures(self.run_id))
        notices = [row.notice for row in rows if row.notice]

        self.assertEqual(len(rows), 2)
        self.assertEqual(len(notices), 1, "the cross-worker duplicate was not reported")
        self.assertIn("identical to", notices[0])

    def test_a_window_that_ignores_a_geometry_is_still_caught(self) -> None:
        self.sizes = {"default": (1280, 800), "constrained": (1280, 800)}
        result = self.capture_with(3)

        self.assertGreater(result["failed"], 0)
        failures = [row.failure for row in self.store.captures(self.run_id) if row.failure]

        self.assertTrue(any("stayed at 1280x800" in failure for failure in failures))

    def test_a_worker_that_dies_does_not_take_the_run_with_it(self) -> None:
        self.broken = {1}
        result = self.capture_with(3)

        self.assertGreater(result["captured"], 0)
        self.assertGreater(result["failed"], 0)
        self.assertTrue(result["errors"])
        self.assertIn("never came up", result["errors"][0])

    def test_no_workers_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            self.capture_with(0)
