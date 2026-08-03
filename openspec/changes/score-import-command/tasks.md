Settled inputs, from `design.md` § Decisions:

| | |
|---|---|
| Location | `src/application/shell/ui/score/`, beside `feedback`, `settings`, `workspace` |
| Threading | `juce::Thread` subclass + `juce::MessageManager::callAsync`, following `FeedbackComponent` |
| File chooser | `juce::FileChooser::launchAsync` + `Component::SafePointer`, following `MainComponentSettings::importSettings` |
| Ownership | `std::shared_ptr<const Score>` member on `MainComponent`, until #39 |
| Testability | Presentation logic split out of the `Component` so it is unit testable without a display |

## 1. Presentation logic, with no UI and no threading

The whole point of doing this first: it is the only part of the change that a
unit test can reach, so it is where the behaviour worth asserting should live.

- [ ] 1.1 Add the summary type that a `MusicXmlImportResult` converts into:
      titles, composer, encoding software, per-part name and staff count,
      measure count, total length, and starting tempo. No JUCE `Component`
      dependency, so it compiles against the test target's module set.
- [ ] 1.2 Convert total length into both bars and seconds. Bars because it is
      what a musician counts; seconds because it is the one number that proves
      the tempo map was read rather than defaulted.
- [ ] 1.3 Omit absent metadata rather than rendering empty labels. A score with
      no composer should not show "Composer:" followed by nothing.
- [ ] 1.4 Group diagnostics by severity, preserving the importer's order within
      each group, and carry each one's occurrence count and musical location.
- [ ] 1.5 Produce an explicit "nothing was dropped or repaired" result for a
      clean import. An empty list and a failure to report are indistinguishable
      on screen, which would make the `imported` versus `importedWithDiagnostics`
      distinction worthless.
- [ ] 1.6 Map every `MusicXmlImportStatus` to the text shown for it, using the
      importer's own error message rather than paraphrasing it. Collapsing the
      statuses would discard the work the previous change did to make failures
      specific.
- [ ] 1.7 Add `src/tests/application/shell/ui/score/ScoreImportSummaryTests.cpp`
      covering: a multi-part score, a score with no metadata, bars-and-seconds
      at a non-default tempo and across a mid-score tempo change, diagnostic
      grouping, a repeated diagnostic's count, a clean import, and **every**
      failure status.
- [ ] 1.8 Add the new sources to `target_sources(PracticeTakes ...)` and to
      `add_executable(PracticeTakesTests ...)` in `CMakeLists.txt`, keeping the
      lists alphabetically grouped as they already are.

## 2. The background import job

- [ ] 2.1 Add the import job as a `juce::Thread` subclass whose `run()` calls
      `importMusicXmlFile` and whose result is delivered with
      `juce::MessageManager::callAsync`. Follow `FeedbackComponent` deliberately
      and say so in the comment, so the shell ends up with two consistent
      examples of a background job rather than two different ones.
- [ ] 2.2 Capture a `juce::Component::SafePointer` in the delivery lambda, so a
      finished import cannot call into a destroyed component.
- [ ] 2.3 Call `signalThreadShouldExit()` and `stopThread(timeout)` in the
      destructor. **This is the failure mode most likely to survive review and
      reappear as an intermittent crash on quit**, so it gets its own task
      rather than being folded into 2.1.
- [ ] 2.4 Refuse to start a second import while one is running, and tell the
      user which file is being read rather than appearing to ignore the command.
- [ ] 2.5 Add the `std::shared_ptr<const Score>` current-score member to
      `MainComponent`, empty until the first success, handed out by value so a
      reader's score survives a later import replacing it.
- [ ] 2.6 Leave the current score untouched when an import fails. A failed
      import must not close the score a user already has open.

## 3. The menu and the file chooser

- [ ] 3.1 Add the application's first File menu, following the `juce::PopupMenu`
      idiom in `MainComponentWorkspaceMenu.cpp`. One item — see `design.md`
      § Open Questions before adding a second.
- [ ] 3.2 Add the Open Score command, launching a `juce::FileChooser`
      asynchronously with a `Component::SafePointer`, following
      `MainComponentSettings::importSettings`.
- [ ] 3.3 Offer `.musicxml`, `.xml`, and `.mxl` together rather than forcing a
      choice between them — the importer decides what a file is by its content,
      and the chooser should not be stricter than the importer.
- [ ] 3.4 Do nothing at all when the chooser is dismissed, leaving any current
      score in place.

## 4. The summary window

- [ ] 4.1 Add the window, non-modal and re-openable, alongside the existing
      settings and feedback windows. It renders what section 1 produced and
      contains no musical drawing of any kind — a summary that grows a staff
      preview undoes the reason `musicxml-import` landed the model before the
      renderer.
- [ ] 4.2 Render the summary for a success and the failure text for a failure in
      the same window, per the decision in `design.md` § Open Questions: the
      diagnostics on a successful import matter as much as the error on a failed
      one, and splitting them puts the two halves of "what happened to my file"
      in different places.
- [ ] 4.3 Make the diagnostics list scrollable rather than truncated. A
      truncated list that does not say it is truncated is worse than a long one.
- [ ] 4.4 Show that an import is in progress, so a large file does not look like
      a command that did nothing.
- [ ] 4.5 Apply the application `LookAndFeel` and confirm the window reads
      correctly in both themes, following the existing settings and feedback
      windows.

## 5. Verification

- [ ] 5.1 Confirm the user interface stays responsive while a large score
      imports, by observation rather than by assertion — this is the property
      the whole background-thread design exists for and nothing has ever
      exercised it.
- [ ] 5.2 Confirm that quitting the application mid-import neither crashes nor
      hangs.
- [ ] 5.3 Open one file of each failure status and confirm the message
      distinguishes them, rather than all nine reading alike.
- [ ] 5.4 Open a score containing unsupported content and confirm the
      diagnostics say what was dropped and where.
- [ ] 5.5 Add the window to the manual GUI verification harness, since it is
      `Component` code outside `PracticeTakesTests`.
- [ ] 5.6 Run `python3 tools/scripts/run_tests.py` and `PracticeTakesTests`, and
      confirm both pass.
- [ ] 5.7 Run `clang-format` and `clang-tidy` via pre-commit and confirm the new
      sources are clean. Note that pre-commit resolves `clang-format` from
      `PATH`, which may not be the pinned 18.1.8 that CI uses; set
      `CLANG_FORMAT` or run `uv sync --extra coverage` first.
- [ ] 5.8 Re-read this change's spec deltas against the implemented behaviour and
      correct any requirement the implementation had to deviate from, recording
      the deviation rather than quietly editing the spec to match.

## 6. Follow-through

- [ ] 6.1 Update `docs/development/architecture/ARCHITECTURE.md` § Score model
      to say who owns the current score and how it is loaded, now that something
      does.
- [ ] 6.2 Note in `docs/development/formats/musicxml-subset.md` that the
      diagnostics it describes are now shown to users, so the document's
      audience is no longer only contributors.
- [ ] 6.3 Add a follow-up note to #32 and #33 that the shell now owns a current
      score they can read, and to #39 that it will take over that ownership.
- [ ] 6.4 Answer, or re-record as still open, the four questions in `design.md`
      § Open Questions.
