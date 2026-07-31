## 0. Decisions before the affected steps

- [ ] 0.1 Resolve the four items in `design.md` § "Open Questions": where manual
      run records live, whether coverage runs per-PR or only on `main`, the
      maximum supported number of simultaneous tool consumers, and whether the
      soak test can avoid needing a real audio device.
- [ ] 0.2 Record each resolution in `design.md` under a "Resolved Questions"
      section, following the convention in
      `openspec/changes/archive/2026-07-31-close-highest-risk-test-gaps/design.md`.
      Only 0.1's first two block section 2 and section 3; the last two block
      section 5 and can be deferred until then.

## 1. Lock in the test layout

- [ ] 1.1 Add `scripts/quality/check_test_layout.py` that fails when a `.cpp`
      file sits directly at the root of `tests/`, or when a test path under
      `tests/` has no corresponding directory under `src/`.
- [ ] 1.2 Give the script an explicit allowlist of directories that are
      deliberately outside the mirror, starting with `tests/support/`, so the
      allowlist doubles as the documentation of what is exempt.
- [ ] 1.3 Add `scripts/quality/test_check_test_layout.py` covering: a conforming
      tree, a stray `.cpp` at the `tests/` root, a mirrored path with no `src/`
      counterpart, and an allowlisted directory being accepted.
- [ ] 1.4 Confirm the new tests are discovered by `python scripts/run_tests.py`,
      which finds `test_*.py` by path.
- [ ] 1.5 Wire the layout check into CI so it runs on pull requests, alongside
      the existing Python check.
- [ ] 1.6 Document the mirror rule, the `tests/support/` exemption, and the
      include-path arrangement in `docs/development/`, and link it from
      `CONTRIBUTING.md`.

## 2. C++ coverage

- [ ] 2.1 Add a `PRACTICE_TAKES_ENABLE_COVERAGE` CMake option, default off, that
      applies `--coverage` to `PracticeTakesTests` and its sources; confirm an
      ordinary configure is byte-for-byte unaffected when it is off.
- [ ] 2.2 Add a script that configures a dedicated coverage build tree, runs the
      suite, and invokes `gcovr` to emit both a human summary and a
      machine-readable report. Do not reuse the normal build tree, and disable
      `ccache` for this configuration.
- [ ] 2.3 Add the second half of the denominator: derive the list of files under
      `src/` that are absent from `add_executable(PracticeTakesTests ...)` and
      report them as *not built into the test binary*, distinct from 0% covered.
- [ ] 2.4 Add tests for the file-list derivation in 2.3, since a bug there would
      silently shrink the denominator and inflate the headline figure.
- [ ] 2.5 Verify the report names the area 9 files — `AudioInputService.cpp`,
      `FeedbackComponent.cpp`, `TunerComponent`, `SpectrogramComponent`, and the
      `MainComponent*` shell — in the not-built-in category rather than omitting
      them.

## 3. TypeScript, Python, and CI publication

- [ ] 3.1 Add a coverage script to `services/` that runs Vitest with coverage
      across every workspace and emits a machine-readable report.
- [ ] 3.2 Add a Python coverage command producing a machine-readable report for
      files under `scripts/`.
- [ ] 3.3 Publish all three reports as CI artifacts and write the headline
      figure for each language to the workflow step summary.
- [ ] 3.4 State explicitly in the workflow that coverage is informational and
      that no threshold gates the build, so its absence is not read as an
      oversight.
- [ ] 3.5 Confirm a pull request that sharply reduces coverage still passes CI.
- [ ] 3.6 Record the measured baseline for all three languages in
      `docs/development/QA_STRATEGY.md` with the date and commit, and replace
      the hand-counted estimates in areas 8 and 9 with the measured figures.

## 4. Manual GUI verification

- [ ] 4.1 Write `docs/development/manual-gui-checklist.md` covering, at minimum:
      microphone device selection and switching, global mute and gain, each
      analysis tool opening and showing live input, moving a tool between
      docked/floating/tabbed presentation, workspace layout surviving a restart,
      and the settings import/export round trip.
- [ ] 4.2 Give every item a stated reason why it is not automated, so the
      checklist shrinks as the smoke and unit suites grow.
- [ ] 4.3 State the cadence — at minimum before a release — and mark which items
      are required every run versus only when a related area changed.
- [ ] 4.4 Define the run-record format and location (per decision 0.1), covering
      date, commit, platform, audio device, and per-item outcome.
- [ ] 4.5 Run the checklist once end to end against a real device and display,
      and commit the first record, so the format is proven rather than assumed.
- [ ] 4.6 Note in the checklist that any item covered by a later automated test
      must be removed in the same change that adds the test.

## 5. End-to-end smoke tests

- [ ] 5.1 Add a CI job that installs Xvfb and runs the built application under a
      virtual display on Linux.
- [ ] 5.2 Build the existing `scripts/quality/ui-validation/` X11 helpers
      (`xwindow_capture`, `window_control`) as a reusable step rather than
      duplicating them for the smoke suite.
- [ ] 5.3 Add a smoke driver under `scripts/quality/` that launches
      `build/bin/PracticeTakes`, waits for the main window with a bounded
      timeout, and exits non-zero on crash, hang, or missing window.
- [ ] 5.4 Add the tool-opening assertion: open one analysis tool and confirm it
      is present in the running application.
- [ ] 5.5 Add the shutdown assertion: close the application, require exit within
      a bounded time with a success status, and terminate the process on timeout
      so a hang cannot stall the run.
- [ ] 5.6 Confirm the whole smoke suite passes on a host with no audio capture
      device, since CI runners have none.
- [ ] 5.7 Confirm the smoke suite does not run as part of the default `ctest`
      invocation.
- [ ] 5.8 Run the smoke suite ten times in CI before relying on it, and record
      the flake count; per `design.md`, a test that flakes without a real defect
      is deleted rather than retried.

## 6. Load and soak tests

- [ ] 6.1 Add a Catch2 tag for opt-in load tests, distinct from `[.benchmark]`,
      and confirm the default `ctest` run excludes it.
- [ ] 6.2 Add concurrent `AudioSampleFifo` tests: a producer and a consumer on
      separate threads, asserting every sample arrives exactly once and in
      order.
- [ ] 6.3 Add the overflow case: the producer outruns the consumer and the ring
      fills, asserting the documented overflow behaviour rather than corruption
      or blocking.
- [ ] 6.4 State in the test file that these are necessary but not sufficient
      without TSan (QA_STRATEGY area 13), so a green run is not mistaken for
      proof of correctness.
- [ ] 6.5 Add the saturation tests: the maximum supported number of simultaneous
      tool consumers each draining its own stream, and one stalled consumer
      overflowing without affecting the others.
- [ ] 6.6 Add the soak test with a configurable duration, asserting bounded
      memory and throughput within a stated tolerance; have CI run a short
      configuration to prove the harness.
- [ ] 6.7 Emit the measured figures — throughput, overflow counts, peak memory,
      duration — from every load and soak test, so a pass still shows whether
      headroom is shrinking.

## 7. Documentation and verification

- [ ] 7.1 Update `docs/development/QA_STRATEGY.md` areas 8, 9, and 13 to reflect
      what this change delivered and what it deliberately left open.
- [ ] 7.2 Add follow-up issues for the work this change explicitly defers:
      coverage thresholds, extracting logic out of the JUCE components,
      sanitizer builds, and macOS test execution.
- [ ] 7.3 Run `python scripts/run_tests.py`, the C++ suite, and
      `npm run check && npm run test` from `services/`, and confirm all pass.
- [ ] 7.4 Run `clang-format` and `clang-tidy` via pre-commit and confirm any new
      C++ sources are clean.
- [ ] 7.5 Re-read this change's spec deltas against what was implemented and
      correct any requirement the implementation had to deviate from, recording
      the deviation rather than quietly editing the spec to match.
