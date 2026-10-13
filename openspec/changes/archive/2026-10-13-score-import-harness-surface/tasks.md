Settled inputs, from `design.md` § Decisions:

| | |
|---|---|
| Vocabulary | A new field on `ApprovedWindowState` naming a committed fixture — never a run-time path |
| Field position | **Appended** after the existing fields; the list is initialised positionally |
| Fixtures | Committed under `src/tests/resources/`, not the generated `build/` corpus |
| Import path | The real `ScoreImportJob`, with the surface declaring `warmup_seconds` |
| Capture | Each surface names the summary window, and is captured once rather than per resolution |

## 1. The fixtures

Committed first, because everything else names them and a state pointing at a
file that does not exist is the failure mode task 4.3 exists to catch.

- [x] 1.1 Add a clean-import fixture under `src/tests/resources/musicxml/`,
      small enough that its report is short and stable in a reference image.
      It must exercise the metadata the summary shows — title, composer,
      encoding software, more than one part, and a part with two staves — so
      the picture covers the fields rather than just the headline.
- [x] 1.2 Add a diagnostic fixture producing diagnostics of more than one
      severity, including **several unrecognised elements that share the
      generic message**. That group is the regression picture: it is what
      rendered as seven identical rows before the element name was printed.
- [x] 1.3 Resolve `design.md` § Open Questions 1 — a real MuseScore export or a
      hand-written document — and record the answer rather than leaving the
      fixture's provenance implicit. Note that `src/tests/resources/musicxml/README.md`
      and the corpus's provenance rules already constrain this: a real export
      needs acceptable provenance, which is why the percussion fixture is
      hand-written.
- [x] 1.4 Confirm both fixtures import to the status the surfaces expect, via
      the existing corpus test rather than by eye.

## 2. The approved states

- [x] 2.1 Add the score-fixture field to `ApprovedWindowState` in
      `ApprovedWindowStates.h`, **appended after `toolView`**, with a comment
      saying why it is appended. The header already warns that a field inserted
      mid-list silently shifts every value after it in all forty entries; this
      is the task most likely to be done wrong in a hurry.
- [x] 2.2 Add the two approved states to `ApprovedWindowStates.cpp` — one
      clean, one with diagnostics — each with a `description` written for a
      tester rather than for a developer.
- [x] 2.3 Extend the existing approved-state test so a state naming an absent
      fixture fails and names both the state and the missing file. Per
      `design.md` decision 5, this must be a test failure, never a blank
      capture a reviewer could approve.
- [x] 2.4 Confirm `list-states` reports both new states, so the harness's
      pre-run check against it can see them. (It does; checked against the
      built application, not by reading the list.)

## 3. Establishing the state

- [x] 3.1 In `MainComponentTestControl.cpp`, start the import through the same
      `ScoreImportJob` path the File menu uses when the state names a fixture.
      Not a synchronous import: `design.md` decision 3 rejects it because the
      surface would then verify a load path no user takes.
- [x] 3.2 Return `false` when the state names a fixture that cannot be resolved
      at run time, so a broken state is a recorded capture failure rather than a
      window photographed empty.
- [x] 3.3 Leave every state that names no fixture behaving exactly as before.
      The field defaults to empty and forty existing states must be untouched —
      assert this rather than assuming it.
- [x] 3.4 Confirm the message thread stays responsive while the state's import
      runs, which is the same property task 5.1 of `score-import-command`
      verified by hand and the reason the real path is used here. Measured
      through the channel: `open-state score-import-diagnostics` was answered in
      4.8 ms, and the three commands after it in 6-24 ms, while the import ran.

## 4. The surfaces

- [x] 4.1 Add the two surfaces to `tools/scripts/testing_suite/surfaces.py`,
      each naming the summary window as the window to capture. Without that the
      pass photographs the main window — which has happened before, when the
      settings surface came back byte-identical to the empty shell.
- [x] 4.2 Give each surface a `warmup_seconds` long enough for the import to
      finish on a loaded machine, and say in a comment why the field is needed
      here, since every other surface using it is waiting on a tool's drawing
      history rather than on a document.
- [x] 4.3 Capture each once rather than across the resolution sweep, following
      the settings and feedback surfaces: the geometry command resizes the main
      window, so sweeping would produce the same image under several labels.
- [x] 4.4 Add surface-specific questions worth asking of a picture — that every
      diagnostic names its element, that a group sharing one message is still
      legible as distinct rows, and that the clean import says so explicitly
      rather than showing an empty list.
- [x] 4.5 Resolve `design.md` § Open Question 2 — whether the clean-import
      surface earns its place alongside the diagnostic one — and drop it rather
      than keep it if the answer is no. The suite's own rule is that a question
      kept "just in case" is a tax on every future run.
- [x] 4.6 Run the suite's surface validation, which enforces that quick mode is
      a strict subset of full and that every state named exists.

## 5. Verification

- [x] 5.1 Capture both surfaces in both palettes and confirm the images are of
      the finished report, not of an import in progress. This is the risk
      `design.md` names as load-bearing for these two surfaces alone.
- [x] 5.2 Re-capture to confirm the images are stable run to run. A reference
      image that differs between two runs on one machine is not a reference.
- [x] 5.3 Confirm the diagnostic surface's capture would have shown the
      seven-identical-rows defect, by reverting the element-name fix locally and
      looking at the picture. The surface is only worth its cost if it catches
      the bug that motivated it.
- [x] 5.4 Run `PracticeTakesTests` and `python tools/scripts/run_tests.py`.
- [x] 5.5 Run `clang-format` and `clang-tidy` on the changed C++ locally, since
      `clang-tidy` runs only in CI and is the usual red build.

## 6. Follow-through

- [x] 6.1 Tick task 5.5 of `score-import-command` and remove the note recording
      why it was blocked, since this change is what unblocks it.
- [x] 6.2 Archive `score-import-command`, which has been waiting on 5.5 alone.
      Archived as `2026-10-13-score-import-command`; its spec deltas were applied
      to `openspec/specs/score-import-command/spec.md`.
- [x] 6.3 Update § 5.5 of
      `docs/development/quality/score-import-manual-verification.md`: it
      currently describes two options and an open decision, and should describe
      what was built and what remains manual.
- [x] 6.4 Re-record or answer `design.md` § Open Question 3 — whether a failed
      import deserves a surface of its own — rather than leaving it open with no
      verdict.

## Notes from the implementation

- **The fixtures are in `src/tests/resources/harness-scores/`, not
  `src/tests/resources/musicxml/`** as task 1.1 said. That directory is the
  real-score corpus: its test iterates `expectations.txt` and warns about every
  file on disk the manifest does not name, so putting two hand-written fixtures
  there meant either two permanent warnings in every test run or pinning them
  in a manifest whose README describes it as real, licence-checked exporter
  output. Its own directory with its own README was cheaper than either.
- `ApprovedWindowStates.h` gained `approvedScoreFixtureDirectory()` so the
  shell and the test resolve a fixture through one repository-root-relative
  path rather than two expressions that could drift. That needed
  `PRACTICE_TAKES_SOURCE_DIR` on the test target, which the application target
  already had.
- `ApprovedScoreFixturesTests.cpp` also carries a hidden
  `[.harness-score-report]` case, following `[.corpus-report]`: it prints the
  exact text each capture should contain, so "what should the reference image
  say" is answerable without building the application or taking a screenshot.

## What verification found

Two defects, both in code this change did not write and both found by the
capture it added -- which is the argument for the surfaces in one paragraph.

- **The summary window did not follow a theme change.**
  `applyAppearanceToOpenWindows` reached the settings and feedback windows and
  not this one, so in the light palette the window chrome stayed dark grey.
  Worse, a `TextEditor` colours each run of text as it is inserted: a report
  written before a theme change kept the palette it was written under, which in
  the dark theme is dark grey on near-black and unreadable. The import is
  asynchronous, so whether the report or the theme arrived first was a race, and
  two captures of the same surface disagreed. Fixed by
  `ScoreImportWindow::applyAppearance`, which the shell now calls on a theme
  change and on the window opening.
- **The feedback invitation was photographed over surfaces.** Closing a tool is
  what records a successful use, establishing a state is what closes every open
  tool, and crossing the threshold raises an `AlertWindow` -- which landed over
  the capture as a black rectangle, because an obscured window has no pixels to
  photograph. This was never specific to the score surfaces; it could have
  landed on any of them. The application now suppresses the invitation for a
  process the harness is driving (`beginTestControlSession`), and a state
  cancels any modal left on screen.

Neither is in `openspec/changes/score-import-harness-surface/proposal.md` § Impact,
which said "no change to the audio thread, to tool ownership, or to the importer
itself" -- all still true, and both fixes are in the shell.

