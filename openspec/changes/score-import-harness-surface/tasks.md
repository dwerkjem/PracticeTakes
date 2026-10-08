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

- [ ] 1.1 Add a clean-import fixture under `src/tests/resources/musicxml/`,
      small enough that its report is short and stable in a reference image.
      It must exercise the metadata the summary shows — title, composer,
      encoding software, more than one part, and a part with two staves — so
      the picture covers the fields rather than just the headline.
- [ ] 1.2 Add a diagnostic fixture producing diagnostics of more than one
      severity, including **several unrecognised elements that share the
      generic message**. That group is the regression picture: it is what
      rendered as seven identical rows before the element name was printed.
- [ ] 1.3 Resolve `design.md` § Open Questions 1 — a real MuseScore export or a
      hand-written document — and record the answer rather than leaving the
      fixture's provenance implicit. Note that `src/tests/resources/musicxml/README.md`
      and the corpus's provenance rules already constrain this: a real export
      needs acceptable provenance, which is why the percussion fixture is
      hand-written.
- [ ] 1.4 Confirm both fixtures import to the status the surfaces expect, via
      the existing corpus test rather than by eye.

## 2. The approved states

- [ ] 2.1 Add the score-fixture field to `ApprovedWindowState` in
      `ApprovedWindowStates.h`, **appended after `toolView`**, with a comment
      saying why it is appended. The header already warns that a field inserted
      mid-list silently shifts every value after it in all forty entries; this
      is the task most likely to be done wrong in a hurry.
- [ ] 2.2 Add the two approved states to `ApprovedWindowStates.cpp` — one
      clean, one with diagnostics — each with a `description` written for a
      tester rather than for a developer.
- [ ] 2.3 Extend the existing approved-state test so a state naming an absent
      fixture fails and names both the state and the missing file. Per
      `design.md` decision 5, this must be a test failure, never a blank
      capture a reviewer could approve.
- [ ] 2.4 Confirm `list-states` reports both new states, so the harness's
      pre-run check against it can see them.

## 3. Establishing the state

- [ ] 3.1 In `MainComponentTestControl.cpp`, start the import through the same
      `ScoreImportJob` path the File menu uses when the state names a fixture.
      Not a synchronous import: `design.md` decision 3 rejects it because the
      surface would then verify a load path no user takes.
- [ ] 3.2 Return `false` when the state names a fixture that cannot be resolved
      at run time, so a broken state is a recorded capture failure rather than a
      window photographed empty.
- [ ] 3.3 Leave every state that names no fixture behaving exactly as before.
      The field defaults to empty and forty existing states must be untouched —
      assert this rather than assuming it.
- [ ] 3.4 Confirm the message thread stays responsive while the state's import
      runs, which is the same property task 5.1 of `score-import-command`
      verified by hand and the reason the real path is used here.

## 4. The surfaces

- [ ] 4.1 Add the two surfaces to `tools/scripts/testing_suite/surfaces.py`,
      each naming the summary window as the window to capture. Without that the
      pass photographs the main window — which has happened before, when the
      settings surface came back byte-identical to the empty shell.
- [ ] 4.2 Give each surface a `warmup_seconds` long enough for the import to
      finish on a loaded machine, and say in a comment why the field is needed
      here, since every other surface using it is waiting on a tool's drawing
      history rather than on a document.
- [ ] 4.3 Capture each once rather than across the resolution sweep, following
      the settings and feedback surfaces: the geometry command resizes the main
      window, so sweeping would produce the same image under several labels.
- [ ] 4.4 Add surface-specific questions worth asking of a picture — that every
      diagnostic names its element, that a group sharing one message is still
      legible as distinct rows, and that the clean import says so explicitly
      rather than showing an empty list.
- [ ] 4.5 Resolve `design.md` § Open Question 2 — whether the clean-import
      surface earns its place alongside the diagnostic one — and drop it rather
      than keep it if the answer is no. The suite's own rule is that a question
      kept "just in case" is a tax on every future run.
- [ ] 4.6 Run the suite's surface validation, which enforces that quick mode is
      a strict subset of full and that every state named exists.

## 5. Verification

- [ ] 5.1 Capture both surfaces in both palettes and confirm the images are of
      the finished report, not of an import in progress. This is the risk
      `design.md` names as load-bearing for these two surfaces alone.
- [ ] 5.2 Re-capture to confirm the images are stable run to run. A reference
      image that differs between two runs on one machine is not a reference.
- [ ] 5.3 Confirm the diagnostic surface's capture would have shown the
      seven-identical-rows defect, by reverting the element-name fix locally and
      looking at the picture. The surface is only worth its cost if it catches
      the bug that motivated it.
- [ ] 5.4 Run `PracticeTakesTests` and `python tools/scripts/run_tests.py`.
- [ ] 5.5 Run `clang-format` and `clang-tidy` on the changed C++ locally, since
      `clang-tidy` runs only in CI and is the usual red build.

## 6. Follow-through

- [ ] 6.1 Tick task 5.5 of `score-import-command` and remove the note recording
      why it was blocked, since this change is what unblocks it.
- [ ] 6.2 Archive `score-import-command`, which has been waiting on 5.5 alone.
- [ ] 6.3 Update § 5.5 of
      `docs/development/quality/score-import-manual-verification.md`: it
      currently describes two options and an open decision, and should describe
      what was built and what remains manual.
- [ ] 6.4 Re-record or answer `design.md` § Open Question 3 — whether a failed
      import deserves a surface of its own — rather than leaving it open with no
      verdict.
