## Why

Three kinds of evidence about a build are produced today, and none of them
accumulate anywhere a question can be asked of them. Manual GUI verification
runs in a Textual TUI (`tools/scripts/manual_gui/`) that prompts about one
surface at a time while the application is driven live in front of the
reviewer — serial, requiring the reviewer present for the whole run, and never
showing the same tool at two sizes together, which is exactly what makes
problems like #113 obvious. Performance numbers come out of the Performance Lab
as one-off exports. Automated suite results live only in CI logs. So "did the
tuner's presentation regress since v0.5.6", "is launch slower on this machine
than it was in June", and "what state was this build actually in when we
shipped it" are all unanswerable without reading individual files by hand.

Separating capture from judgement fixes the first problem — capture runs
unattended, review becomes one pass over a contact sheet — and giving the three
kinds of evidence one durable store fixes the second.

## What Changes

- Add a **standalone testing-suite application** under `tools/`, separate from
  Practice Takes and able to test a build it is not part of. It drives the
  application under test through the existing test-control channel and captures
  through the existing X tooling in `tools/scripts/quality/ui-validation/`; no
  second mechanism for either.
- Add an **unattended capture pass**: every surface in the surface list, at
  every configured resolution, captured to an image with no human present. A
  surface that cannot be reached or captured is recorded as a failure of that
  surface, not a gap.
- Add a **grid review UI**: all captures for a run in one scrollable grid,
  grouped by surface with resolutions side by side, zoom to full size,
  multi-select tagging, and free-text comment on any single image.
- Add a **run database** holding machines (hardware and OS provenance), runs
  (commit, build configuration, mode), captured images with their tags and
  comments, performance measurements, and automated test results — so a
  measurement or a verdict can be compared against the same measurement or
  verdict from an earlier run on the same machine.
- **Ingest rather than re-measure**: performance measurements come from the
  Performance Lab's machine-readable export (`hardware-performance-lab`) and
  automated results from the existing suites; the testing suite is their store
  and comparison surface, not a second measurement path.
- Keep the release gate working unchanged: a completed run still exports the
  JSON and Markdown record under `docs/development/quality/manual-runs/` that
  `tools/scripts/release/check_manual_verification.py` reads, and historical TUI
  records stay parseable.
- **BREAKING**: retire the Textual TUI harness. `uv run ui-test` and
  `tools/scripts/manual_gui/app.py` are removed, along with the `textual`
  dependency group. The testable core — the surface list, the driver, the run
  record, and the session sequencing — is carried across; the new UI is the thin
  layer over it, as the TUI's split intended.

## Capabilities

### New Capabilities

- `verification-run-store`: the durable database behind a verification run —
  what it holds (machines, runs, captures, tags, comments, performance
  measurements, automated test results), how provenance makes a measurement
  comparable, how the schema is migrated, and how a run is exported to the
  record the release gate reads.
- `ui-capture-and-review`: the two-phase workflow — an unattended capture pass
  that renders every surface at every configured resolution, and an attended
  grid review with zoom, multi-select tagging, and per-image comments.
- `test-suite-hub`: the front door — every automated suite the project has,
  listed in one place, runnable individually or all at once, building what each
  needs first and recording every result against the same run.

### Modified Capabilities

- `manual-gui-verification`: verification is no longer a terminal UI that
  prompts while the application runs in front of the tester. The requirements
  that mandate an interactive terminal harness, live advancement through
  surfaces, and per-question prompting are replaced by capture-then-review. The
  three fixed axes, the full/quick modes, the geometry sweep, the honesty rules
  about incomplete runs and unwaived failures, and every release-gate
  requirement survive unchanged.

## Impact

- **New code** under `tools/scripts/` (no new top-level entry): the store, the
  capture pass, the review server and its browser UI, and the run exporter.
- **Removed code**: `tools/scripts/manual_gui/app.py`, the `ui-test` console
  script, and the `manual-gui` dependency group in `pyproject.toml`.
- **Reused as-is**: `tools/scripts/quality/ui-validation/xwindow_capture.cpp`,
  `window_control.cpp`, and `pointer_control.cpp`; the test-control channel and
  its approved window states in `src/application/testcontrol/`.
- **Unchanged contract**: the on-disk run record read by
  `tools/scripts/release/check_manual_verification.py` and by the release
  workflow. The database is additive — it never becomes the gate's input.
- **Dependencies**: drops `textual`; adds an image dependency for PPM-to-PNG
  conversion and thumbnailing, scoped to a dependency group so the
  standard-library-only commit gate is unaffected.
- **Test coverage**: the store, the capture plan, the tag and comment
  operations, and the exporter are pure logic covered by
  `python tools/scripts/run_tests.py`; only the browser layer is uncovered.
- **Documentation**: `docs/development/quality/` gains the workflow document;
  historical runs under `docs/development/quality/manual-runs/` are untouched.
