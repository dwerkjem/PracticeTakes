#!/usr/bin/env python3
"""Tests for the parts of the manual GUI harness that need no display.

Deliberately importable without Textual: the surface list, the record, and the
protocol parsing are all standard-library only, so this runs in the ordinary
Python suite rather than needing the harness's optional dependency.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import record  # noqa: E402
import session  # noqa: E402
import surfaces  # noqa: E402
from driver import missing_states, parse_reply  # noqa: E402


class SurfaceListTests(unittest.TestCase):
    def test_the_surface_list_is_coherent(self) -> None:
        """Run before a session starts, so a malformed list fails immediately."""
        self.assertEqual(surfaces.validate(), [])

    def test_quick_mode_is_a_strict_subset_of_full(self) -> None:
        """Otherwise 'quick' stops meaning 'less than full'."""
        quick = {s.title for s in surfaces.surfaces_for_mode(surfaces.QUICK)}
        full = {s.title for s in surfaces.surfaces_for_mode(surfaces.FULL)}

        self.assertTrue(quick)
        self.assertTrue(quick < full)

    def test_quick_mode_stays_small(self) -> None:
        """A quick mode that grows becomes a second full mode."""
        self.assertLessEqual(len(surfaces.surfaces_for_mode(surfaces.QUICK)), 5)

    def test_every_surface_asks_the_three_fixed_axes(self) -> None:
        """Fixed axes are what make runs comparable across versions."""
        for surface in surfaces.SURFACES:
            with self.subTest(surface=surface.title):
                ids = [q.id for q in surfaces.questions_for(surface)][:3]
                self.assertEqual(ids, [q.id for q in surfaces.FIXED_AXES])

    def test_extras_come_after_the_fixed_axes(self) -> None:
        surface = next(s for s in surfaces.SURFACES if s.extras)
        questions = surfaces.questions_for(surface)

        self.assertEqual(len(questions), len(surfaces.FIXED_AXES) + len(surface.extras))

    def test_an_unknown_mode_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            surfaces.surfaces_for_mode("thorough")

    def test_the_geometry_sweep_is_off_by_default(self) -> None:
        """It multiplies how many prompts a run asks."""
        self.assertEqual(surfaces.geometries_for(False), (surfaces.DEFAULT_GEOMETRY,))
        self.assertEqual(len(surfaces.geometries_for(True)), 3)

    def test_the_sweep_multiplies_all_but_the_exempt_surfaces(self) -> None:
        plain = surfaces.plan(surfaces.FULL, sweep=False)
        swept = surfaces.plan(surfaces.FULL, sweep=True)

        exempt = sum(1 for s in surfaces.surfaces_for_mode(surfaces.FULL) if s.fixed_geometry)

        self.assertTrue(exempt)
        self.assertEqual(len(swept), (len(plain) - exempt) * 3 + exempt)

    def test_a_surface_about_its_own_size_is_never_swept(self) -> None:
        """Resizing it would destroy the thing under test.

        This is not hypothetical: the sweep reset narrow-window and fullscreen
        to the default size, and a real run reported them "not narrow" and
        "not fullscreen".
        """
        for surface in surfaces.SURFACES:
            if surface.fixed_geometry:
                with self.subTest(surface=surface.title):
                    self.assertEqual(
                        surfaces.geometries_for_surface(surface, sweep=True),
                        (surfaces.DEFAULT_GEOMETRY,),
                    )

    def test_the_geometry_surfaces_are_the_exempt_ones(self) -> None:
        exempt = {s.state for s in surfaces.SURFACES if s.fixed_geometry}

        self.assertEqual(exempt, {"narrow-window", "fullscreen"})

    def test_the_plan_records_which_geometry_each_entry_is_for(self) -> None:
        for _, geometry in surfaces.plan(surfaces.QUICK, sweep=True):
            self.assertIn(geometry, surfaces.SWEEP_GEOMETRIES)

    def test_the_spec_required_surfaces_are_covered(self) -> None:
        """The surfaces the change committed to verifying."""
        titles = " ".join(s.title.lower() for s in surfaces.SURFACES)
        states = surfaces.required_states()

        self.assertIn("settings-audio-device", states)
        self.assertIn("microphone-muted", states)
        self.assertIn("two-tools-tabbed", states)
        self.assertIn("tuner-floating", states)
        self.assertIn("restart", titles)
        self.assertIn("import and export", titles)

    def test_a_restart_surface_exists(self) -> None:
        """Layout surviving a relaunch cannot be checked inside one process."""
        self.assertTrue(any(s.restart_before for s in surfaces.SURFACES))


class MissingStateTests(unittest.TestCase):
    def test_nothing_missing_when_the_application_offers_everything(self) -> None:
        available = list(surfaces.required_states())

        self.assertEqual(missing_states(available, surfaces.required_states()), [])

    def test_a_drifted_harness_is_detected(self) -> None:
        """Better to fail up front than halfway through a tester's run."""
        self.assertEqual(
            missing_states(["empty"], frozenset({"empty", "tuner-docked"})),
            ["tuner-docked"],
        )


class ReplyParsingTests(unittest.TestCase):
    def test_a_success_with_payload(self) -> None:
        reply = parse_reply(["item one", "item two", "ok"])

        self.assertTrue(reply.success)
        self.assertEqual(reply.items, ["one", "two"])

    def test_a_bare_success(self) -> None:
        self.assertTrue(parse_reply(["ok"]).success)

    def test_a_failure_carries_its_reason(self) -> None:
        reply = parse_reply(["error no approved state 'x'"])

        self.assertFalse(reply.success)
        self.assertEqual(reply.error, "no approved state 'x'")

    def test_a_reply_with_no_verdict_is_a_failure(self) -> None:
        """Treating it as success would record a check that never happened."""
        reply = parse_reply(["item stray"])

        self.assertFalse(reply.success)
        self.assertTrue(reply.error)


class RecordTests(unittest.TestCase):
    def make_run(self, **overrides: object) -> record.Run:
        defaults = dict(
            commit="abc123",
            platform="Linux",
            audio_device="Built-in",
            mode=surfaces.FULL,
            geometry_sweep=False,
            started_at="2026-08-01T10:00:00",
        )
        defaults.update(overrides)

        return record.Run(**defaults)  # type: ignore[arg-type]

    def answer(self, verdict: str, note: str = "") -> record.Answer:
        return record.Answer(
            surface="The tuner, docked",
            state="tuner-docked",
            geometry="default",
            question="works",
            prompt="Does it work?",
            verdict=verdict,
            note=note,
        )

    def test_a_failure_without_a_note_is_rejected(self) -> None:
        """A failure with no detail says something is wrong without saying what."""
        problems = self.answer(record.FAIL).problems()

        self.assertEqual(len(problems), 1)

    def test_a_failure_with_a_note_is_accepted(self) -> None:
        self.assertEqual(self.answer(record.FAIL, "meter frozen").problems(), [])

    def test_a_pass_needs_no_note(self) -> None:
        self.assertEqual(self.answer(record.PASS).problems(), [])

    def test_an_unknown_verdict_is_rejected(self) -> None:
        self.assertTrue(self.answer("maybe").problems())

    def test_failures_are_collected(self) -> None:
        run = self.make_run()
        run.add(self.answer(record.PASS))
        run.add(self.answer(record.FAIL, "meter frozen"))
        run.add(self.answer(record.SKIP))

        self.assertEqual(len(run.failures()), 1)

    def test_an_incomplete_run_says_so_prominently(self) -> None:
        """A partial run must never be mistaken for a passing one."""
        run = self.make_run()
        run.add(self.answer(record.PASS))

        markdown = record.to_markdown(run)

        self.assertIn("INCOMPLETE", markdown)
        self.assertIn("must not be read as a pass", markdown)

    def test_a_complete_run_is_distinguishable(self) -> None:
        run = self.make_run()
        run.complete = True
        run.add(self.answer(record.PASS))

        self.assertNotIn("INCOMPLETE", record.to_markdown(run))

    def test_the_mode_is_recorded_so_a_quick_run_is_not_a_release_check(self) -> None:
        run = self.make_run(mode=surfaces.QUICK)
        run.complete = True

        self.assertIn("quick", record.to_markdown(run))

    def test_the_mode_is_in_the_filename(self) -> None:
        """So a directory listing distinguishes runs without opening them."""
        run = self.make_run(mode=surfaces.QUICK)
        json_path, markdown_path = record.record_paths(run, Path("/tmp"))

        self.assertIn("quick", json_path.name)
        self.assertIn("quick", markdown_path.name)

    def test_the_geometry_sweep_is_recorded(self) -> None:
        run = self.make_run(geometry_sweep=False)

        self.assertIn("default size only", record.to_markdown(run))

    def test_unreachable_surfaces_appear_as_findings(self) -> None:
        """A surface nobody could look at is a finding, not an absence."""
        run = self.make_run()
        run.unreachable.append({"surface": "Fullscreen", "reason": "could not establish state"})

        markdown = record.to_markdown(run)

        self.assertIn("Unreachable", markdown)
        self.assertIn("Fullscreen", markdown)

    def test_a_note_containing_a_pipe_does_not_break_the_table(self) -> None:
        run = self.make_run()
        run.add(self.answer(record.FAIL, "shows a | character"))

        self.assertIn("\\|", record.to_markdown(run))

    def test_both_forms_are_written(self) -> None:
        run = self.make_run()
        run.complete = True
        run.add(self.answer(record.PASS))

        with tempfile.TemporaryDirectory() as directory:
            json_path, markdown_path = record.write(run, Path(directory))

            self.assertTrue(json_path.is_file())
            self.assertTrue(markdown_path.is_file())
            self.assertIn("tuner-docked", json_path.read_text(encoding="utf-8"))

    def test_the_json_round_trips(self) -> None:
        import json

        run = self.make_run()
        run.add(self.answer(record.PASS))

        parsed = json.loads(record.to_json(run))

        self.assertEqual(parsed["commit"], "abc123")
        self.assertEqual(len(parsed["answers"]), 1)


if __name__ == "__main__":
    unittest.main()


class SessionTests(unittest.TestCase):
    def make_session(self, mode: str = surfaces.QUICK, sweep: bool = False) -> session.Session:
        run = record.Run(
            commit="abc123",
            platform="Linux",
            audio_device="Built-in",
            mode=mode,
            geometry_sweep=sweep,
            started_at="2026-08-01T10:00:00",
        )

        return session.Session(run, mode, sweep)

    def test_a_session_walks_every_question_of_every_surface(self) -> None:
        active = self.make_session()
        expected = sum(
            len(surfaces.questions_for(s)) for s in surfaces.surfaces_for_mode(surfaces.QUICK)
        )

        self.assertEqual(active.total_steps, expected)

    def test_the_sweep_multiplies_the_steps(self) -> None:
        self.assertEqual(
            self.make_session(sweep=True).total_steps,
            self.make_session(sweep=False).total_steps * 3,
        )

    def test_answering_advances(self) -> None:
        active = self.make_session()
        first = active.current()

        self.assertEqual(active.answer(record.PASS), [])
        self.assertIsNot(active.current(), first)

    def test_a_failure_without_a_note_does_not_advance(self) -> None:
        """The answer must be corrected, not stored."""
        active = self.make_session()
        before = active.current()

        problems = active.answer(record.FAIL)

        self.assertTrue(problems)
        self.assertEqual(active.current().question.id, before.question.id)
        self.assertEqual(len(active.run.answers), 0)

    def test_a_failure_with_a_note_advances(self) -> None:
        active = self.make_session()

        self.assertEqual(active.answer(record.FAIL, "meter frozen"), [])
        self.assertEqual(len(active.run.answers), 1)

    def test_the_application_moves_only_when_the_surface_changes(self) -> None:
        active = self.make_session()

        self.assertTrue(active.surface_changed())
        active.answer(record.PASS)
        self.assertFalse(active.surface_changed())

    def test_a_run_finishes_only_when_every_step_is_answered(self) -> None:
        active = self.make_session()

        active.finish()
        self.assertFalse(active.run.complete)

        while not active.finished:
            active.answer(record.PASS)

        active.finish()
        self.assertTrue(active.run.complete)

    def test_an_unreachable_surface_fails_its_questions(self) -> None:
        """A surface nobody could look at is a finding, not an absence."""
        active = self.make_session()
        surface = active.current().surface
        expected = len(surfaces.questions_for(surface))

        active.skip_surface("could not establish state")

        self.assertEqual(len(active.run.unreachable), 1)
        self.assertEqual(len(active.run.answers), expected)
        self.assertTrue(all(a.verdict == record.FAIL for a in active.run.answers))

    def test_an_unreachable_surface_does_not_abandon_the_run(self) -> None:
        active = self.make_session()
        active.skip_surface("could not establish state")

        self.assertFalse(active.finished)
        self.assertIsNotNone(active.current())

    def test_an_area_pass_answers_every_question_on_the_surface(self) -> None:
        """The common case: you look, nothing is wrong, one keystroke."""
        active = self.make_session()
        surface = active.current().surface
        expected = len(surfaces.questions_for(surface))

        self.assertEqual(active.answer_rest_of_surface(record.PASS), [])
        self.assertEqual(len(active.run.answers), expected)
        self.assertTrue(all(a.verdict == record.PASS for a in active.run.answers))

    def test_an_area_pass_stops_at_the_surface_boundary(self) -> None:
        active = self.make_session()
        first = active.current().surface

        active.answer_rest_of_surface(record.PASS)

        self.assertIsNot(active.current().surface, first)

    def test_an_area_skip_records_skips_not_passes(self) -> None:
        """An area you did not examine must not be recorded as verified."""
        active = self.make_session()

        active.answer_rest_of_surface(record.SKIP)

        self.assertTrue(all(a.verdict == record.SKIP for a in active.run.answers))

    def test_an_area_fail_without_a_note_records_nothing(self) -> None:
        """It stops at the offending question rather than half-applying."""
        active = self.make_session()

        problems = active.answer_rest_of_surface(record.FAIL)

        self.assertTrue(problems)
        self.assertEqual(len(active.run.answers), 0)

    def test_an_area_fail_with_a_note_applies_to_the_whole_surface(self) -> None:
        active = self.make_session()
        surface = active.current().surface
        expected = len(surfaces.questions_for(surface))

        self.assertEqual(active.answer_rest_of_surface(record.FAIL, "all broken"), [])
        self.assertEqual(len(active.run.failures()), expected)

    def test_answering_past_the_end_is_reported(self) -> None:
        active = self.make_session()

        while not active.finished:
            active.answer(record.PASS)

        self.assertTrue(active.answer(record.PASS))


try:
    import textual  # noqa: F401

    TEXTUAL_AVAILABLE = True
except ImportError:
    TEXTUAL_AVAILABLE = False


@unittest.skipUnless(TEXTUAL_AVAILABLE, "Textual is an optional harness dependency")
class TerminalUiTests(unittest.IsolatedAsyncioTestCase):
    """Drives the real TUI headlessly, so it is verified without a human."""

    def make_app(self, reachable: bool = True):
        from app import VerificationApp

        run = record.Run(
            commit="abc123",
            platform="Linux",
            audio_device="Built-in",
            mode=surfaces.QUICK,
            geometry_sweep=False,
            started_at="2026-08-01T10:00:00",
        )
        active = session.Session(run, surfaces.QUICK, False)

        def move(surface, geometry):
            return "" if reachable else "could not establish state"

        return VerificationApp(active, move), active

    async def type_line(self, pilot, text: str) -> None:
        from textual.widgets import Input

        pilot.app.query_one("#entry", Input).value = text
        await pilot.press("enter")
        await pilot.pause()

    async def test_enter_alone_passes(self) -> None:
        """The overwhelmingly common answer costs one keystroke."""
        app, active = self.make_app()

        async with app.run_test() as pilot:
            await self.type_line(pilot, "")

            self.assertEqual(len(active.run.answers), 1)
            self.assertEqual(active.run.answers[0].verdict, record.PASS)

    async def test_a_failure_carries_the_reason_typed_with_it(self) -> None:
        """The bug this design exists to fix: two earlier attempts made it
        impossible to supply the reason a failure requires."""
        app, active = self.make_app()

        async with app.run_test() as pilot:
            await self.type_line(pilot, "f meter frozen")

        self.assertEqual(len(active.run.answers), 1)
        self.assertEqual(active.run.answers[0].verdict, record.FAIL)
        self.assertEqual(active.run.answers[0].note, "meter frozen")

    async def test_a_failure_with_no_reason_is_refused(self) -> None:
        app, active = self.make_app()

        async with app.run_test() as pilot:
            await self.type_line(pilot, "f")

            self.assertEqual(len(active.run.answers), 0)

    async def test_an_unknown_command_is_refused_and_the_text_kept(self) -> None:
        """So a mistyped verb is corrected rather than retyped."""
        from textual.widgets import Input

        app, active = self.make_app()

        async with app.run_test() as pilot:
            await self.type_line(pilot, "wat something")

            self.assertEqual(len(active.run.answers), 0)
            self.assertEqual(pilot.app.query_one("#entry", Input).value, "wat something")

    async def test_area_pass_answers_the_whole_surface(self) -> None:
        app, active = self.make_app()
        first = active.current().surface
        expected = len(surfaces.questions_for(first))

        async with app.run_test() as pilot:
            await self.type_line(pilot, "a")

            self.assertEqual(len(active.run.answers), expected)
            self.assertIsNot(active.current().surface, first)

    async def test_area_skip_records_skips(self) -> None:
        app, active = self.make_app()

        async with app.run_test() as pilot:
            await self.type_line(pilot, "as")

        self.assertTrue(all(a.verdict == record.SKIP for a in active.run.answers))

    async def test_quitting_records_an_incomplete_run(self) -> None:
        app, active = self.make_app()

        async with app.run_test() as pilot:
            await self.type_line(pilot, "")
            await self.type_line(pilot, "q")

        self.assertFalse(active.run.complete)
        self.assertEqual(len(active.run.answers), 1)
        self.assertIn("INCOMPLETE", record.to_markdown(active.run))

    async def test_passing_everything_completes_the_run(self) -> None:
        app, active = self.make_app()

        async with app.run_test() as pilot:
            for _ in range(active.total_steps):
                await self.type_line(pilot, "")

        self.assertTrue(active.run.complete)

    async def test_the_entry_field_has_focus_from_the_start(self) -> None:
        app, _ = self.make_app()

        async with app.run_test() as pilot:
            await pilot.pause()

            self.assertEqual(app.focused.id, "entry")

    async def test_an_unreachable_surface_is_recorded_and_the_run_continues(self) -> None:
        app, active = self.make_app(reachable=False)

        async with app.run_test() as pilot:
            await pilot.pause()

        self.assertTrue(active.run.unreachable)
        self.assertTrue(all(a.verdict == record.FAIL for a in active.run.answers))

    async def test_a_small_terminal_still_shows_the_entry_field(self) -> None:
        """The one thing you must always reach cannot be pushed off-screen."""
        from textual.widgets import Input

        app, _ = self.make_app()

        async with app.run_test(size=(60, 12)) as pilot:
            await pilot.pause()

            entry = pilot.app.query_one("#entry", Input)

            self.assertTrue(entry.region.height > 0)
            self.assertTrue(entry.region.y < 12)

    async def test_a_small_terminal_sheds_the_legend(self) -> None:
        from textual.widgets import Static

        app, _ = self.make_app()

        async with app.run_test(size=(60, 12)) as pilot:
            await pilot.pause()

            self.assertTrue(pilot.app.query_one("#legend", Static).has_class("hidden"))

    async def test_a_roomy_terminal_keeps_the_legend(self) -> None:
        from textual.widgets import Static

        app, _ = self.make_app()

        async with app.run_test(size=(120, 40)) as pilot:
            await pilot.pause()

            self.assertFalse(pilot.app.query_one("#legend", Static).has_class("hidden"))

    async def test_typing_a_question_mark_shows_the_commands(self) -> None:
        from textual.widgets import Static

        app, _ = self.make_app()

        async with app.run_test() as pilot:
            await self.type_line(pilot, "?")

            shown = str(pilot.app.query_one("#problem", Static).content)

            self.assertIn("fail", shown)
