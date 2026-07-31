## 0. Decisions before the affected steps

- [ ] 0.1 Resolve the remaining five items in `design.md` § "Open Questions":
      where manual run records live and in what format, which surfaces belong in
      quick mode, whether coverage runs per-PR or only on `main`, the maximum
      supported number of simultaneous tool consumers, and whether the soak test
      can avoid needing a real audio device. (The sixth — how the harness drives
      the application — is resolved; see § "Resolved Questions".)
- [ ] 0.2 Record each resolution in `design.md` under a "Resolved Questions"
      section, following the convention in
      `openspec/changes/archive/2026-07-31-close-highest-risk-test-gaps/design.md`.
- [ ] 0.3 Confirm the sequencing dependency on `adopt-uv-for-python`
      (`design.md` decision 9): either land it first, or decide how else the
      harness gets a Python dependency. Section 4 is blocked until this is
      settled, and section 5 is blocked behind section 4 because the gate reads
      the records the harness produces. Sections 1, 2, 3, 6, and 7 are
      independent and can proceed meanwhile.

## 1. Lock in the test layout

- [x] 1.1 Add `scripts/quality/check_test_layout.py` that fails when a `.cpp`
      file sits directly at the root of `tests/`, or when a test path under
      `tests/` has no corresponding directory under `src/`.
- [x] 1.2 Give the script an explicit allowlist of directories that are
      deliberately outside the mirror, starting with `tests/support/`, so the
      allowlist doubles as the documentation of what is exempt.
- [x] 1.3 Add `scripts/quality/test_check_test_layout.py` covering: a conforming
      tree, a stray `.cpp` at the `tests/` root, a mirrored path with no `src/`
      counterpart, and an allowlisted directory being accepted.
- [x] 1.4 Confirm the new tests are discovered by `python scripts/run_tests.py`,
      which finds `test_*.py` by path.
- [x] 1.5 Wire the layout check into CI so it runs on pull requests, alongside
      the existing Python check.
- [x] 1.6 Document the mirror rule, the `tests/support/` exemption, and the
      include-path arrangement in `docs/development/`, and link it from
      `CONTRIBUTING.md`.

## 2. C++ coverage

- [x] 2.1 Add a `PRACTICE_TAKES_ENABLE_COVERAGE` CMake option, default off, that
      applies `--coverage` to `PracticeTakesTests` and its sources; confirm an
      ordinary configure is byte-for-byte unaffected when it is off.
- [x] 2.2 Add a script that configures a dedicated coverage build tree, runs the
      suite, and invokes `gcovr` to emit both a human summary and a
      machine-readable report. Do not reuse the normal build tree, and disable
      `ccache` for this configuration.
- [x] 2.3 Add the second half of the denominator: derive the list of files under
      `src/` that are absent from `add_executable(PracticeTakesTests ...)` and
      report them as *not built into the test binary*, distinct from 0% covered.
- [x] 2.4 Add tests for the file-list derivation in 2.3, since a bug there would
      silently shrink the denominator and inflate the headline figure.
- [x] 2.5 Verify the report names the area 9 files — `AudioInputService.cpp`,
      `FeedbackComponent.cpp`, `TunerComponent`, `SpectrogramComponent`, and the
      `MainComponent*` shell — in the not-built-in category rather than omitting
      them.

## 3. TypeScript, Python, and CI publication

- [x] 3.1 Add a coverage script to `services/` that runs Vitest with coverage
      across every workspace and emits a machine-readable report.
- [x] 3.2 Add a Python coverage command producing a machine-readable report for
      files under `scripts/`.
- [x] 3.3 Publish all three reports as CI artifacts and write the headline
      figure for each language to the workflow step summary.
- [x] 3.4 State explicitly in the workflow that coverage is informational and
      that no threshold gates the build, so its absence is not read as an
      oversight.
- [x] 3.5 Confirm a pull request that sharply reduces coverage still passes CI.
- [x] 3.6 Record the measured baseline for all three languages in
      `docs/development/QA_STRATEGY.md` with the date and commit, and replace
      the hand-counted estimates in areas 8 and 9 with the measured figures.

## 4. Manual GUI verification harness

**Prerequisite:** `adopt-uv-for-python` must land first, or this section must
define its own Python dependency mechanism — see `design.md` decision 9. Do not
start 4.2 until that is settled.

- [x] 4.1 Decide how the harness drives the application to a surface. Resolved:
      **no input synthesis** — a development-only control channel with a closed
      vocabulary of approved window states and approved click targets, with
      clicks invoking the object's own action in process. See `design.md`
      § "Resolved Questions".
- [x] 4.1a Add the control-channel command parser as JUCE-free logic, rejecting
      unknown verbs, missing arguments, and trailing extra arguments, since a
      harness that has drifted must fail loudly rather than appear to work.
- [x] 4.1b Add the approved-state and approved-click-target registries as
      JUCE-free logic, with tests pinning uniqueness, tool-name agreement with
      `ToolType`, and every tool being reachable in at least one state.
- [x] 4.1c Set component ids on the approved click targets in the shell, and
      make a click resolve by id and invoke that object's action — no pointer
      movement, no synthesised button event.
- [x] 4.1d Add the `PRACTICE_TAKES_ENABLE_TEST_CONTROL` CMake option, default
      off, following the `PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB` precedent, and
      confirm the channel is absent from a default build.
- [x] 4.1e Wire the channel to `MainComponent`: apply an approved state via
      `openTool`, report the current state for `status`, and answer
      `list-states` / `list-objects` so the harness can verify the vocabulary
      before relying on it.
- [x] 4.1f Add the launch flag that starts the application directly in an
      approved state, so a surface can be reached without replaying a click
      sequence.
- [ ] 4.2 Add Textual as a Python dependency for the harness only, and confirm
      the three `pre-commit` scripts (`secrets_manager.py` twice,
      `run_clang_format.py`) and everything CI runs remain stdlib-only.
- [ ] 4.3 Define the declarative surface format: how to reach a surface, its
      three fixed axes, and any surface-specific extra questions. Surfaces are
      data, not code, so adding one is not a harness change.
- [ ] 4.4 Define the surface set, covering at minimum microphone device
      selection and switching, global mute and gain, each analysis tool opening
      and showing live input, moving a tool between docked/floating/tabbed
      presentation, workspace layout surviving a restart, and the settings
      import/export round trip.
- [ ] 4.5 Mark which surfaces belong to quick mode — the smallest set that
      answers "does it still work" (see `design.md` § Open Questions) — and
      confirm quick mode is a strict subset of full mode.
- [ ] 4.6 Build the TUI: present each surface's three fixed axes as
      pass/fail/skip, then its extras, allow a free-text note on any answer, and
      require one on any failure.
- [ ] 4.7 Build the driving layer per 4.1, launching the application and
      advancing between surfaces without tester intervention.
- [ ] 4.8 Record an unreachable surface as a failure of that surface with the
      reason, and continue the run, rather than skipping it silently.
- [ ] 4.9 Add the `--full` and `--quick` modes and record which was used, so a
      quick run cannot be read as a release check.
- [ ] 4.10 Add the optional window-geometry flag repeating each surface at a
      constrained size, the default, and maximised; record which geometry each
      answer applied to, and state in the record when only the default was
      covered.
- [ ] 4.11 Write the run record automatically on completion — date, commit,
      platform, audio device, mode, geometry flag, and every answer with notes —
      in the format and location settled in 0.1.
- [ ] 4.12 Preserve answers and mark the run incomplete when a run is
      interrupted, so a long full run is not lost and an incomplete run is never
      mistaken for a passing one.
- [ ] 4.13 Add tests for the parts that do not need a display: surface
      definition parsing, mode subsetting, geometry expansion, record writing,
      and incomplete-run marking.
- [ ] 4.14 Run the harness once in quick mode and once in full mode against a
      real device and display, and commit both records, so the format is proven
      rather than assumed.
- [ ] 4.15 Document that any question covered by a later automated test must be
      removed from the harness in the same change that adds the test.

## 5. Release gate on manual verification

- [ ] 5.1 Define and document the release-affecting path list — `src/`,
      `CMakeLists.txt`, `cmake/`, `packaging/`, `vcpkg.json` — and why `VERSION`
      is excluded (the dispatch path bumps it, so including it makes the gate
      unsatisfiable).
- [ ] 5.2 Add the gate script: find the most recent full-mode record, confirm it
      is complete, confirm its verified commit is an ancestor of or identical to
      the release commit, and confirm no release-affecting file differs between
      the two.
- [ ] 5.3 Make the gate block on any failed item in the record unless that item
      carries a written waiver.
- [ ] 5.4 Make every failure mode report its specific cause — missing,
      quick-only, incomplete, stale, or unwaived failure — and for staleness,
      name the verified commit and the release-affecting files that changed.
- [ ] 5.5 Add tests covering each accept and reject path, including the case
      that motivates the whole design: a record committed *after* the run it
      describes must still be accepted, because committing it changed no
      release-affecting file.
- [ ] 5.6 Add the skip flag to `release.yml`'s `workflow_dispatch` inputs,
      defaulting to off, with a required reason that fails the release if the
      flag is set and the reason is empty.
- [ ] 5.7 Record the skip and its reason with the release itself, not only in
      workflow logs, so past releases that shipped without manual verification
      are identifiable later.
- [ ] 5.8 Handle the tag-push path, which has no workflow inputs: decide and
      document its skip equivalent (a committed marker) and note the asymmetry
      with the dispatch path rather than leaving it implicit.
- [ ] 5.9 Wire the gate in as a job that runs before any artifact is built or
      published, on both release entry points.
- [ ] 5.10 Confirm an ordinary pull request does not fail for the absence of a
      manual run record.
- [ ] 5.11 Exercise the gate end to end before relying on it: one release
      attempt blocked by a stale record, one passing with a current record, and
      one skipped with a reason.

## 6. End-to-end smoke tests

- [ ] 6.1 Add a CI job that installs Xvfb and runs the built application under a
      virtual display on Linux.
- [ ] 6.2 Build the existing `scripts/quality/ui-validation/` X11 helpers
      (`xwindow_capture`, `window_control`) as a reusable step rather than
      duplicating them for the smoke suite.
- [ ] 6.3 Add a smoke driver under `scripts/quality/` that launches
      `build/bin/PracticeTakes`, waits for the main window with a bounded
      timeout, and exits non-zero on crash, hang, or missing window.
- [ ] 6.4 Add the tool-opening assertion: open one analysis tool and confirm it
      is present in the running application.
- [ ] 6.5 Add the shutdown assertion: close the application, require exit within
      a bounded time with a success status, and terminate the process on timeout
      so a hang cannot stall the run.
- [ ] 6.6 Confirm the whole smoke suite passes on a host with no audio capture
      device, since CI runners have none.
- [ ] 6.7 Confirm the smoke suite does not run as part of the default `ctest`
      invocation.
- [ ] 6.8 Run the smoke suite ten times in CI before relying on it, and record
      the flake count; per `design.md`, a test that flakes without a real defect
      is deleted rather than retried.

## 7. Load and soak tests

- [ ] 7.1 Add a Catch2 tag for opt-in load tests, distinct from `[.benchmark]`,
      and confirm the default `ctest` run excludes it.
- [ ] 7.2 Add concurrent `AudioSampleFifo` tests: a producer and a consumer on
      separate threads, asserting every sample arrives exactly once and in
      order.
- [ ] 7.3 Add the overflow case: the producer outruns the consumer and the ring
      fills, asserting the documented overflow behaviour rather than corruption
      or blocking.
- [ ] 7.4 State in the test file that these are necessary but not sufficient
      without TSan (QA_STRATEGY area 13), so a green run is not mistaken for
      proof of correctness.
- [ ] 7.5 Add the saturation tests: the maximum supported number of simultaneous
      tool consumers each draining its own stream, and one stalled consumer
      overflowing without affecting the others.
- [ ] 7.6 Add the soak test with a configurable duration, asserting bounded
      memory and throughput within a stated tolerance; have CI run a short
      configuration to prove the harness.
- [ ] 7.7 Emit the measured figures — throughput, overflow counts, peak memory,
      duration — from every load and soak test, so a pass still shows whether
      headroom is shrinking.

## 8. Documentation and verification

- [ ] 8.1 Update `docs/development/QA_STRATEGY.md` areas 8, 9, and 13 to reflect
      what this change delivered and what it deliberately left open.
- [ ] 8.2 Add follow-up issues for the work this change explicitly defers:
      coverage thresholds, extracting logic out of the JUCE components,
      sanitizer builds, and macOS test execution.
- [ ] 8.3 Run `python scripts/run_tests.py`, the C++ suite, and
      `npm run check && npm run test` from `services/`, and confirm all pass.
- [ ] 8.4 Run `clang-format` and `clang-tidy` via pre-commit and confirm any new
      C++ sources are clean.
- [ ] 8.5 Re-read this change's spec deltas against what was implemented and
      correct any requirement the implementation had to deviate from, recording
      the deviation rather than quietly editing the spec to match.
