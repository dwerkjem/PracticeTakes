## Why

The score import summary window is `Component` code, so `PracticeTakesTests`
cannot reach it, and the manual GUI harness — the project's only verification
for `Component` code — cannot reach it either. A harness surface names an
approved window state, and no approved state can open this window: reaching it
means importing a file, and a file path is a concept the test-control channel's
deliberately closed vocabulary does not have.

That gap is not hypothetical. A real MuseScore 4.7.3 export rendered seven
identical rows of "The importer does not read this element, so it was ignored",
naming nothing a user could act on. The bug was found by a person opening a file
and reading the window, and it was fixable with a unit test only because it sat
in `ScoreImportSummary` — the pure layer. The code that puts that string on
screen, `ScoreImportWindow`, is still covered by nothing at all: wire it to the
wrong field and all 527 tests still pass.

This closes task 5.5 of `score-import-command`, the one task in that change
that needs a decision rather than a tester, and the one blocking its archive.

## What Changes

- `ApprovedWindowState` gains a field naming a score to import on startup,
  appended after the existing fields because the state list is initialised
  positionally. A state that names one opens the summary window on the result,
  exactly as `settingsOpen` and `feedbackOpen` open theirs.
- Two approved states are added: one on a score that imports cleanly, one on a
  score whose import produces diagnostics. The second is the one that would
  have caught the defect above.
- Those states name fixtures committed under `src/tests/resources/`, so a
  capture is byte-stable across machines and runs. The generated
  manual-verification corpus is deliberately **not** reused: it lives in
  `build/`, and a surface whose reference image depends on an untracked file is
  not a reference image.
- The testing suite gains the matching surfaces in `surfaces.py`, with the
  summary window named as the window to capture — as the settings and feedback
  surfaces already do, and without which the pass photographs the main window.
- The full-run surface list in the manual verification capability grows to
  include the summary window.

Not in scope: a general `open-score <path>` command accepting an arbitrary path
at runtime. The vocabulary stays closed — a state names a committed fixture, and
the harness still cannot ask the application to open a file of its choosing.
That is what keeps "the set of surfaces under verification is a reviewable list
in one file" true.

## Capabilities

### New Capabilities

None. This extends two existing capabilities rather than introducing a concept
of its own: the ability to reach a document-dependent surface is a property of
the existing capture mechanism, and the window being verified is an addition to
the existing manual run.

### Modified Capabilities

- `ui-capture-and-review`: the approved-state vocabulary gains the ability to
  name a score for a state to import, so a surface that only exists after a
  document is loaded can be reached without synthesising input or widening the
  channel to arbitrary paths.
- `manual-gui-verification`: a full run's minimum surface list grows to include
  the score import summary window, in both its clean and its diagnostic form.

## Impact

- `src/application/testcontrol/ApprovedWindowStates.{h,cpp}` — one new field and
  two new entries. The field must be appended: every entry is initialised
  positionally, and the file already warns that a field inserted mid-list
  silently shifts every value after it in all forty of them.
- `src/application/shell/` — the glue that establishes a state gains the import,
  which must run through the existing `ScoreImportJob` rather than importing
  synchronously, or the state's own establishment would block the message
  thread the harness is driving.
- `src/tests/resources/` — two small committed fixtures.
- `src/tests/application/testcontrol/` — the existing test that every approved
  state resolves gains the new field; a state naming a missing fixture must fail
  loudly rather than capture an empty window.
- `tools/scripts/testing_suite/surfaces.py` — two surfaces.
- `openspec/changes/score-import-command/tasks.md` — task 5.5 becomes
  completable, and that change becomes archivable.
- No change to the audio thread, to tool ownership, or to the importer itself.
