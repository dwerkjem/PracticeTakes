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
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import capture as capture_module  # noqa: E402
import images  # noqa: E402
import surfaces  # noqa: E402
from driver import ChannelError, Reply  # noqa: E402
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

    def capture_one(self, surface, geometry, seen, digests, theme, warm=True):  # type: ignore[override]
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
    def worker(self, index: int, shared):
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
            lock=shared.checks,
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
        """And loses nothing, now that work is taken rather than dealt.

        A worker whose screen never came up has no share to lose: the groups it
        would have been given are still in the queue, and whoever is free takes
        them. The failure is still reported — a run that quietly captured
        everything with a broken worker would hide a broken worker.
        """
        self.broken = {1}
        alone = self.capture_with(1)
        expected = self.rows()

        self.setUp()
        self.broken = {1}
        result = self.capture_with(3)

        self.assertTrue(result["errors"])
        self.assertIn("never came up", result["errors"][0])
        self.assertEqual(result["captured"], alone["captured"])
        self.assertEqual(self.rows(), expected)

    def test_every_worker_dying_is_not_a_silent_success(self) -> None:
        self.broken = {0, 1, 2}
        result = self.capture_with(3)

        self.assertEqual(result["captured"], 0)
        self.assertEqual(len(result["errors"]), 3)

    def test_work_is_taken_rather_than_dealt(self) -> None:
        """The point of the queue: one slow worker does not hold the run open.

        Dealt in advance, whoever drew the surfaces carrying a tone would still
        be settling while everyone else had stopped.
        """
        taken: dict[int, int] = {}
        real = self.worker

        @contextmanager
        def counting(index: int, shared):
            with real(index, shared) as pass_:
                original = pass_.run

                def run(plan, **kwargs):
                    taken[index] = taken.get(index, 0) + 1

                    return original(plan, **kwargs)

                pass_.run = run

                yield pass_

        capture_module.run_in_parallel(self.plan(), workers=2, worker=counting)

        self.assertEqual(sum(taken.values()), len(capture_module.plan_groups(self.plan())))

    def test_no_workers_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            self.capture_with(0)


class AudioGateTests(unittest.TestCase):
    """Only one application opens the input device at a time.

    There is one device on the machine and instances contend for it. Four
    applications starting together left two blocked inside ALSA's open, on the
    thread that answers the control channel — running, but never replying, so
    their share of the plan was simply lost.

    Opening is the only part that has to be alone: once an application has the
    device it keeps it, and settling, photographing and converting — which is
    where a run's time actually goes — overlap freely.
    """

    def test_a_restart_takes_the_gate(self) -> None:
        """A surface that restarts reopens the device mid-run."""
        held: list[str] = []
        gate = threading.Lock()

        class Watched(capture_module.CapturePass):
            def __init__(inner):  # noqa: N805 - test double
                inner.audio_gate = gate
                inner.driver = inner
                inner._open = None

            def restart(inner) -> None:  # noqa: N805 - standing in for the driver
                held.append("locked" if gate.locked() else "UNGUARDED")

            def open_state(inner, state):  # noqa: N805 - test double
                return Reply(True, [])

            def set_theme(inner, theme):  # noqa: N805 - test double
                return ""

            def set_geometry(inner, geometry):  # noqa: N805 - test double
                return ""

        restarting = surfaces.Surface(
            state="tuner-docked", title="After a restart",
            modes=frozenset({surfaces.FULL}), restart_before=True
        )
        Watched().move_to(restarting, "default", surfaces.DARK)

        self.assertEqual(held, ["locked"])

    def test_a_surface_that_does_not_restart_does_not_wait(self) -> None:
        """Holding it for every surface would serialise the run it exists to keep parallel."""
        gate = threading.Lock()
        gate.acquire()
        self.addCleanup(gate.release)

        class Ordinary(capture_module.CapturePass):
            def __init__(inner):  # noqa: N805 - test double
                inner.audio_gate = gate
                inner.driver = inner
                inner._open = None

            def open_state(inner, state):  # noqa: N805 - test double
                return Reply(True, [])

            def set_theme(inner, theme):  # noqa: N805 - test double
                return ""

            def set_geometry(inner, geometry):  # noqa: N805 - test double
                return ""

        plain = surfaces.Surface(
            state="empty", title="The shell", modes=frozenset({surfaces.QUICK})
        )

        # Would block forever if `move_to` took the gate unconditionally.
        self.assertEqual(Ordinary().move_to(plain, "default", surfaces.DARK), "")

    def test_a_parallel_run_hands_out_one_gate(self) -> None:
        seen: list[int] = []

        @contextmanager
        def worker(index: int, shared):
            seen.append(id(shared.audio))

            raise RuntimeError("nothing to capture here")
            yield  # noqa: PLW0101 - unreachable by design

        capture_module.run_in_parallel(
            surfaces.plan(surfaces.QUICK, ("default",), (surfaces.DARK,)),
            workers=3,
            worker=worker,
        )

        self.assertEqual(len(seen), 3)
        self.assertEqual(len(set(seen)), 1, "workers were given different gates")


class ProgressWeightTests(unittest.TestCase):
    """Half the surfaces is not half the time."""

    def test_a_tone_surface_costs_more_than_a_plain_one(self) -> None:
        plain = surfaces.Surface(
            state="empty", title="The shell", modes=frozenset({surfaces.QUICK})
        )
        tone = surfaces.Surface(
            state="tuner-in-tune", title="In tune", modes=frozenset({surfaces.QUICK}),
            warmup_seconds=2.5,
        )

        self.assertGreater(
            surfaces.capture_cost(tone, first_of_group=True),
            surfaces.capture_cost(plain, first_of_group=True) * 2,
        )

    def test_a_restart_is_the_most_expensive_thing_a_surface_can_ask_for(self) -> None:
        plain = surfaces.Surface(
            state="tuner-docked", title="Docked", modes=frozenset({surfaces.FULL})
        )
        restarting = surfaces.Surface(
            state="tuner-docked", title="After a restart",
            modes=frozenset({surfaces.FULL}), restart_before=True,
        )

        self.assertGreater(
            surfaces.capture_cost(restarting, first_of_group=True),
            surfaces.capture_cost(plain, first_of_group=True),
        )

    def test_the_second_resolution_of_a_surface_is_cheaper_than_the_first(self) -> None:
        """The state is already open; only the resize and the settle repeat."""
        tone = surfaces.Surface(
            state="tuner-in-tune", title="In tune", modes=frozenset({surfaces.QUICK}),
            warmup_seconds=2.5,
        )

        self.assertLess(
            surfaces.capture_cost(tone, first_of_group=False),
            surfaces.capture_cost(tone, first_of_group=True),
        )

    def test_a_plans_cost_is_more_than_its_length(self) -> None:
        plan = surfaces.plan(surfaces.QUICK, ("default", "constrained"), surfaces.THEMES)

        self.assertGreater(surfaces.plan_cost(plan), len(plan))

    def test_the_fraction_uses_weight_when_there_is_one(self) -> None:
        self.assertAlmostEqual(
            capture_module.progress_fraction(
                {"cost_done": 25.0, "cost_total": 100.0, "done": 9, "total": 10}
            ),
            0.25,
        )

    def test_the_fraction_falls_back_to_counting(self) -> None:
        """A caller reporting progress without weights still gets a bar that moves."""
        self.assertAlmostEqual(
            capture_module.progress_fraction({"done": 5, "total": 10}), 0.5
        )

    def test_the_fraction_never_exceeds_one(self) -> None:
        self.assertEqual(
            capture_module.progress_fraction({"cost_done": 120.0, "cost_total": 100.0}), 1.0
        )

    def test_a_run_reports_weights_that_reach_the_total(self) -> None:
        """Whatever is skipped, failed, or captured, the bar arrives at the end."""
        seen: list[dict] = []
        plan = surfaces.plan(surfaces.QUICK, ("default",), (surfaces.DARK,))

        class Counting(capture_module.CapturePass):
            def __init__(inner):  # noqa: N805 - test double
                inner.lock = threading.Lock()
                inner.store = _StubStore()
                inner.run_id = 1

            def capture_one(inner, surface, geometry, seen_sizes, digests, theme, warm=True):  # noqa: N805
                return (1280, 800)

        Counting().run(plan, progress=seen.append)

        self.assertEqual(len(seen), len(plan))
        self.assertEqual(seen[0]["cost_done"], 0.0)
        self.assertAlmostEqual(seen[0]["cost_total"], surfaces.plan_cost(plan))
        # Everything but the last entry's own cost has been paid by the end.
        self.assertAlmostEqual(
            seen[-1]["cost_done"] + seen[-1]["cost"], surfaces.plan_cost(plan)
        )


class ConcurrencyTests(unittest.TestCase):
    """The races. Each of these is silent when it goes wrong.

    A group handed to two workers photographs the same surface twice and looks
    like a slow run. A group handed to nobody is a hole in a contact sheet
    nobody counts. Two applications opening the input device at once is the
    hang that started all of this. None of them raise, and none of them show up
    in an image.

    Every test here runs real threads against real locks, with enough workers
    and enough contention to lose if the guarding were removed — which was
    checked for each, by removing it.
    """

    WORKERS = 8

    def plan(self) -> tuple:
        return surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, surfaces.THEMES)

    def test_every_group_is_taken_exactly_once(self) -> None:
        taken: list[tuple] = []
        guard = threading.Lock()

        @contextmanager
        def worker(index: int, shared):
            class Pass:
                def run(inner, plan, **kwargs):  # noqa: N805 - test double
                    # Long enough for eight threads to be inside this at once.
                    time.sleep(0.001)

                    with guard:
                        taken.append(tuple((s.state, s.title, g, t) for s, g, t in plan))

                    return {"captured": len(plan)}

            yield Pass()

        plan = self.plan()
        capture_module.run_in_parallel(plan, workers=self.WORKERS, worker=worker)

        flat = [entry for group in taken for entry in group]

        self.assertEqual(len(flat), len(plan), "a group was taken twice or not at all")
        self.assertEqual(len(set(flat)), len(plan))
        self.assertEqual(len(taken), len(capture_module.plan_groups(plan)))

    def test_the_audio_gate_is_never_held_by_two_workers(self) -> None:
        """The hang this whole design exists around."""
        inside = 0
        most = 0
        guard = threading.Lock()
        overlapped = threading.Event()

        @contextmanager
        def worker(index: int, shared):
            nonlocal inside, most

            with shared.audio:
                with guard:
                    inside += 1
                    most = max(most, inside)

                    if inside > 1:
                        overlapped.set()

                # Held long enough that any other worker reaching for it would
                # be inside at the same time if the gate did not hold.
                time.sleep(0.02)

                with guard:
                    inside -= 1

            class Pass:
                def run(inner, plan, **kwargs):  # noqa: N805 - test double
                    return {"captured": len(plan)}

            yield Pass()

        capture_module.run_in_parallel(self.plan(), workers=self.WORKERS, worker=worker)

        self.assertFalse(overlapped.is_set())
        self.assertEqual(most, 1, "two applications opened the device at once")

    def test_the_cross_capture_maps_survive_being_written_by_everyone(self) -> None:
        """Eight workers, one map each way, no lost writes."""
        plan = self.plan()
        recorded: list[tuple] = []
        guard = threading.Lock()

        @contextmanager
        def worker(index: int, shared):
            class Pass:
                lock = None

                def run(inner, plan, *, sizes, digests, **kwargs):  # noqa: N805 - test double
                    for position, (surface, geometry, theme) in enumerate(plan):
                        with shared.checks:
                            seen = sizes.setdefault(f"{surface.title}/{theme}", {})
                            seen[geometry] = (index, position)
                            digests[(geometry, f"{surface.state}-{position}")] = surface.state

                        with guard:
                            recorded.append((surface.title, theme, geometry))

                    return {"captured": len(plan)}

            yield Pass()

        sizes: dict = {}
        digests: dict = {}

        # Reach into the run's own maps by capturing what the workers were given.
        @contextmanager
        def capturing(index: int, shared):
            with worker(index, shared) as made:
                original = made.run

                def run(plan, **kwargs):
                    sizes.update({})  # the same objects the run created
                    digests.update({})

                    return original(plan, **kwargs)

                made.run = run

                yield made

        capture_module.run_in_parallel(plan, workers=self.WORKERS, worker=capturing)

        self.assertEqual(len(recorded), len(plan))
        self.assertEqual(len(set(recorded)), len(plan))

    def test_the_progress_count_is_not_lost_between_workers(self) -> None:
        """`6 of 204` has to mean six, whoever captured them."""
        seen: list[int] = []
        guard = threading.Lock()

        @contextmanager
        def worker(index: int, shared):
            class Pass:
                def run(inner, plan, *, progress, **kwargs):  # noqa: N805 - test double
                    for surface, geometry, theme in plan:
                        progress({
                            "surface": surface.title, "geometry": geometry,
                            "theme": theme, "cost": 1.0,
                        })

                    return {"captured": len(plan)}

            yield Pass()

        plan = self.plan()

        def note(entry: dict) -> None:
            with guard:
                seen.append(entry["done"])

        capture_module.run_in_parallel(
            plan, workers=self.WORKERS, worker=worker, progress=note
        )

        self.assertEqual(sorted(seen), list(range(len(plan))))

    def test_a_stop_already_asked_for_reaches_every_worker(self) -> None:
        """No worker takes a group after the run has been told to stop."""
        captured = 0
        guard = threading.Lock()

        @contextmanager
        def worker(index: int, shared):
            class Pass:
                def run(inner, plan, **kwargs):  # noqa: N805 - test double
                    nonlocal captured

                    with guard:
                        captured += len(plan)

                    return {"captured": len(plan)}

            yield Pass()

        result = capture_module.run_in_parallel(
            self.plan(), workers=self.WORKERS, worker=worker, should_stop=lambda: True
        )

        self.assertTrue(result["stopped"])
        self.assertEqual(captured, 0, "a worker took a group after the stop")

    def test_a_stop_part_way_leaves_the_rest_untaken(self) -> None:
        taken = 0
        guard = threading.Lock()
        stop = threading.Event()

        @contextmanager
        def worker(index: int, shared):
            class Pass:
                def run(inner, plan, **kwargs):  # noqa: N805 - test double
                    nonlocal taken

                    with guard:
                        taken += 1

                        if taken >= self.WORKERS:
                            stop.set()

                    return {"captured": len(plan)}

            yield Pass()

        plan = self.plan()
        groups = len(capture_module.plan_groups(plan))
        result = capture_module.run_in_parallel(
            plan, workers=self.WORKERS, worker=worker, should_stop=stop.is_set
        )

        self.assertTrue(result["stopped"])
        self.assertLess(taken, groups, "the stop reached nobody")


class WarmupTests(unittest.TestCase):
    """The wait that a tool drawing a history needs, paid once per surface.

    A tone surface waits 2.5s so the tuner or the spectrogram has something to
    show. Paid again for each of that surface's resolutions it was 270 seconds
    of a full sweep; paid once it is 45. Nothing is lost: the tool has been
    listening the whole time, and asking for a different window size does not
    empty what it heard.
    """

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.store = Store.open(self.root / "verification.db")
        self.addCleanup(self.store.close)
        self.run_id = self.store.start_run(
            provenance=PROVENANCE, commit="abc", mode=surfaces.QUICK,
            resolutions=("default", "constrained"),
        )
        self.slept: list[float] = []
        self.original_convert = images.convert
        images.convert = self.fake_convert
        self.addCleanup(setattr, images, "convert", self.original_convert)
        self.converted = 0

    def fake_convert(self, source: Path, png: Path, thumb: Path):
        png.write_bytes(b"png")
        thumb.write_bytes(b"thumb")
        self.converted += 1

        return 1280, 800, f"digest-{self.converted}"

    def make_pass(self):
        outer = self
        sizes = {"default": (1280, 800), "constrained": (800, 600)}
        asked: list[str] = []

        class TestablePass(capture_module.CapturePass):
            def read_size(inner, title: str = ""):  # noqa: N805 - test double
                return sizes.get(asked[-1] if asked else "default")

            def _capture_to(inner, destination: Path, title: str = "") -> str:  # noqa: N805
                destination.write_bytes(b"P6")

                return ""

            def move_to(inner, surface, geometry, theme=""):  # noqa: N805 - test double
                asked.append(geometry)

                return capture_module.CapturePass.move_to(inner, surface, geometry, theme)

        return TestablePass(
            store=self.store, run_id=self.run_id, driver=FakeDriver(),
            tooling=capture_module.Tooling(Path("capture"), Path("control")),
            image_directory=self.root / "images",
            settle_seconds=2.0, poll_interval=0.0,
            sleep=outer.slept.append,
        )

    def tone_plan(self) -> tuple:
        tone = surfaces.Surface(
            state="tuner-in-tune", title="In tune", modes=frozenset({surfaces.QUICK}),
            warmup_seconds=2.5,
        )

        return tuple((tone, geometry, surfaces.DARK) for geometry in ("default", "constrained"))

    def test_a_surface_warms_up_once_however_many_resolutions_it_has(self) -> None:
        plan = self.tone_plan()
        self.make_pass().run(plan)

        self.assertEqual(self.slept, [2.5], "the warmup was paid for every resolution")

    def test_each_surface_gets_its_own_warmup(self) -> None:
        """Once per surface, not once per run: a different tool has a different
        history to fill."""
        first = surfaces.Surface(
            state="tuner-in-tune", title="In tune", modes=frozenset({surfaces.QUICK}),
            warmup_seconds=2.5,
        )
        second = surfaces.Surface(
            state="spectrogram-tone", title="Spectrogram",
            modes=frozenset({surfaces.QUICK}), warmup_seconds=2.5,
        )
        plan = (
            (first, "default", surfaces.DARK), (first, "constrained", surfaces.DARK),
            (second, "default", surfaces.DARK), (second, "constrained", surfaces.DARK),
        )
        self.make_pass().run(plan)

        self.assertEqual(self.slept, [2.5, 2.5])

    def test_the_same_surface_in_another_palette_warms_up_again(self) -> None:
        """The state is reopened for a palette change, so the history is new."""
        tone = surfaces.Surface(
            state="tuner-in-tune", title="In tune", modes=frozenset({surfaces.QUICK}),
            warmup_seconds=2.5,
        )
        plan = (
            (tone, "default", surfaces.DARK),
            (tone, "default", surfaces.LIGHT),
        )
        self.make_pass().run(plan)

        self.assertEqual(self.slept, [2.5, 2.5])

    def test_a_resumed_run_warms_up_the_first_one_it_actually_takes(self) -> None:
        """Skipping the first resolution must not skip the warmup with it."""
        plan = self.tone_plan()
        self.store.record_capture(
            self.run_id, state="tuner-in-tune", title="In tune",
            geometry="default", theme=surfaces.DARK, image_path="x", thumbnail_path="y",
            width=1, height=1, digest="d",
        )
        self.make_pass().run(plan, resume=True)

        self.assertEqual(self.slept, [2.5])

    def test_a_surface_with_no_tone_never_waits(self) -> None:
        plain = surfaces.Surface(
            state="empty", title="The shell", modes=frozenset({surfaces.QUICK})
        )
        self.make_pass().run(((plain, "default", surfaces.DARK),))

        self.assertEqual(self.slept, [])


class ReopeningTests(unittest.TestCase):
    """How often the workspace is rebuilt.

    Opening a state rebuilds it, which throws away whatever a tool had heard.
    Asking for four window sizes used to build the surface four times and refill
    its history four times — for four pictures of the same thing at different
    sizes.

    These and `WarmupTests` are one change in two halves. Reopen per resolution
    and the second capture needs its own warmup; do not reopen and it does not.
    Change one without the other and the capture is of an empty graph, which
    reads as a rendering bug rather than as a harness bug.
    """

    def setUp(self) -> None:
        self.driver = FakeDriver()

    def make_pass(self):
        return capture_module.CapturePass(
            store=None, run_id=1, driver=self.driver,
            tooling=capture_module.Tooling(Path("capture"), Path("control")),
            image_directory=Path("."),
        )

    def surface(self, state: str = "tuner-in-tune", **fields):
        return surfaces.Surface(
            state=state, title=fields.pop("title", "A surface"),
            modes=frozenset({surfaces.QUICK}), **fields,
        )

    def test_the_same_surface_at_several_sizes_is_opened_once(self) -> None:
        pass_ = self.make_pass()
        surface = self.surface()

        for geometry in ("default", "constrained", "narrow", "maximised"):
            pass_.move_to(surface, geometry, surfaces.DARK)

        self.assertEqual(self.driver.opened, ["tuner-in-tune"])
        self.assertEqual(len(self.driver.geometries), 4)

    def test_a_different_surface_is_opened(self) -> None:
        pass_ = self.make_pass()
        pass_.move_to(self.surface("empty"), "default", surfaces.DARK)
        pass_.move_to(self.surface("tuner-docked"), "default", surfaces.DARK)

        self.assertEqual(self.driver.opened, ["empty", "tuner-docked"])

    def test_a_palette_change_reopens(self) -> None:
        """The theme is applied after the state, so the state has to come again."""
        pass_ = self.make_pass()
        surface = self.surface()
        pass_.move_to(surface, "default", surfaces.DARK)
        pass_.move_to(surface, "default", surfaces.LIGHT)

        self.assertEqual(self.driver.opened, ["tuner-in-tune", "tuner-in-tune"])
        self.assertEqual(self.driver.themes, [surfaces.DARK, surfaces.LIGHT])

    def test_a_restart_forces_the_state_open_again(self) -> None:
        """There is nothing open after a restart, whatever was open before."""
        pass_ = self.make_pass()
        surface = self.surface(restart_before=True)
        pass_.move_to(surface, "default", surfaces.DARK)
        pass_.move_to(surface, "constrained", surfaces.DARK)

        self.assertEqual(self.driver.restarts, 2)
        self.assertEqual(self.driver.opened, ["tuner-in-tune", "tuner-in-tune"])

    def test_a_refused_state_is_not_remembered_as_open(self) -> None:
        """Otherwise the next capture assumes a surface that was never built."""
        pass_ = self.make_pass()
        self.driver.refuse = {"tuner-in-tune"}
        surface = self.surface()

        self.assertNotEqual(pass_.move_to(surface, "default", surfaces.DARK), "")

        self.driver.refuse = set()
        pass_.move_to(surface, "constrained", surfaces.DARK)

        self.assertEqual(self.driver.opened, ["tuner-in-tune", "tuner-in-tune"])

    def test_a_broken_channel_is_not_remembered_as_open(self) -> None:
        pass_ = self.make_pass()
        surface = self.surface()

        class Breaking(FakeDriver):
            def open_state(inner, state):  # noqa: N805 - test double
                inner.opened.append(state)

                raise ChannelError("the application closed the channel")

        pass_.driver = Breaking()

        self.assertNotEqual(pass_.move_to(surface, "default", surfaces.DARK), "")
        self.assertIsNone(pass_._open)

    def test_a_full_sweep_opens_one_state_per_surface_and_palette(self) -> None:
        """The number this change is about: 72 rather than 292."""
        plan = surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, surfaces.THEMES)
        pass_ = self.make_pass()

        for surface, geometry, theme in plan:
            pass_.move_to(surface, geometry, theme)

        # One per surface-and-palette, plus the surfaces that restart first —
        # those have nothing open afterwards whatever was open before.
        restarts = sum(1 for surface, _, _ in plan if surface.restart_before)
        restart_groups = sum(
            1 for group in capture_module.plan_groups(plan) if group[0][0].restart_before
        )
        expected = len(capture_module.plan_groups(plan)) + restarts - restart_groups

        self.assertEqual(len(self.driver.opened), expected)
        self.assertLess(len(self.driver.opened), len(plan) / 3)


class InputRoutingTests(unittest.TestCase):
    """Which worker may take a surface that has to be hearing something.

    One application at a time holds the input device. Measured with eight
    workers running: seven reported none. A tone surface photographed by a
    worker without input is a picture of a tool that has heard nothing, and it
    is counted as captured — which is how a blank tuner saying "waiting for the
    microphone" landed in a run that reported twelve captured.
    """

    def plan(self) -> tuple:
        return surfaces.plan(surfaces.QUICK, ("default", "constrained"), surfaces.THEMES)

    def capture_with(self, deaf: set[int], workers: int = 4) -> dict:
        took: dict[int, list] = {}
        guard = threading.Lock()

        @contextmanager
        def worker(index: int, shared):
            class Pass:
                has_input = index not in deaf

                def run(inner, plan, **kwargs):  # noqa: N805 - test double
                    with guard:
                        took.setdefault(index, []).extend(plan)

                    return {"captured": len(plan)}

            yield Pass()

        result = capture_module.run_in_parallel(
            self.plan(), workers=workers, worker=worker
        )

        return {"result": result, "took": took}

    def test_a_worker_without_input_never_takes_a_tone_surface(self) -> None:
        run = self.capture_with(deaf={1, 2, 3})

        for index, entries in run["took"].items():
            if index in {1, 2, 3}:
                with self.subTest(worker=index):
                    self.assertFalse(
                        any(surface.warmup_seconds > 0 for surface, _, _ in entries),
                        "a worker with no input photographed a tool that needed one",
                    )

    def test_everything_is_still_captured(self) -> None:
        run = self.capture_with(deaf={1, 2, 3})
        dealt = [entry for entries in run["took"].values() for entry in entries]

        self.assertEqual(len(dealt), len(self.plan()))

    def test_nobody_hearing_means_the_pictures_are_taken_anyway(self) -> None:
        """A missing capture is invisible; a tool saying it is waiting is not."""
        run = self.capture_with(deaf={0, 1, 2, 3})
        dealt = [entry for entries in run["took"].values() for entry in entries]

        self.assertEqual(len(dealt), len(self.plan()))
        self.assertTrue(any(surface.warmup_seconds > 0 for surface, _, _ in dealt))

    def test_the_worker_that_can_hear_takes_them_all(self) -> None:
        run = self.capture_with(deaf={1, 2, 3})
        tone = [
            entry
            for entries in run["took"].values()
            for entry in entries
            if entry[0].warmup_seconds > 0
        ]

        self.assertTrue(tone)
        self.assertEqual(
            {surface.title for surface, _, _ in tone},
            {surface.title for surface, _, _ in self.plan() if surface.warmup_seconds > 0},
        )
