"""Tests for the roadmap forecast: crediting, the simulation, and publishing."""

from __future__ import annotations

import datetime as dt
import importlib.util
import json
from pathlib import Path
import sys
import unittest

_PATH = Path(__file__).resolve().parent / "forecast_roadmap.py"
_SPEC = importlib.util.spec_from_file_location("forecast_roadmap", _PATH)
assert _SPEC is not None and _SPEC.loader is not None
fr = importlib.util.module_from_spec(_SPEC)
sys.modules["forecast_roadmap"] = fr
_SPEC.loader.exec_module(fr)

D = dt.date.fromisoformat


def small_ledger(**overrides) -> dict:
    raw = {
        "roadmap_issue": 10,
        "baseline": "2026-11-09",
        "trackers": [12, 13],
        "model": {
            "runs": 300,
            "seed": 7,
            "active_to_idle_beta": [3, 9],
            "idle_to_active_beta": [3, 6],
            "rate_mean": 4.0,
            "rate_sd": 0.6,
            "rate_floor": 1.5,
            "weekly_sigma": 0.4,
            "bias_sigma": 0.15,
            "growth_max": 0.94,
            "active_window_days": 14,
            "horizon_weeks": 400,
        },
        "releases": [
            {"id": "a", "name": "A", "tracker": 12, "target": "2026-11-16", "commitment": "2026-11-29"},
            {"id": "b", "name": "B", "tracker": 13, "target": "2027-01-04", "commitment": "2027-02-22"},
        ],
        "checkpoints": [
            {"date": "2026-12-31", "floor": 12, "target": 20},
            {"date": "2027-01-31", "floor": 18, "target": 30},
        ],
        "excluded": [{"issue": 99, "reason": "not counted"}],
        "items": [
            {"issue": 1, "size": 2.0, "release": "a"},
            {"issue": 2, "size": 10.0, "release": "b"},
            {"issue": 3, "size": 6.0, "release": "b"},
        ],
    }
    raw.update(overrides)
    return raw


def state(number, *, open=True, reason=None, closed=None):
    return fr.issue_state_from_api(
        {"number": number, "state": "open" if open else "closed", "state_reason": reason, "closed_at": closed}
    )


class LedgerTests(unittest.TestCase):
    def test_the_committed_ledger_loads_and_totals_the_published_scope(self):
        ledger = fr.load_ledger()
        self.assertEqual(ledger.roadmap_issue, 10)
        # #10 publishes 110.5 estimate-weeks to 1.0; the file and the issue must agree.
        self.assertAlmostEqual(sum(i.size for i in ledger.items), 110.5)
        per_release = {r.id: 0.0 for r in ledger.releases}
        for item in ledger.items:
            per_release[item.release] += item.size
        # The MVP covers the piano and the voice together (2026-11-08).
        self.assertAlmostEqual(per_release["mvp"], 30.25)

    def test_committed_releases_are_in_date_order(self):
        ledger = fr.load_ledger()
        commitments = [r.commitment for r in ledger.releases]
        self.assertEqual(commitments, sorted(commitments))

    def test_duplicate_issue_is_refused(self):
        raw = small_ledger()
        raw["items"].append({"issue": 1, "size": 1.0, "release": "a"})
        with self.assertRaisesRegex(fr.LedgerError, "twice"):
            fr.parse_ledger(raw)

    def test_unknown_release_is_refused(self):
        raw = small_ledger()
        raw["items"].append({"issue": 4, "size": 1.0, "release": "zzz"})
        with self.assertRaisesRegex(fr.LedgerError, "unknown release"):
            fr.parse_ledger(raw)

    def test_an_issue_cannot_be_both_sized_and_excluded(self):
        raw = small_ledger(excluded=[{"issue": 1, "reason": "x"}])
        with self.assertRaisesRegex(fr.LedgerError, "both sized and excluded"):
            fr.parse_ledger(raw)


class AssessTests(unittest.TestCase):
    def setUp(self):
        self.ledger = fr.parse_ledger(small_ledger())

    def test_completed_work_is_credited_and_leaves_the_remainder(self):
        progress = fr.assess(
            self.ledger,
            {1: state(1, open=False, reason="completed", closed="2026-11-20T10:00:00Z"), 2: state(2), 3: state(3)},
        )
        self.assertEqual(progress.credited, 2.0)
        self.assertEqual(progress.remaining_by_release, {"a": 0.0, "b": 16.0})
        self.assertEqual(progress.last_completed, D("2026-11-20"))

    def test_not_planned_leaves_scope_without_credit(self):
        progress = fr.assess(
            self.ledger,
            {1: state(1), 2: state(2, open=False, reason="not_planned", closed="2026-11-20T00:00:00Z"), 3: state(3)},
        )
        self.assertEqual(progress.credited, 0.0)
        self.assertEqual(progress.remaining_by_release["b"], 6.0)
        self.assertEqual(progress.dropped, (2,))

    def test_work_closed_before_the_baseline_is_not_credited(self):
        progress = fr.assess(
            self.ledger,
            {1: state(1, open=False, reason="completed", closed="2026-11-01T00:00:00Z"), 2: state(2), 3: state(3)},
        )
        self.assertEqual(progress.credited, 0.0)
        self.assertEqual(progress.remaining_by_release["a"], 0.0)

    def test_an_issue_github_does_not_return_counts_as_open(self):
        progress = fr.assess(self.ledger, {1: state(1), 2: state(2)})
        self.assertEqual(progress.unknown, (3,))
        self.assertEqual(progress.remaining_by_release["b"], 16.0)

    def test_unsized_children_are_reported_but_excluded_and_closed_ones_are_not(self):
        children = {12: [state(1), state(50), state(99), state(51, open=False, reason="completed")]}
        self.assertEqual(fr.unsized_open_issues(self.ledger, children), [50])


class ForecastTests(unittest.TestCase):
    def setUp(self):
        self.ledger = fr.parse_ledger(small_ledger())
        self.today = D("2026-11-09")

    def test_the_simulation_is_deterministic(self):
        a = fr.simulate([2.0, 18.0], self.ledger.model, active=True, runs=200)
        b = fr.simulate([2.0, 18.0], self.ledger.model, active=True, runs=200)
        self.assertEqual(a, b)

    def test_later_releases_never_land_before_earlier_ones(self):
        for run in fr.simulate([2.0, 18.0], self.ledger.model, active=False, runs=200):
            self.assertLessEqual(run[0], run[1])

    def test_quantiles_are_ordered(self):
        progress = fr.assess(self.ledger, {1: state(1), 2: state(2), 3: state(3)})
        for f in fr.forecast(self.ledger, progress, self.today, runs=300):
            self.assertLessEqual(f.dates[0.5], f.dates[0.8])
            self.assertLessEqual(f.dates[0.8], f.dates[0.9])

    def test_less_remaining_work_never_forecasts_later(self):
        open_all = fr.assess(self.ledger, {1: state(1), 2: state(2), 3: state(3)})
        half_done = fr.assess(
            self.ledger,
            {1: state(1), 2: state(2, open=False, reason="completed", closed="2026-11-09T00:00:00Z"), 3: state(3)},
        )
        before = fr.forecast(self.ledger, open_all, self.today, runs=300)[1].dates[0.8]
        after = fr.forecast(self.ledger, half_done, self.today, runs=300)[1].dates[0.8]
        self.assertLessEqual(after, before)

    def test_a_release_with_nothing_left_is_done(self):
        progress = fr.assess(
            self.ledger,
            {1: state(1, open=False, reason="completed", closed="2026-11-10T00:00:00Z"), 2: state(2), 3: state(3)},
        )
        first = fr.forecast(self.ledger, progress, self.today, runs=50)[0]
        self.assertTrue(first.done)
        self.assertEqual(first.status, "done")

    def test_a_long_idle_spell_starts_the_forecast_idle(self):
        progress = fr.assess(self.ledger, {1: state(1), 2: state(2), 3: state(3)})
        self.assertTrue(fr.starts_active(self.ledger, progress, D("2026-11-20")))
        self.assertFalse(fr.starts_active(self.ledger, progress, D("2026-12-20")))

    def test_judge_compares_against_the_commitment(self):
        release = self.ledger.releases[1]  # committed 22 Feb 2027
        self.assertEqual(fr.judge(release, False, D("2027-01-01"), D("2027-02-01")), "on track")
        self.assertEqual(fr.judge(release, False, D("2027-01-01"), D("2027-03-01")), "at risk")
        self.assertEqual(fr.judge(release, False, D("2027-03-01"), D("2027-04-01")), "behind")


class CheckpointTests(unittest.TestCase):
    def setUp(self):
        self.ledger = fr.parse_ledger(small_ledger())

    def test_next_checkpoint_verdicts_on_its_day(self):
        today = D("2026-12-31")
        self.assertEqual(fr.checkpoint_view(self.ledger, 25, today).verdict, "on target")
        self.assertEqual(fr.checkpoint_view(self.ledger, 15, today).verdict, "above the floor")
        self.assertEqual(fr.checkpoint_view(self.ledger, 5, today).verdict, "below the floor")

    def test_day_one_with_nothing_closed_is_not_behind(self):
        view = fr.checkpoint_view(self.ledger, 0, D("2026-11-09"))
        self.assertEqual(view.floor_today, 0)
        self.assertNotEqual(view.verdict, "below the floor")

    def test_mid_month_is_judged_against_the_pro_rated_floor(self):
        # Halfway from the baseline (9 Nov, 0) to 31 Dec (floor 12): floor ~6.
        view = fr.checkpoint_view(self.ledger, 7, D("2026-12-05"))
        self.assertAlmostEqual(view.floor_today, 12 * 26 / 52)
        self.assertEqual(view.verdict, "above the floor")
        self.assertEqual(fr.checkpoint_view(self.ledger, 5, D("2026-12-05")).verdict, "below the floor")

    def test_a_passed_checkpoint_is_judged_against_its_floor(self):
        view = fr.checkpoint_view(self.ledger, 11, D("2027-01-05"))
        self.assertEqual(view.last.date, D("2026-12-31"))
        self.assertFalse(view.last_met)
        self.assertEqual(view.next.date, D("2027-01-31"))


class PublishingTests(unittest.TestCase):
    def setUp(self):
        self.ledger = fr.parse_ledger(small_ledger())
        self.today = D("2026-11-09")
        self.progress = fr.assess(self.ledger, {1: state(1), 2: state(2), 3: state(3)})
        self.block = fr.render_block(
            self.ledger,
            self.progress,
            fr.forecast(self.ledger, self.progress, self.today, runs=100),
            fr.checkpoint_view(self.ledger, 0, self.today),
            [50],
            today=self.today,
            trigger="#36 closed",
        )

    def test_block_is_wrapped_in_markers_and_names_what_matters(self):
        self.assertTrue(self.block.startswith(fr.BLOCK_START))
        self.assertTrue(self.block.endswith(fr.BLOCK_END))
        self.assertIn("after #36 closed", self.block)
        self.assertIn("**Remaining:** 18", self.block)
        self.assertIn("22 Feb 2027", self.block)  # the commitment is shown, unchanged
        self.assertIn("#50", self.block)  # unsized scope is surfaced

    def test_splice_replaces_only_the_block(self):
        body = f"intro\n\n{fr.BLOCK_START}\nold\n{fr.BLOCK_END}\n\noutro"
        spliced = fr.splice_block(body, self.block)
        self.assertTrue(spliced.startswith("intro\n\n"))
        self.assertTrue(spliced.endswith("\n\noutro"))
        self.assertNotIn("\nold\n", spliced)
        # Idempotent: splicing the same block again changes nothing.
        self.assertEqual(fr.splice_block(spliced, self.block), spliced)

    def test_splice_refuses_a_body_without_markers(self):
        with self.assertRaises(fr.MissingMarkersError):
            fr.splice_block("no markers here", self.block)

    def test_splice_refuses_duplicate_markers(self):
        body = f"{fr.BLOCK_START}\n{fr.BLOCK_END}\n{fr.BLOCK_START}\n{fr.BLOCK_END}"
        with self.assertRaises(fr.MissingMarkersError):
            fr.splice_block(body, self.block)


class OfflineRunTests(unittest.TestCase):
    def test_main_renders_from_a_saved_listing_without_github(self):
        import io
        import tempfile
        from contextlib import redirect_stdout

        ledger = fr.load_ledger()
        listing = [{"number": i.issue, "state": "open", "state_reason": None, "closed_at": None} for i in ledger.items]
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
            json.dump(listing, handle)
        out = io.StringIO()
        with redirect_stdout(out):
            code = fr.main(["--issues-json", handle.name, "--today", "2026-11-09", "--runs", "200"])
        Path(handle.name).unlink()
        self.assertEqual(code, 0)
        self.assertIn("**Remaining:** 110.5", out.getvalue())


if __name__ == "__main__":
    unittest.main()
