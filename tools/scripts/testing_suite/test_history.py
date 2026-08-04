#!/usr/bin/env python3
"""Tests for history across runs, and for the git-tracked sync behind it.

The graphs are drawn by a library; what could be wrong is the data handed to it
— what counts as a pass, which runs belong on one line, what leaves the machine
— and that is all decided here.
"""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import history  # noqa: E402
import surfaces  # noqa: E402
from store import Store  # noqa: E402

HERE = {
    "identity": "machine-here",
    "processor": "Test CPU",
    "cores": 8,
    "memory_bytes": 16 * 1024**3,
    "graphics": "Test GPU",
    "operating_system": "Linux",
    "display": "2560x1440",
    "attributes": {},
}
ELSEWHERE = dict(HERE, identity="machine-there", processor="Another CPU")

SHELL = next(s for s in surfaces.SURFACES if s.title == "The shell with no tool open")


class HistoryTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.shared = self.root / "run-history"
        self.store = Store.open(self.root / "verification.db")
        self.addCleanup(self.store.close)

    def add_run(self, *, commit: str, provenance=None, verdicts=(), measurements=(),
                suites=()) -> int:
        run_id = self.store.start_run(
            provenance=provenance or HERE,
            commit=commit,
            mode=surfaces.FULL,
            resolutions=("default",),
        )
        # started_at comes from the clock, and several runs in one test would
        # otherwise share a key. Nudged apart explicitly.
        self.store.execute(
            "UPDATE run SET started_at = ? WHERE id = ?",
            (f"2026-08-0{run_id}T10:00:00", run_id),
        )
        self.store.commit()

        if verdicts:
            capture_id = self.store.record_capture(
                run_id, state=SHELL.state, title=SHELL.title, geometry="default",
                image_path=str(self.root / f"{commit}.png"),
            )

            for index, verdict in enumerate(verdicts):
                self.store.record_verdict(
                    capture_id, question=f"q{index}", prompt="?", verdict=verdict
                )

        if measurements:
            self.store.record_measurements(run_id, list(measurements), "benchmarks")

        for suite in suites:
            self.store.record_test_result(run_id, **suite)

        return run_id


class PassPercentTests(unittest.TestCase):
    def test_everything_passing_is_a_hundred(self) -> None:
        entry = {"passed": 9, "failed": 0, "skipped": 0}

        self.assertEqual(history.pass_percent(entry), 100.0)

    def test_skips_count_against_it(self) -> None:
        """An area nobody examined is not a pass, and letting skips vanish
        would make a barely-reviewed run look perfect."""
        entry = {"passed": 1, "failed": 0, "skipped": 1}

        self.assertEqual(history.pass_percent(entry), 50.0)

    def test_a_run_with_nothing_scored_has_no_percentage(self) -> None:
        self.assertIsNone(history.pass_percent({"passed": 0, "failed": 0, "skipped": 0}))


class CollectTests(HistoryTestCase):
    def test_a_local_run_appears_with_its_numbers(self) -> None:
        self.add_run(commit="aaa", verdicts=("pass", "pass", "fail"))
        entries = history.collect(self.store, self.shared)

        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["passed"], 2)
        self.assertEqual(entries[0]["failed"], 1)
        self.assertEqual(entries[0]["source"], "local")

    def test_runs_come_back_oldest_first(self) -> None:
        self.add_run(commit="aaa")
        self.add_run(commit="bbb")
        entries = history.collect(self.store, self.shared)

        self.assertEqual([entry["commit"] for entry in entries], ["aaa", "bbb"])

    def test_a_synced_run_from_another_machine_is_included(self) -> None:
        """Pull the repository and another machine's runs are simply there."""
        self.shared.mkdir(parents=True)
        history.write_run(
            {
                "key": history.run_key("machine-there", "2026-07-01T09:00:00", "old"),
                "started_at": "2026-07-01T09:00:00",
                "commit": "old",
                "mode": "full",
                "complete": True,
                "machine": "machine-there",
                "machine_description": "Another CPU",
                "passed": 10, "failed": 0, "skipped": 0,
                "captures": 1, "capture_failures": 0,
                "measurements": [], "suites": [],
            },
            self.shared,
        )
        self.add_run(commit="aaa")
        entries = history.collect(self.store, self.shared)

        self.assertEqual(len(entries), 2)
        self.assertEqual(entries[0]["source"], "synced")

    def test_a_run_present_in_both_places_is_counted_once(self) -> None:
        self.add_run(commit="aaa", verdicts=("pass",))
        history.sync(self.store, self.shared)
        entries = history.collect(self.store, self.shared)

        self.assertEqual(len(entries), 1)

    def test_the_local_copy_wins_over_the_synced_one(self) -> None:
        """The same run may have gained answers since it was last written out."""
        run_id = self.add_run(commit="aaa", verdicts=("pass",))
        history.sync(self.store, self.shared)

        capture_id = self.store.captures(run_id)[0].id
        self.store.record_verdict(capture_id, question="q9", prompt="?", verdict="fail")

        entries = history.collect(self.store, self.shared)

        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["failed"], 1)

    def test_an_unreadable_file_is_skipped_rather_than_fatal(self) -> None:
        self.shared.mkdir(parents=True)
        (self.shared / "broken.json").write_text("{not json", encoding="utf-8")
        self.add_run(commit="aaa")

        self.assertEqual(len(history.collect(self.store, self.shared)), 1)


class SyncTests(HistoryTestCase):
    def test_syncing_writes_one_file_per_run(self) -> None:
        """One file per run is what lets two machines commit without conflict."""
        self.add_run(commit="aaa")
        self.add_run(commit="bbb")
        result = history.sync(self.store, self.shared)

        self.assertEqual(len(result["written"]), 2)
        self.assertEqual(len(list(self.shared.glob("*.json"))), 2)

    def test_syncing_twice_writes_nothing_the_second_time(self) -> None:
        self.add_run(commit="aaa")
        history.sync(self.store, self.shared)
        result = history.sync(self.store, self.shared)

        self.assertEqual(result["written"], [])
        self.assertEqual(result["unchanged"], 1)

    def test_a_changed_run_is_written_again(self) -> None:
        run_id = self.add_run(commit="aaa", verdicts=("pass",))
        history.sync(self.store, self.shared)

        capture_id = self.store.captures(run_id)[0].id
        self.store.record_verdict(capture_id, question="q9", prompt="?", verdict="pass")

        self.assertEqual(len(history.sync(self.store, self.shared)["written"]), 1)

    def test_no_image_path_ever_reaches_the_shared_directory(self) -> None:
        """Images stay on the machine; only the numbers are shared."""
        self.add_run(commit="aaa", verdicts=("pass",))
        history.sync(self.store, self.shared)
        written = (self.shared / next(iter(p.name for p in self.shared.glob("*.json"))))
        text = written.read_text(encoding="utf-8")

        self.assertNotIn(".png", text)
        self.assertNotIn("image", text)
        self.assertNotIn("thumbnail", text)

    def test_the_written_file_carries_the_numbers_worth_graphing(self) -> None:
        self.add_run(
            commit="aaa",
            verdicts=("pass", "fail"),
            measurements=({"metric": "launch", "value": 412.0, "unit": "ms"},),
            suites=({"suite": "ctest", "cases": 44, "failures": 1},),
        )
        history.sync(self.store, self.shared)
        entry = json.loads(next(self.shared.glob("*.json")).read_text(encoding="utf-8"))

        self.assertEqual(entry["passed"], 1)
        self.assertEqual(entry["failed"], 1)
        self.assertEqual(entry["measurements"][0]["metric"], "launch")
        self.assertEqual(entry["suites"][0]["cases"], 44)
        self.assertNotIn("run_id", entry)

    def test_a_local_run_id_never_leaks_into_shared_history(self) -> None:
        """Run 4 here is not run 4 anywhere else; the key is what identifies it."""
        self.add_run(commit="aaa")
        history.sync(self.store, self.shared)
        entry = json.loads(next(self.shared.glob("*.json")).read_text(encoding="utf-8"))

        self.assertIn("key", entry)
        self.assertNotIn("run_id", entry)


class SeriesTests(HistoryTestCase):
    def test_only_the_chosen_machine_is_plotted(self) -> None:
        """A timing from another processor is not a point on this line."""
        self.add_run(commit="aaa", verdicts=("pass",),
                     measurements=({"metric": "launch", "value": 400.0, "unit": "ms"},))
        self.add_run(commit="bbb", provenance=ELSEWHERE, verdicts=("pass",),
                     measurements=({"metric": "launch", "value": 900.0, "unit": "ms"},))

        data = history.series(history.collect(self.store, self.shared), "machine-here")

        self.assertEqual(len(data["runs"]), 1)
        self.assertEqual(data["metrics"][0]["points"][0]["value"], 400.0)

    def test_every_known_machine_is_offered(self) -> None:
        self.add_run(commit="aaa")
        self.add_run(commit="bbb", provenance=ELSEWHERE)
        data = history.series(history.collect(self.store, self.shared), "machine-here")

        self.assertEqual(len(data["machines"]), 2)

    def test_a_run_with_nothing_scored_is_not_a_point_on_the_line(self) -> None:
        self.add_run(commit="aaa")
        data = history.series(history.collect(self.store, self.shared), "machine-here")

        self.assertEqual(data["runs"], [])

    def test_measurements_become_one_series_per_metric(self) -> None:
        for commit, value in (("aaa", 400.0), ("bbb", 420.0)):
            self.add_run(commit=commit, measurements=(
                {"metric": "launch", "value": value, "unit": "ms"},
                {"metric": "fifo push", "value": 500.0, "unit": "ns"},
            ))

        data = history.series(history.collect(self.store, self.shared), "machine-here")
        metrics = {metric["metric"]: metric for metric in data["metrics"]}

        self.assertEqual(set(metrics), {"launch", "fifo push"})
        self.assertEqual([point["value"] for point in metrics["launch"]["points"]], [400.0, 420.0])
        self.assertEqual(metrics["fifo push"]["unit"], "ns")

    def test_points_are_in_run_order(self) -> None:
        for commit, value in (("aaa", 1.0), ("bbb", 2.0), ("ccc", 3.0)):
            self.add_run(commit=commit,
                         measurements=({"metric": "launch", "value": value, "unit": "ms"},))

        data = history.series(history.collect(self.store, self.shared), "machine-here")

        self.assertEqual(
            [point["value"] for point in data["metrics"][0]["points"]], [1.0, 2.0, 3.0]
        )

    def test_a_trend_reports_the_change_since_the_run_before(self) -> None:
        summary = history.trend([{"value": 400.0}, {"value": 420.0}])

        self.assertEqual(summary["latest"], 420.0)
        self.assertEqual(summary["delta"], 20.0)
        self.assertEqual(summary["best"], 400.0)

    def test_a_single_point_has_no_delta(self) -> None:
        summary = history.trend([{"value": 400.0}])

        self.assertIsNone(summary["delta"])
        self.assertEqual(summary["count"], 1)


if __name__ == "__main__":
    unittest.main()
