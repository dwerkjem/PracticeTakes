#!/usr/bin/env python3
"""Tests for the review's decisions, and for the exporter and ingest on top of them.

The browser is not exercised here and does not need to be: it renders what
`review.py` returns and posts back what the reviewer did. Everything that could
be wrong — which questions a capture still owes, what a tag applied to a
selection does, whether an unfinished review can pass as complete — is decided
server-side and tested here.
"""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import export as export_module  # noqa: E402
import ingest as ingest_module  # noqa: E402
import review  # noqa: E402
import surfaces  # noqa: E402
from store import Store  # noqa: E402

PROVENANCE = {
    "identity": "machine-one",
    "processor": "Test CPU",
    "cores": 8,
    "memory_bytes": 16 * 1024**3,
    "graphics": "Test GPU",
    "operating_system": "Linux",
    "display": "2560x1440",
    "attributes": {"kernel": "6.12.90"},
}

TUNER = next(s for s in surfaces.SURFACES if s.title == "The tuner, docked")
SHELL = next(s for s in surfaces.SURFACES if s.title == "The shell with no tool open")


class ReviewTestCase(unittest.TestCase):
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
            platform="Linux 6.12 (x86_64)",
            audio_device="Built-in Audio",
        )

    def add_capture(self, surface, geometry: str = "default", failure: str = "",
                    theme: str = "dark") -> int:
        images = self.root / "images" / "run-1"
        images.mkdir(parents=True, exist_ok=True)
        image = images / f"{surface.state}-{geometry}-{theme}.png"

        if not failure:
            image.write_bytes(b"png")

        return self.store.record_capture(
            self.run_id,
            state=surface.state,
            title=surface.title,
            geometry=geometry,
            theme=theme,
            image_path="" if failure else str(image),
            thumbnail_path="" if failure else str(image),
            width=1280,
            height=800,
            failure=failure,
        )

    def answer_everything(self) -> None:
        """Score every outstanding question, so the run is genuinely finished."""
        for entry in review.outstanding(self.store, self.run_id):
            review.score(
                self.store,
                entry["capture_id"],
                entry["question"],
                "pass",
                attended=entry["attended"],
            )


class OutstandingTests(ReviewTestCase):
    def test_a_fresh_capture_owes_the_three_axes(self) -> None:
        capture_id = self.add_capture(SHELL)
        pending = [entry["question"] for entry in review.outstanding(self.store, self.run_id)]

        self.assertEqual(pending, [question.id for question in surfaces.FIXED_AXES])
        self.assertTrue(capture_id)

    def test_a_behavioural_question_is_owed_to_the_attended_pass(self) -> None:
        self.add_capture(TUNER)
        attended = review.outstanding(self.store, self.run_id, attended=True)

        self.assertEqual([entry["question"] for entry in attended], ["live-input"])

    def test_a_behavioural_question_is_asked_once_per_surface(self) -> None:
        """Its answer does not vary with window size, so asking three times is friction."""
        self.add_capture(TUNER, "default")
        self.add_capture(TUNER, "constrained")

        attended = review.outstanding(self.store, self.run_id, attended=True)

        self.assertEqual(len(attended), 1)

    def test_the_axes_are_owed_at_every_resolution(self) -> None:
        """Presentation is exactly the thing that varies with size."""
        self.add_capture(TUNER, "default")
        self.add_capture(TUNER, "constrained")

        reviewable = review.outstanding(self.store, self.run_id, attended=False)

        self.assertEqual(len(reviewable), len(surfaces.FIXED_AXES) * 2)

    def test_answering_removes_a_question_from_the_list(self) -> None:
        capture_id = self.add_capture(SHELL)
        review.score(self.store, capture_id, "looks-correct", "pass")

        pending = [entry["question"] for entry in review.outstanding(self.store, self.run_id)]

        self.assertNotIn("looks-correct", pending)

    def test_a_failed_capture_still_owes_its_answers(self) -> None:
        """It is a finding to score, not a gap to skip past."""
        self.add_capture(SHELL, failure="the window never settled at a size")

        self.assertTrue(review.outstanding(self.store, self.run_id))


class ScoringTests(ReviewTestCase):
    def test_a_failure_without_a_note_is_recorded(self) -> None:
        capture_id = self.add_capture(SHELL)
        problems = review.score(self.store, capture_id, "works", "fail")

        self.assertEqual(problems, [])
        self.assertEqual(self.store.verdicts(capture_id)[0]["verdict"], "fail")

    def test_a_failure_with_a_note_is_recorded(self) -> None:
        capture_id = self.add_capture(SHELL)
        problems = review.score(self.store, capture_id, "works", "fail", "the menu bar is missing")

        self.assertEqual(problems, [])

    def test_a_question_the_surface_does_not_ask_is_refused(self) -> None:
        capture_id = self.add_capture(SHELL)
        problems = review.score(self.store, capture_id, "invented-question", "pass")

        self.assertTrue(problems)

    def test_scoring_an_unknown_capture_is_refused(self) -> None:
        self.assertTrue(review.score(self.store, 9999, "works", "pass"))


class GridViewTests(ReviewTestCase):
    def test_resolutions_of_one_surface_are_grouped_together(self) -> None:
        self.add_capture(TUNER, "default")
        self.add_capture(TUNER, "constrained")
        self.add_capture(SHELL, "default")

        view = review.run_view(self.store, self.run_id)
        titles = [group["surface"] for group in view["groups"]]

        self.assertEqual(titles, [TUNER.title, SHELL.title])
        self.assertEqual(len(view["groups"][0]["captures"]), 2)

    def test_a_failed_capture_is_marked_rather_than_shown_blank(self) -> None:
        self.add_capture(SHELL, failure="could not reach the surface")
        view = review.run_view(self.store, self.run_id)
        capture = view["groups"][0]["captures"][0]

        self.assertEqual(capture["unavailable"], "failed")
        self.assertIn("could not reach", capture["failure"])

    def test_a_missing_image_file_is_marked_missing(self) -> None:
        capture_id = self.add_capture(SHELL)
        Path(self.store.capture(capture_id).image_path).unlink()

        view = review.run_view(self.store, self.run_id)

        self.assertEqual(view["groups"][0]["captures"][0]["unavailable"], "missing")

    def test_a_pruned_image_says_pruned(self) -> None:
        self.add_capture(SHELL)
        self.store.prune_images(keep=0)

        view = review.run_view(self.store, self.run_id)

        self.assertEqual(view["groups"][0]["captures"][0]["unavailable"], "pruned")

    def test_the_view_carries_the_tag_vocabulary(self) -> None:
        self.add_capture(SHELL)
        view = review.run_view(self.store, self.run_id)

        self.assertEqual({tag["name"] for tag in view["tags"]}, {"broken", "ugly", "illegible"})

    def test_only_one_capture_of_a_surface_shows_its_attended_questions(self) -> None:
        self.add_capture(TUNER, "default")
        self.add_capture(TUNER, "constrained")

        view = review.run_view(self.store, self.run_id)
        showing = [
            capture
            for capture in view["groups"][0]["captures"]
            if any(question["attended"] for question in capture["questions"])
        ]

        self.assertEqual(len(showing), 1)


class FacetTests(ReviewTestCase):
    """What a reviewer can narrow a hundred captures by."""

    def test_a_capture_carries_the_facets_of_its_surface(self) -> None:
        capture_id = self.add_capture(TUNER, "constrained")
        facets = review.facets_of(self.store.capture(capture_id))

        self.assertEqual(facets["resolution"], "constrained")
        self.assertEqual(facets["theme"], "dark")
        self.assertEqual(facets["tools"], ["tuner"])
        self.assertEqual(facets["presentation"], "docked")
        self.assertEqual(facets["area"], "workspace")
        self.assertEqual(facets["tool_count"], "1")

    def test_a_surface_with_no_tools_says_so_rather_than_nothing(self) -> None:
        """An empty facet would drop the capture out of that filter entirely."""
        capture_id = self.add_capture(SHELL)
        facets = review.facets_of(self.store.capture(capture_id))

        self.assertEqual(facets["tools"], ["none"])
        self.assertEqual(facets["presentation"], "none")
        self.assertEqual(facets["tool_count"], "0")

    def test_a_capture_of_a_surface_since_removed_still_has_the_basics(self) -> None:
        capture_id = self.store.record_capture(
            self.run_id, state="gone", title="A surface that no longer exists",
            geometry="default", theme="light",
        )
        facets = review.facets_of(self.store.capture(capture_id))

        self.assertEqual(facets["theme"], "light")
        self.assertEqual(facets["resolution"], "default")
        self.assertEqual(facets["tools"], ["none"])

    def test_the_view_offers_every_value_present_with_its_count(self) -> None:
        self.add_capture(TUNER, "default")
        self.add_capture(TUNER, "constrained")
        self.add_capture(SHELL, "default")

        facets = review.run_view(self.store, self.run_id)["facets"]

        self.assertEqual(dict(facets["resolution"]), {"default": 2, "constrained": 1})
        self.assertEqual(dict(facets["tools"]), {"tuner": 2, "none": 1})
        self.assertEqual(dict(facets["area"]), {"workspace": 2, "shell": 1})

    def test_a_filter_offers_only_what_the_run_contains(self) -> None:
        """A fixed list would offer values that match nothing in this run."""
        self.add_capture(SHELL)
        facets = review.run_view(self.store, self.run_id)["facets"]

        self.assertEqual(dict(facets["theme"]), {"dark": 1})


class VerdictFacetTests(ReviewTestCase):
    """The facets a second pass is filtered by."""

    def answered(self, *verdicts: str) -> list[dict]:
        return [{"id": f"q{index}", "verdict": verdict} for index, verdict in enumerate(verdicts)]

    def test_a_failure_anywhere_makes_the_capture_failed(self) -> None:
        """Two passes and a fail is a failing surface, and calling it passed
        would hide the one thing worth looking at."""
        self.assertEqual(review.verdict_of(self.answered("pass", "fail", "pass")), "failed")

    def test_a_skip_outranks_a_pass(self) -> None:
        """An area nobody examined is not a pass."""
        self.assertEqual(review.verdict_of(self.answered("pass", "skip")), "skipped")

    def test_everything_passing_is_passed(self) -> None:
        self.assertEqual(review.verdict_of(self.answered("pass", "pass")), "passed")

    def test_nothing_answered_is_unanswered(self) -> None:
        self.assertEqual(review.verdict_of(self.answered("", "")), "unanswered")

    def test_review_state_counts_what_is_left(self) -> None:
        self.assertEqual(review.review_state(self.answered("", "")), "unreviewed")
        self.assertEqual(review.review_state(self.answered("pass", "")), "part reviewed")
        self.assertEqual(review.review_state(self.answered("pass", "skip")), "reviewed")

    def test_an_unreviewed_capture_says_so(self) -> None:
        capture_id = self.add_capture(SHELL)
        view = review.run_view(self.store, self.run_id)
        facets = view["groups"][0]["captures"][0]["facets"]

        self.assertEqual(facets["review"], "unreviewed")
        self.assertEqual(facets["verdict"], "unanswered")
        self.assertTrue(capture_id)

    def test_a_reviewed_capture_reports_its_verdict(self) -> None:
        capture_id = self.add_capture(SHELL)
        review.score_many(self.store, [capture_id], "pass")
        facets = review.run_view(self.store, self.run_id)["groups"][0]["captures"][0]["facets"]

        self.assertEqual(facets["review"], "reviewed")
        self.assertEqual(facets["verdict"], "passed")

    def test_one_failure_is_visible_through_the_facet(self) -> None:
        capture_id = self.add_capture(SHELL)
        review.score_many(self.store, [capture_id], "pass")
        review.score(self.store, capture_id, "works", "fail", "the menu bar is missing")
        facets = review.run_view(self.store, self.run_id)["groups"][0]["captures"][0]["facets"]

        self.assertEqual(facets["verdict"], "failed")

    def test_tags_are_a_facet_and_untagged_is_a_value(self) -> None:
        """Otherwise "everything nobody flagged" is not a question you can ask."""
        tagged = self.add_capture(TUNER)
        self.add_capture(SHELL)
        self.store.apply_tag([tagged], "ugly")

        view = review.run_view(self.store, self.run_id)
        facets = {group["surface"]: group["captures"][0]["facets"]["tag"]
                  for group in view["groups"]}

        self.assertEqual(facets[TUNER.title], ["ugly"])
        self.assertEqual(facets[SHELL.title], ["untagged"])
        self.assertEqual(dict(view["facets"]["tag"]), {"ugly": 1, "untagged": 1})

    def test_a_failed_capture_is_its_own_facet_value(self) -> None:
        self.add_capture(SHELL, failure="the window never settled")
        facets = review.run_view(self.store, self.run_id)["groups"][0]["captures"][0]["facets"]

        self.assertEqual(facets["capture"], "failed")


class SelectionTests(ReviewTestCase):
    def test_a_tag_applied_to_a_selection_lands_on_all_of_it(self) -> None:
        ids = [self.add_capture(TUNER, geometry) for geometry in ("default", "constrained")]
        ids.append(self.add_capture(SHELL))
        selection = ids[:2]

        review.apply_tag(self.store, selection, "illegible")

        for capture_id in selection:
            self.assertIn("illegible", self.store.tags_for(capture_id))

        self.assertEqual(self.store.tags_for(ids[2]), [])

    def test_tagging_an_empty_selection_does_nothing(self) -> None:
        self.assertEqual(review.apply_tag(self.store, [], "ugly"), {"tagged": 0})

    def test_a_comment_belongs_to_one_image(self) -> None:
        first = self.add_capture(TUNER, "default")
        second = self.add_capture(TUNER, "constrained")

        review.add_comment(self.store, second, "the cents readout is unreadable here")

        self.assertEqual(self.store.comments_for(first), [])
        self.assertEqual(len(self.store.comments_for(second)), 1)

    def test_an_empty_comment_is_reported_not_stored(self) -> None:
        capture_id = self.add_capture(SHELL)
        result = review.add_comment(self.store, capture_id, "   ")

        self.assertIn("error", result)


class BulkScoringTests(ReviewTestCase):
    """One verdict across a selection — the common case by far."""

    def setUp(self) -> None:
        super().setUp()
        self.ids = [
            self.add_capture(TUNER, "default"),
            self.add_capture(TUNER, "constrained"),
            self.add_capture(SHELL, "default"),
        ]

    def test_a_bulk_pass_answers_every_image_in_the_selection(self) -> None:
        result = review.score_many(self.store, self.ids[:2], "pass")

        self.assertEqual(result["problems"], [])
        self.assertEqual(result["scored"], len(surfaces.FIXED_AXES) * 2)

        for capture_id in self.ids[:2]:
            with self.subTest(capture=capture_id):
                self.assertEqual(len(self.store.verdicts(capture_id)), len(surfaces.FIXED_AXES))

    def test_an_image_outside_the_selection_is_untouched(self) -> None:
        review.score_many(self.store, self.ids[:2], "pass")

        self.assertEqual(self.store.verdicts(self.ids[2]), [])

    def test_a_bulk_fail_without_a_note_is_recorded(self) -> None:
        result = review.score_many(self.store, self.ids, "fail")

        self.assertEqual(result["problems"], [])
        self.assertEqual(result["scored"], len(surfaces.FIXED_AXES) * len(self.ids))

    def test_a_bulk_fail_with_a_note_is_recorded_on_all_of_them(self) -> None:
        result = review.score_many(self.store, self.ids, "fail", "the layout is cramped")

        self.assertEqual(result["problems"], [])

        for capture_id in self.ids:
            notes = {row["note"] for row in self.store.verdicts(capture_id)}

            self.assertEqual(notes, {"the layout is cramped"})

    def test_an_answered_question_is_left_alone_by_default(self) -> None:
        """A bulk pass must not quietly overwrite a considered verdict."""
        review.score(self.store, self.ids[0], "looks-good", "fail", "cramped")
        result = review.score_many(self.store, self.ids[:1], "pass")

        self.assertEqual(result["left_alone"], 1)

        stored = {row["question"]: row["verdict"] for row in self.store.verdicts(self.ids[0])}

        self.assertEqual(stored["looks-good"], "fail")
        self.assertEqual(stored["works"], "pass")

    def test_overwrite_replaces_answered_questions(self) -> None:
        review.score(self.store, self.ids[0], "looks-good", "fail", "cramped")
        review.score_many(self.store, self.ids[:1], "skip", overwrite=True)

        stored = {row["question"]: row["verdict"] for row in self.store.verdicts(self.ids[0])}

        self.assertEqual(set(stored.values()), {"skip"})

    def test_attended_questions_are_never_bulk_answered(self) -> None:
        """A live-input question passed from a grid is a claim nobody made."""
        review.score_many(self.store, self.ids, "pass")
        attended = review.outstanding(self.store, self.run_id, attended=True)

        self.assertTrue(attended)
        self.assertEqual([entry["question"] for entry in attended], ["live-input"])

    def test_a_bulk_pass_clears_the_reviewable_outstanding_list(self) -> None:
        review.score_many(self.store, self.ids, "pass")

        self.assertEqual(review.outstanding(self.store, self.run_id, attended=False), [])

    def test_an_unknown_capture_is_reported(self) -> None:
        result = review.score_many(self.store, [9999], "pass")

        self.assertTrue(result["problems"])


class ExportTests(ReviewTestCase):
    def test_an_unfinished_review_exports_as_incomplete(self) -> None:
        """The failure mode that matters: unscored must never read as passed."""
        self.add_capture(SHELL)
        run = export_module.build(self.store, self.run_id)

        self.assertFalse(run.complete)
        self.assertTrue(run.unanswered)

    def test_a_finished_review_exports_as_complete(self) -> None:
        self.add_capture(SHELL)
        self.answer_everything()

        run = export_module.build(self.store, self.run_id)

        self.assertTrue(run.complete)
        self.assertEqual(run.unanswered, [])

    def test_an_answered_grid_with_attended_questions_left_is_incomplete(self) -> None:
        self.add_capture(TUNER)

        for entry in review.outstanding(self.store, self.run_id, attended=False):
            review.score(self.store, entry["capture_id"], entry["question"], "pass")

        run = export_module.build(self.store, self.run_id)

        self.assertFalse(run.complete)
        self.assertTrue(all(entry["attended"] for entry in run.unanswered))

    def test_a_failed_capture_is_exported_as_unreachable(self) -> None:
        self.add_capture(SHELL, failure="could not reach the surface: no approved state")
        run = export_module.build(self.store, self.run_id)

        self.assertEqual(len(run.unreachable), 1)
        self.assertIn("no approved state", run.unreachable[0]["reason"])

    def test_tags_and_comments_ride_along_as_detail(self) -> None:
        capture_id = self.add_capture(TUNER, "constrained")
        self.store.apply_tag([capture_id], "illegible")
        review.add_comment(self.store, capture_id, "cents readout unreadable")

        run = export_module.build(self.store, self.run_id)

        self.assertEqual(run.image_notes[0]["tags"], ["illegible"])
        self.assertEqual(run.image_notes[0]["comments"], ["cents readout unreadable"])
        # And the axes are still the record's comparable core.
        self.assertTrue(all(answer.question for answer in run.answers))

    def test_the_record_names_the_resolutions_and_the_machine(self) -> None:
        self.add_capture(SHELL)
        run = export_module.build(self.store, self.run_id)

        self.assertEqual(run.resolutions, ["default", "constrained"])
        self.assertTrue(run.geometry_sweep)
        self.assertIn("Test CPU", run.machine["description"])

    def test_the_written_record_is_what_the_release_gate_reads(self) -> None:
        self.add_capture(SHELL)
        self.answer_everything()

        json_path, markdown_path = export_module.write(self.store, self.run_id, self.root / "runs")
        document = json.loads(json_path.read_text(encoding="utf-8"))

        # The fields tools/scripts/release/check_manual_verification.py reads.
        self.assertEqual(document["mode"], surfaces.QUICK)
        self.assertTrue(document["complete"])
        self.assertEqual(document["commit"], "abc123")
        self.assertTrue(document["started_at"])
        self.assertTrue(all("verdict" in answer for answer in document["answers"]))
        self.assertTrue(markdown_path.exists())

    def test_a_run_with_no_captures_is_never_complete(self) -> None:
        run = export_module.build(self.store, self.run_id)

        self.assertFalse(run.complete)


class IngestTests(ReviewTestCase):
    def write(self, name: str, payload: object) -> Path:
        path = self.root / name
        path.write_text(json.dumps(payload), encoding="utf-8")

        return path

    def test_a_performance_export_is_ingested(self) -> None:
        path = self.write(
            "lab.json",
            {"measurements": [
                {"metric": "launch", "value": 412.1, "unit": "ms", "scenario": "cold"},
                {"metric": "analysis", "value": 4.2, "unit": "ms"},
            ]},
        )

        count = ingest_module.performance_export(self.store, self.run_id, path)

        self.assertEqual(count, 2)
        self.assertEqual(len(self.store.measurements(self.run_id)), 2)

    def test_a_measurement_without_a_value_fails_the_whole_ingest(self) -> None:
        path = self.write(
            "lab.json",
            {"measurements": [{"metric": "launch", "value": 412.1}, {"metric": "analysis"}]},
        )

        with self.assertRaises(ingest_module.IngestError):
            ingest_module.performance_export(self.store, self.run_id, path)

        self.assertEqual(self.store.measurements(self.run_id), [])

    def test_an_unreadable_export_says_so(self) -> None:
        path = self.root / "broken.json"
        path.write_text("{not json", encoding="utf-8")

        with self.assertRaises(ingest_module.IngestError):
            ingest_module.performance_export(self.store, self.run_id, path)

    def test_ctest_output_is_summarised(self) -> None:
        summary = ingest_module.ctest_summary(
            "98% tests passed, 1 tests failed out of 44\n\n"
            "Total Test time (real) =   3.51 sec\n"
        )

        self.assertEqual(summary, {"cases": 44, "failures": 1, "duration_seconds": 3.51})

    def test_output_that_is_not_ctest_is_rejected(self) -> None:
        with self.assertRaises(ingest_module.IngestError):
            ingest_module.ctest_summary("everything is fine")

    def test_a_suite_result_can_be_recorded_from_counts(self) -> None:
        ingest_module.test_result(
            self.store, self.run_id, suite="PracticeTakesTests", cases=412, failures=0
        )
        results = self.store.test_results(self.run_id)

        self.assertEqual(results[0]["cases"], 412)

    def test_a_junit_report_is_summarised(self) -> None:
        path = self.root / "report.xml"
        path.write_text(
            '<testsuites><testsuite tests="10" failures="2" errors="1" time="1.5"/>'
            '<testsuite tests="4" failures="0" errors="0" time="0.5"/></testsuites>',
            encoding="utf-8",
        )
        summary = ingest_module.junit_summary(path)

        self.assertEqual(summary["cases"], 14)
        self.assertEqual(summary["failures"], 3)

    def test_ingested_results_reach_the_record(self) -> None:
        self.add_capture(SHELL)
        ingest_module.test_result(self.store, self.run_id, suite="ctest", cases=44, failures=1)
        ingest_module.performance_export(
            self.store,
            self.run_id,
            self.write("lab.json", {"measurements": [{"metric": "launch", "value": 412.0}]}),
        )

        run = export_module.build(self.store, self.run_id)

        self.assertEqual(run.test_results[0]["suite"], "ctest")
        self.assertEqual(run.measurements[0]["metric"], "launch")


if __name__ == "__main__":
    unittest.main()


class BulkCommentTests(unittest.TestCase):
    """One sentence about one defect, typed once.

    Four captures got the same comment about the same border this afternoon,
    which is what this exists to stop.
    """

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = Store.open(Path(self.directory.name) / "verification.db")
        self.addCleanup(self.store.close)
        self.run_id = self.store.start_run(
            provenance=PROVENANCE,
            commit="abc123",
            mode=surfaces.QUICK,
            resolutions=("default",),
        )
        self.captures = [
            self.store.record_capture(
                self.run_id,
                state=f"surface-{index}",
                title=f"Surface {index}",
                geometry="default",
                theme="dark",
                image_path=f"/tmp/{index}.png",
                thumbnail_path=f"/tmp/{index}.thumb.png",
                width=1280,
                height=800,
                digest=f"digest-{index}",
            )
            for index in range(3)
        ]

    def test_one_comment_reaches_every_capture_in_the_selection(self) -> None:
        result = review.add_comment_to_many(self.store, self.captures, "the border has to go")

        self.assertEqual(result["written"], 3)

        for capture_id in self.captures:
            bodies = [row["body"] for row in self.store.comments_for(capture_id)]
            self.assertEqual(bodies, ["the border has to go"])

    def test_captures_outside_the_selection_are_untouched(self) -> None:
        review.add_comment_to_many(self.store, self.captures[:2], "only these two")

        self.assertEqual(self.store.comments_for(self.captures[2]), [])

    def test_an_empty_comment_is_refused(self) -> None:
        """Otherwise a stray keypress writes a blank comment onto twenty images."""
        result = review.add_comment_to_many(self.store, self.captures, "   ")

        self.assertIn("error", result)
        self.assertEqual(self.store.comments_for(self.captures[0]), [])

    def test_an_empty_selection_is_refused(self) -> None:
        self.assertIn("error", review.add_comment_to_many(self.store, [], "something"))

    def test_a_capture_that_has_gone_does_not_lose_the_rest(self) -> None:
        """A selection can outlive one of its captures; the others still get it."""
        result = review.add_comment_to_many(
            self.store, [*self.captures, 999_999], "still worth saying"
        )

        self.assertEqual(result["written"], 3)
        self.assertEqual(result["missed"], [999_999])


class DeleteCommentTests(unittest.TestCase):
    """Removing a comment typed by accident."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = Store.open(Path(self.directory.name) / "verification.db")
        self.addCleanup(self.store.close)
        self.run_id = self.store.start_run(
            provenance=PROVENANCE, commit="abc123", mode=surfaces.QUICK,
            resolutions=("default",),
        )
        self.capture = self.store.record_capture(
            self.run_id, state="s", title="S", geometry="default", theme="dark",
            image_path="/tmp/s.png", digest="d",
        )

    def test_a_comment_can_be_removed(self) -> None:
        comment_id = self.store.add_comment(self.capture, "typed by accident")

        self.assertEqual(review.delete_comment(self.store, comment_id), {"deleted": comment_id})
        self.assertEqual(self.store.comments_for(self.capture), [])

    def test_only_the_named_comment_goes(self) -> None:
        keep = self.store.add_comment(self.capture, "worth keeping")
        drop = self.store.add_comment(self.capture, "not worth keeping")

        review.delete_comment(self.store, drop)

        self.assertEqual([row["id"] for row in self.store.comments_for(self.capture)], [keep])

    def test_deleting_something_that_is_not_there_says_so(self) -> None:
        """Rather than reporting success and leaving the reader to wonder."""
        self.assertIn("error", review.delete_comment(self.store, 999_999))

    def test_comments_reach_the_page_with_their_ids(self) -> None:
        # Position in a list is not a name: without the id the page cannot say
        # which comment to remove.
        comment_id = self.store.add_comment(self.capture, "something")
        capture = self.store.captures(self.run_id)[0]
        shown = review.capture_view(self.store, capture)

        self.assertEqual(shown["comments"], [{"id": comment_id, "body": "something"}])
