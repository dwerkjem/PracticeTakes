# The roadmap forecast

The roadmap issue, [#10](https://github.com/dwerkjem/PracticeTakes/issues/10),
makes **commitments**: one date per release, each with an 80% chance of being
met if the future behaves like the project's measured past. A commitment is only
worth something if slipping is visible the day it starts. So every time an issue
is closed or reopened, a workflow re-runs the forecast against what is left and
rewrites the **Live forecast** block in #10. When the ledger changes on `main`,
the same workflow re-forecasts and sets the milestones' due dates to the
commitments, so nobody copies a date across by hand.

| Piece | Path |
|---|---|
| The ledger: every issue to 1.0, its size, its release | `tools/scripts/planning/roadmap_ledger.json` |
| The forecast and the publisher | `tools/scripts/planning/forecast_roadmap.py` |
| Its tests | `tools/scripts/planning/test_forecast_roadmap.py` |
| The milestone sync, and its tests | `tools/scripts/planning/sync_milestones.py`, `test_sync_milestones.py` |
| The workflow | `.github/workflows/roadmap-forecast.yml` |

## What it does on each run

1. **Reads every issue's state** from GitHub, and the sub-issues of each stage
   tracker (#12–#15).
2. **Credits closed work.** An issue in the ledger closed *as completed* on or
   after the baseline (9 Nov 2026) adds its size to *delivered*. Closed *as not
   planned* removes it from what is left without crediting it, so cutting scope
   never passes for progress. Reopening an issue takes its credit back.
3. **Re-runs the forecast** on the work left in each release, in release order.
   It uses the same Monte Carlo model the commitments were made from (below),
   seeded, so the same state always gives the same dates.
4. **Judges each release against its commitment**, which the script never
   changes:
   - 🟢 *on track*: the 80% forecast meets the commitment;
   - 🟠 *at risk*: only the 50% forecast does;
   - 🔴 *behind*: neither does;
   - ✅ *done*: nothing left in it or before it.
5. **Judges the monthly checkpoint.** It compares delivered work against the
   floor and target pro-rated to today, on a straight line between checkpoints,
   so day three of a month doesn't read as behind. It also says whether the last
   checkpoint's floor was met.
6. **Flags scope the forecast can't see:** any open sub-issue of a stage tracker
   that the ledger neither sizes nor excludes.
7. **Splices the block** between `<!-- roadmap-forecast:start -->` and
   `<!-- roadmap-forecast:end -->` in #10's body, leaving the rest untouched,
   and writes the same block to the run's summary.

A commitment moves only by a person's decision recorded in #10, never by this
workflow. If a checkpoint is below the floor, #10's rule is to cut scope from
the next release first.

## The milestones

The ledger's `milestones` list names each GitHub milestone by number, with its
title, its description, and the releases it stands for. Stage 4, the Beta,
stands for Beta 1, Beta 2 and the Beta. A milestone is **due on the commitment
of the latest release it names**.

When a push to `main` changes the ledger, or the workflow is run by hand,
`sync_milestones.py` compares each named milestone with GitHub and sends only
the fields that differ. Running it twice changes nothing the second time. It
never creates or closes a milestone, and leaves milestones the ledger doesn't
name alone. A number in the ledger that doesn't exist on GitHub fails the run.

So **edit a milestone's title, description or due date in the ledger, not on
GitHub**: a change made on GitHub is put back the next time the ledger changes.
Due dates are sent at midday UTC, so they read as the same date from UTC-11 to
UTC+11.

## Keeping the ledger true

The forecast is exactly as honest as the ledger.

- **A new roadmap issue gets a size in the ledger** in the same pull request that
  schedules it, at its estimate's midpoint in weeks. Until then the Live forecast
  lists it under *Not in the forecast yet*.
- **Moving an issue between releases** means changing its `release`, alongside
  the sub-issue and milestone move on GitHub.
- **Splitting an issue** means giving each half its own entry and splitting the
  size between them, as was done for #36/#286 and #52/#287.
- **An issue that deliberately carries no size**, such as a hardware check or a
  maybe, goes in `excluded` with a reason.
- **Changing a commitment** means editing `commitment` *and* recording the
  decision and its reason in #10. Changing a target is the same. The
  milestone's due date follows when the change reaches `main`.
- `parse_ledger` refuses duplicates, unknown releases, non-positive sizes and an
  issue both sized and excluded. The tests also check that the committed ledger
  still totals the 128 estimate-weeks #10 publishes. Change that test when the
  scope is deliberately changed.

## Where the model's numbers come from

Every parameter in `model` was measured from this repository's history from
16 Jul to 8 Nov 2026: `git log`, 42 archived OpenSpec changes and 53 closed
issues. The evidence is laid out in #10. In brief:

| Parameter | Value | Measured from |
|---|---|---|
| `active_to_idle_beta` | Beta(3, 9) | 2 of 10 active weeks were followed by an idle one |
| `idle_to_active_beta` | Beta(3, 6) | 2 of 7 idle weeks were followed by an active one |
| `rate_mean`, `rate_sd` | 4.0, 0.6 | estimate-weeks closed per active week: 3.8 (Jul–Aug burst), 4.2 (Oct–Nov burst) |
| `weekly_sigma` | 0.4 | week-to-week variation within a burst |
| `bias_sigma` | 0.15 | the spread of tasks per estimated week (16–29) across matched issues |
| `growth_max` | 0.94 | ≈15 estimate-weeks of new roadmap issues added in 16 weeks |
| `active_window_days` | 14 | a run starts *active* if roadmap work closed in the last two weeks |

Change a parameter only with the evidence that moved it, and say so in #10. A
model re-fitted to make a date look better is the one thing that would make the
commitments meaningless.

## Running it by hand

```bash
# Print the block for the live repository without editing #10 (needs `gh` signed in)
python3 tools/scripts/planning/forecast_roadmap.py --repo dwerkjem/PracticeTakes --dry-run

# Offline, from a saved REST `issues` listing
python3 tools/scripts/planning/forecast_roadmap.py --issues-json issues.json --today 2026-12-31

# What the milestones would change to, without changing them
python3 tools/scripts/planning/sync_milestones.py --repo dwerkjem/PracticeTakes --dry-run

# The tests
python3 tools/scripts/run_tests.py
```

From GitHub, **Actions → Roadmap forecast → Run workflow** publishes on demand
and syncs the milestones. If the workflow fails on an issue event or a push, it
files an issue titled *Roadmap
forecast: the live forecast in #10 is not updating*. The usual causes are a
ledger that no longer validates, or the two markers having been deleted from
#10's body.
