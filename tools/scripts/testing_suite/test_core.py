#!/usr/bin/env python3
"""Tests for the suite's core: the surface list, the protocol, and the record.

None of it needs a display, an application, or Pillow, so it runs in the
ordinary Python suite. The parts that do need those — capture, the attended
pass, the review server — are covered by the other test files here against
fakes and a temporary store.
"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import record  # noqa: E402
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

    def test_a_run_covers_every_configured_resolution(self) -> None:
        plain = surfaces.plan(surfaces.FULL, (surfaces.DEFAULT_GEOMETRY,), (surfaces.DARK,))
        swept = surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, (surfaces.DARK,))

        # Two kinds of surface are captured once whatever the set says: one that
        # is about its own size, and one whose subject is a window the main
        # window's geometry does not govern.
        once = sum(
            1
            for surface in surfaces.surfaces_for_mode(surfaces.FULL)
            if surface.fixed_geometry or surface.window_title
        )

        self.assertTrue(once)
        self.assertEqual(
            len(swept), (len(plain) - once) * len(surfaces.SWEEP_GEOMETRIES) + once
        )

    def test_a_run_must_cover_something(self) -> None:
        with self.assertRaises(ValueError):
            surfaces.plan(surfaces.FULL, ())

        with self.assertRaises(ValueError):
            surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, ())

    def test_a_run_covers_every_configured_palette(self) -> None:
        """Every surface exists in both palettes; the theme is a dimension, not a state."""
        one = surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, (surfaces.DARK,))
        both = surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, surfaces.THEMES)

        self.assertEqual(len(both), len(one) * 2)
        self.assertEqual({theme for _, _, theme in both}, set(surfaces.THEMES))

    def test_an_unknown_palette_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            surfaces.plan(surfaces.FULL, surfaces.SWEEP_GEOMETRIES, ("neon",))

    def test_a_run_can_be_narrowed_to_named_surfaces(self) -> None:
        """Someone who changed one tool wants that tool, not a full sweep."""
        chosen = surfaces.surfaces_for_mode(surfaces.FULL)[0].state
        narrowed = surfaces.plan(
            surfaces.FULL, (surfaces.DEFAULT_GEOMETRY,), (surfaces.DARK,), (chosen,)
        )

        self.assertTrue(narrowed)
        self.assertEqual({surface.state for surface, _, _ in narrowed}, {chosen})

    def test_narrowing_still_covers_every_resolution_and_palette(self) -> None:
        """The selection replaces which surfaces, not the other two dimensions."""
        chosen = next(
            surface.state
            for surface in surfaces.surfaces_for_mode(surfaces.FULL)
            if not surface.fixed_geometry and not surface.window_title
        )
        narrowed = surfaces.plan(
            surfaces.FULL, surfaces.SWEEP_GEOMETRIES, surfaces.THEMES, (chosen,)
        )

        self.assertEqual({theme for _, _, theme in narrowed}, set(surfaces.THEMES))
        self.assertEqual(
            {geometry for _, geometry, _ in narrowed}, set(surfaces.SWEEP_GEOMETRIES)
        )

    def test_an_unknown_surface_name_is_rejected_rather_than_ignored(self) -> None:
        """A typo that captured nothing would look exactly like a clean run.

        Warning and carrying on with whatever matched produces a run that
        covered less than the operator believes, and nothing in the output
        distinguishes that from every surface being fine.
        """
        with self.assertRaises(ValueError) as raised:
            surfaces.plan(
                surfaces.FULL, (surfaces.DEFAULT_GEOMETRY,), (surfaces.DARK,), ("tuner-in-tune-typo",)
            )

        self.assertIn("tuner-in-tune-typo", str(raised.exception))

    def test_no_selection_covers_everything(self) -> None:
        everything = surfaces.plan(surfaces.FULL, (surfaces.DEFAULT_GEOMETRY,), (surfaces.DARK,))
        explicit = surfaces.plan(
            surfaces.FULL, (surfaces.DEFAULT_GEOMETRY,), (surfaces.DARK,), ()
        )

        self.assertEqual(everything, explicit)

    def test_a_surface_outside_the_mode_is_rejected(self) -> None:
        """Quick mode is a subset, so a full-only name is not silently empty."""
        full_only = {surface.state for surface in surfaces.surfaces_for_mode(surfaces.FULL)} - {
            surface.state for surface in surfaces.surfaces_for_mode(surfaces.QUICK)
        }

        if not full_only:
            self.skipTest("quick mode currently covers every surface")

        with self.assertRaises(ValueError):
            surfaces.plan(
                surfaces.QUICK, (surfaces.DEFAULT_GEOMETRY,), (surfaces.DARK,), (sorted(full_only)[0],)
            )

    def test_a_palette_is_captured_in_one_pass_before_the_next(self) -> None:
        """Switching palette is instant; a resize has to settle, so themes go outside."""
        themes = [theme for _, _, theme in surfaces.plan(surfaces.QUICK)]

        self.assertEqual(themes, sorted(themes, key=lambda name: themes.index(name)))
        self.assertEqual(len(set(themes)), len(surfaces.THEMES))

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
                        surfaces.resolutions_for_surface(surface, surfaces.SWEEP_GEOMETRIES),
                        (surfaces.DEFAULT_GEOMETRY,),
                    )

    def test_the_geometry_surfaces_are_the_exempt_ones(self) -> None:
        exempt = {s.state for s in surfaces.SURFACES if s.fixed_geometry}

        self.assertEqual(exempt, {"narrow-window", "narrow-empty", "fullscreen"})

    def test_the_plan_records_which_geometry_each_entry_is_for(self) -> None:
        for _, geometry, theme in surfaces.plan(surfaces.QUICK, surfaces.SWEEP_GEOMETRIES):
            self.assertIn(geometry, surfaces.SWEEP_GEOMETRIES)
            self.assertIn(theme, surfaces.THEMES)

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

    def test_a_failure_without_a_note_is_accepted(self) -> None:
        """Encouraged, not required: the export is never blocked over wording."""
        self.assertEqual(self.answer(record.FAIL).problems(), [])

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


class AttendedSplitTests(unittest.TestCase):
    """Which pass asks what.

    The split is the load-bearing part of moving to screenshots: a question
    about behaviour answered from a still image is not answered at all.
    """

    def test_the_three_axes_are_always_asked_from_the_image(self) -> None:
        for surface in surfaces.SURFACES:
            with self.subTest(surface=surface.title):
                asked = [q.id for q in surfaces.review_questions(surface)]

                self.assertEqual(asked[:3], [q.id for q in surfaces.FIXED_AXES])

    def test_a_behavioural_question_is_never_asked_of_an_image(self) -> None:
        for surface in surfaces.SURFACES:
            reviewable = {q.id for q in surfaces.review_questions(surface)}

            for question in surface.extras:
                if question.behavioural:
                    with self.subTest(surface=surface.title, question=question.id):
                        self.assertNotIn(question.id, reviewable)

    def test_live_input_questions_are_attended(self) -> None:
        """The clearest case: a screenshot cannot show a tuner reacting to sound."""
        for surface in surfaces.SURFACES:
            for question in surface.extras:
                if question.id in ("live-input", "all-live", "mute-silences"):
                    with self.subTest(surface=surface.title):
                        self.assertIn(question, surfaces.attended_questions(surface))

    def test_a_surface_with_an_instruction_is_entirely_attended(self) -> None:
        """Its extras only mean anything once somebody has done the thing."""
        for surface in surfaces.SURFACES:
            if surface.instruction:
                with self.subTest(surface=surface.title):
                    self.assertEqual(surfaces.attended_questions(surface), surface.extras)

    def test_every_question_is_asked_by_exactly_one_pass(self) -> None:
        for surface in surfaces.SURFACES:
            with self.subTest(surface=surface.title):
                reviewable = [q.id for q in surfaces.review_questions(surface)]
                attended = [q.id for q in surfaces.attended_questions(surface)]
                everything = [q.id for q in surfaces.questions_for(surface)]

                self.assertEqual(sorted(reviewable + attended), sorted(everything))
                self.assertFalse(set(reviewable) & set(attended))

    def test_the_attended_pass_is_much_smaller_than_the_run(self) -> None:
        """If it is not, the workflow has not actually moved off the serial harness."""
        attending = surfaces.surfaces_needing_attendance(surfaces.FULL)
        everything = surfaces.surfaces_for_mode(surfaces.FULL)

        self.assertTrue(attending)
        self.assertLess(len(attending), len(everything))

    def test_a_capture_is_matched_back_to_its_surface(self) -> None:
        """One state serves several surfaces, so title has to be part of the match."""
        surface = surfaces.SURFACES[1]

        self.assertIs(surfaces.find(surface.state, surface.title), surface)
        self.assertIsNone(surfaces.find(surface.state, "a title nobody uses"))


class RecordAdditionsTests(unittest.TestCase):
    """The fields the store's exporter adds, and the ones it must not disturb."""

    def make_run(self) -> record.Run:
        return record.Run(
            commit="abc123",
            platform="Linux",
            audio_device="Built-in",
            mode=surfaces.FULL,
            geometry_sweep=True,
            started_at="2026-08-03T10:00:00",
            resolutions=["default", "constrained"],
        )

    def test_the_resolutions_covered_are_named(self) -> None:
        """"Sweep: yes" cannot distinguish two runs that covered different sets."""
        markdown = record.to_markdown(self.make_run())

        self.assertIn("default, constrained", markdown)

    def test_tags_and_comments_appear_as_detail_not_verdicts(self) -> None:
        run = self.make_run()
        run.image_notes = [
            {"surface": "The tuner, docked", "geometry": "constrained",
             "tags": ["illegible"], "comments": ["the cents readout is unreadable"]}
        ]

        markdown = record.to_markdown(run)

        self.assertIn("illegible", markdown)
        self.assertIn("cents readout", markdown)
        # The comparable core is still the answers table, not the tags.
        self.assertIn("## All answers", markdown)

    def test_unanswered_questions_are_named(self) -> None:
        run = self.make_run()
        run.unanswered = [
            {"surface": "The tuner, docked", "geometry": "default",
             "question": "live-input", "prompt": "Does it respond to sound?"}
        ]

        self.assertIn("Does it respond to sound?", record.to_markdown(run))

    def test_ingested_evidence_is_rendered(self) -> None:
        run = self.make_run()
        run.measurements = [{"metric": "launch", "value": 412.0, "unit": "ms", "scenario": "cold"}]
        run.test_results = [{"suite": "PracticeTakesTests", "cases": 412, "failures": 0,
                             "duration_seconds": 3.2}]

        markdown = record.to_markdown(run)

        self.assertIn("launch", markdown)
        self.assertIn("PracticeTakesTests", markdown)


if __name__ == "__main__":
    unittest.main()
