#!/usr/bin/env python3
"""Tests for the run store and the machine provenance behind it.

Everything here runs against a temporary database, so the suite never touches
the developer's real store and never needs a display, an application, or Pillow.
"""

from __future__ import annotations

from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import machine  # noqa: E402
import store as store_module  # noqa: E402
from store import Store, StoreError  # noqa: E402

PROVENANCE = {
    "identity": "machine-one",
    "processor": "Test CPU",
    "cores": 8,
    "memory_bytes": 16 * 1024**3,
    "graphics": "Test GPU",
    "operating_system": "Linux Debian GNU/Linux",
    "display": "2560x1440",
    "attributes": {"kernel": "6.12.90"},
}


class StoreTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "verification.db"
        self.store = Store.open(self.path)
        self.addCleanup(self.store.close)

    def start_run(self, **overrides) -> int:
        arguments = dict(
            provenance=PROVENANCE,
            commit="abc123",
            mode="full",
            resolutions=("default", "constrained"),
        )
        arguments.update(overrides)

        return self.store.start_run(**arguments)

    def capture(self, run_id: int, geometry: str = "default", **overrides) -> int:
        arguments = dict(
            state="tuner-docked",
            title="The tuner, docked",
            geometry=geometry,
            image_path=str(self.path.parent / f"{geometry}.png"),
        )
        arguments.update(overrides)

        return self.store.record_capture(run_id, **arguments)


class MigrationTests(StoreTestCase):
    def test_a_fresh_store_is_at_the_current_schema(self) -> None:
        self.assertEqual(self.store._current_version(), store_module.SCHEMA_VERSION)

    def test_an_existing_store_reopens_with_its_rows(self) -> None:
        run_id = self.start_run()
        self.store.close()

        reopened = Store.open(self.path)
        self.addCleanup(reopened.close)

        self.assertEqual(int(reopened.run(run_id)["id"]), run_id)

    def test_a_store_from_a_newer_suite_is_refused(self) -> None:
        """Writing into a schema this version does not understand corrupts history."""
        connection = sqlite3.connect(self.path)
        connection.execute(
            "INSERT INTO schema_version (version) VALUES (?)", (store_module.SCHEMA_VERSION + 5,)
        )
        connection.commit()
        connection.close()

        with self.assertRaises(StoreError) as raised:
            Store.open(self.path)

        self.assertIn("newer", str(raised.exception))

    def test_the_tag_vocabulary_starts_populated(self) -> None:
        self.assertEqual(
            {row["name"] for row in self.store.tags()}, {"broken", "ugly", "illegible"}
        )


class MachineTests(StoreTestCase):
    def test_the_same_machine_is_reused(self) -> None:
        first = self.store.machine_id(PROVENANCE)
        second = self.store.machine_id(PROVENANCE)

        self.assertEqual(first, second)

    def test_a_kernel_upgrade_does_not_create_a_new_machine(self) -> None:
        """Otherwise every system update silently throws away comparison history."""
        before = dict(PROVENANCE)
        before.pop("identity")
        after = dict(before)

        self.assertEqual(machine.identity_of(before), machine.identity_of(after))

        upgraded = dict(before)
        upgraded["attributes"] = {"kernel": "6.13.0"}

        self.assertEqual(machine.identity_of(before), machine.identity_of(upgraded))

    def test_new_hardware_is_a_new_machine(self) -> None:
        facts = {key: value for key, value in PROVENANCE.items() if key != "identity"}
        more_memory = dict(facts, memory_bytes=32 * 1024**3)
        other_cpu = dict(facts, processor="Some Other CPU")

        self.assertNotEqual(machine.identity_of(facts), machine.identity_of(more_memory))
        self.assertNotEqual(machine.identity_of(facts), machine.identity_of(other_cpu))

    def test_identity_covers_the_documented_fields(self) -> None:
        facts = {key: value for key, value in PROVENANCE.items() if key != "identity"}
        baseline = machine.identity_of(facts)

        for field in machine.IDENTITY_FIELDS:
            with self.subTest(field=field):
                changed = dict(facts, **{field: "something else"})

                self.assertNotEqual(baseline, machine.identity_of(changed))


class CaptureRecordingTests(StoreTestCase):
    def test_a_capture_round_trips(self) -> None:
        run_id = self.start_run()
        capture_id = self.capture(run_id, width=1280, height=800, digest="abc")
        stored = self.store.capture(capture_id)

        self.assertIsNotNone(stored)
        self.assertEqual(stored.width, 1280)
        self.assertEqual(stored.geometry, "default")

    def test_recapturing_replaces_rather_than_duplicates(self) -> None:
        run_id = self.start_run()
        first = self.capture(run_id)
        second = self.capture(run_id, width=999)

        self.assertEqual(first, second)
        self.assertEqual(len(self.store.captures(run_id)), 1)

    def test_a_failed_capture_is_a_row_not_an_absence(self) -> None:
        """A surface nobody could look at is a finding, not a gap in the grid."""
        run_id = self.start_run()
        capture_id = self.store.record_capture(
            run_id,
            state="fullscreen",
            title="Fullscreen",
            geometry="default",
            failure="the window never settled at a size",
        )
        stored = self.store.capture(capture_id)

        self.assertTrue(stored.failed)
        self.assertIn("never settled", stored.failure)

    def test_a_resumed_pass_knows_what_it_already_has(self) -> None:
        run_id = self.start_run()
        self.capture(run_id, "default")
        self.store.record_capture(
            run_id, state="empty", title="The shell with no tool open",
            geometry="default", failure="could not be reached",
        )

        keys = self.store.captured_keys(run_id)

        self.assertIn(("tuner-docked", "The tuner, docked", "default"), keys)
        self.assertIn(("empty", "The shell with no tool open", "default"), keys)


class VerdictTests(StoreTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.run_id = self.start_run()
        self.capture_id = self.capture(self.run_id)

    def test_a_failure_without_a_note_is_refused(self) -> None:
        problems = self.store.record_verdict(
            self.capture_id, question="works", prompt="Does it work?", verdict="fail"
        )

        self.assertTrue(problems)
        self.assertEqual(self.store.verdicts(self.capture_id), [])

    def test_a_failure_with_a_note_is_stored(self) -> None:
        problems = self.store.record_verdict(
            self.capture_id,
            question="works",
            prompt="Does it work?",
            verdict="fail",
            note="the needle never moves",
        )

        self.assertEqual(problems, [])
        self.assertEqual(len(self.store.verdicts(self.capture_id)), 1)

    def test_an_unknown_verdict_is_refused(self) -> None:
        problems = self.store.record_verdict(
            self.capture_id, question="works", prompt="Does it work?", verdict="probably"
        )

        self.assertTrue(problems)

    def test_rescoring_replaces_the_answer(self) -> None:
        self.store.record_verdict(
            self.capture_id, question="works", prompt="Does it work?", verdict="skip"
        )
        self.store.record_verdict(
            self.capture_id, question="works", prompt="Does it work?", verdict="pass"
        )
        verdicts = self.store.verdicts(self.capture_id)

        self.assertEqual(len(verdicts), 1)
        self.assertEqual(verdicts[0]["verdict"], "pass")


class TagAndCommentTests(StoreTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.run_id = self.start_run()
        self.ids = [
            self.capture(self.run_id, geometry)
            for geometry in ("default", "constrained", "maximised")
        ]

    def test_a_tag_applied_to_a_selection_lands_on_every_image(self) -> None:
        self.store.apply_tag(self.ids, "illegible")

        for capture_id in self.ids:
            with self.subTest(capture=capture_id):
                self.assertIn("illegible", self.store.tags_for(capture_id))

    def test_removing_a_tag_leaves_untagged_images_alone(self) -> None:
        self.store.apply_tag(self.ids[:2], "ugly")
        self.store.remove_tag(self.ids, "ugly")

        for capture_id in self.ids:
            self.assertEqual(self.store.tags_for(capture_id), [])

    def test_applying_a_tag_twice_is_harmless(self) -> None:
        self.store.apply_tag(self.ids, "broken")
        self.store.apply_tag(self.ids, "broken")

        self.assertEqual(self.store.tags_for(self.ids[0]), ["broken"])

    def test_a_new_tag_joins_the_vocabulary(self) -> None:
        """The vocabulary is data, so a reviewer adds to it mid-review."""
        self.store.apply_tag([self.ids[0]], "cramped")

        self.assertIn("cramped", {row["name"] for row in self.store.tags()})

    def test_a_tag_needs_a_name(self) -> None:
        with self.assertRaises(StoreError):
            self.store.add_tag("   ")

    def test_a_comment_is_kept_with_the_image(self) -> None:
        self.store.add_comment(self.ids[0], "the cents readout is unreadable here")

        self.assertEqual(len(self.store.comments_for(self.ids[0])), 1)
        self.assertEqual(self.store.comments_for(self.ids[1]), [])

    def test_an_empty_comment_is_refused(self) -> None:
        with self.assertRaises(StoreError):
            self.store.add_comment(self.ids[0], "  ")


class MeasurementTests(StoreTestCase):
    def test_measurements_are_stored_against_the_run(self) -> None:
        run_id = self.start_run()
        count = self.store.record_measurements(
            run_id, [{"metric": "launch", "value": 412.0, "unit": "ms"}], "performance-lab"
        )

        self.assertEqual(count, 1)
        self.assertEqual(len(self.store.measurements(run_id)), 1)

    def test_a_malformed_set_stores_none_of_itself(self) -> None:
        """A half-ingested export understates what was measured."""
        run_id = self.start_run()

        with self.assertRaises(StoreError):
            self.store.record_measurements(
                run_id,
                [
                    {"metric": "launch", "value": 412.0},
                    {"metric": "analysis"},  # no value
                ],
                "performance-lab",
            )

        self.assertEqual(self.store.measurements(run_id), [])

    def test_a_comparison_uses_earlier_runs_on_the_same_machine(self) -> None:
        first = self.start_run()
        self.store.record_measurements(first, [{"metric": "launch", "value": 400.0}], "lab")
        second = self.start_run()
        self.store.record_measurements(second, [{"metric": "launch", "value": 450.0}], "lab")

        comparison = self.store.compare_metric(second, "launch")

        self.assertEqual(comparison["baseline"], 400.0)
        self.assertEqual(comparison["delta"], 50.0)

    def test_a_comparison_never_crosses_machines(self) -> None:
        """Another processor's launch time is not a baseline."""
        elsewhere = dict(PROVENANCE, identity="machine-two", processor="Other CPU")
        other = self.start_run(provenance=elsewhere)
        self.store.record_measurements(other, [{"metric": "launch", "value": 400.0}], "lab")

        here = self.start_run()
        self.store.record_measurements(here, [{"metric": "launch", "value": 450.0}], "lab")

        comparison = self.store.compare_metric(here, "launch")

        self.assertIsNone(comparison["baseline"])
        self.assertEqual(comparison["reason"], "no baseline on this machine")


class TestResultTests(StoreTestCase):
    def test_a_suite_result_is_recorded(self) -> None:
        run_id = self.start_run()
        self.store.record_test_result(
            run_id, suite="PracticeTakesTests", cases=412, failures=1, duration_seconds=3.5
        )
        results = self.store.test_results(run_id)

        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["failures"], 1)

    def test_rerunning_a_suite_replaces_its_result(self) -> None:
        run_id = self.start_run()
        self.store.record_test_result(run_id, suite="ctest", cases=10, failures=2)
        self.store.record_test_result(run_id, suite="ctest", cases=10, failures=0)
        results = self.store.test_results(run_id)

        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["failures"], 0)


class PruneTests(StoreTestCase):
    def test_pruning_keeps_the_decisions_and_drops_the_pixels(self) -> None:
        run_id = self.start_run()
        image = self.path.parent / "default.png"
        image.write_bytes(b"not really a png")
        capture_id = self.capture(run_id, image_path=str(image))
        self.store.apply_tag([capture_id], "ugly")
        self.store.add_comment(capture_id, "worth another look")
        self.store.record_verdict(
            capture_id, question="works", prompt="Does it work?", verdict="pass"
        )

        # A second run, so the first falls outside the keep count.
        self.start_run()
        removed = self.store.prune_images(keep=1)

        self.assertEqual(len(removed), 1)
        self.assertFalse(image.exists())

        stored = self.store.capture(capture_id)

        self.assertTrue(stored.pruned)
        self.assertEqual(self.store.tags_for(capture_id), ["ugly"])
        self.assertEqual(len(self.store.comments_for(capture_id)), 1)
        self.assertEqual(len(self.store.verdicts(capture_id)), 1)

    def test_the_kept_runs_keep_their_images(self) -> None:
        run_id = self.start_run()
        image = self.path.parent / "kept.png"
        image.write_bytes(b"still here")
        self.capture(run_id, image_path=str(image))

        self.store.prune_images(keep=5)

        self.assertTrue(image.exists())


if __name__ == "__main__":
    unittest.main()
