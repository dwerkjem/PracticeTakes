Settled inputs, from `design.md` § Decisions:

| | |
|---|---|
| Seam | `const score::CurrentScore&` in `ToolServices`, polled by the tool on its own timer |
| Change detection | Pointer identity against the `shared_ptr` the tool last rendered — no generation counter |
| Holder ownership | `ScoreImportState` owns the `CurrentScore`; #39 takes it over |
| Threading | Message thread only, by documented constraint rather than by lock |
| Target rule | First pitched, non-grace event of the first part; earliest onset across voices, tie-broken by lower voice number; sounding pitch |
| Tool location | `src/features/score/`, flat like `src/features/feedback/` |
| Testability | Selection logic split out of the `Component`, following `ScoreImportSummary` |

## 1. The holder, and the name the model could not render

JUCE-free, no consumer yet, so the tree keeps building at every step.

- [x] 1.1 Add `src/platform/score/CurrentScore.h`: publish a
      `std::shared_ptr<const Score>`, read one back by value, and report whether
      one is present. Header comment states the message-thread-only constraint
      and *why* there is no mutex — a lock would imply a cross-thread use the
      model's design forbids.
- [x] 1.2 Document in that header why change detection needs no generation
      counter: a reader holding its copy keeps the old score alive, so two
      distinct scores are always two distinct pointers.
- [x] 1.3 Add `src/platform/score/PitchNames.h` with
      `score::spelledName(const Pitch&)`. Not in `Pitch.h`, which is
      `constexpr`-only and includes no `<string>`.
- [x] 1.4 Spell accidentals in ASCII (`C#4`, `Bbb3`), matching what the tuner
      already shows, and render accidentals beyond a double rather than clamping
      them.
- [x] 1.5 Add `src/tests/platform/score/CurrentScoreTests.cpp`: absent before
      the first publish, present after, hands back the same score, a second
      publish replaces it, and a score a reader still holds stays alive and
      unchanged after it is replaced.
- [x] 1.6 Add `src/tests/platform/score/PitchNamesTests.cpp`: naturals, sharps,
      flats, double accidentals, enharmonic spellings naming differently, and
      the octave boundary at C.
- [x] 1.7 Add both sources to `PRACTICE_TAKES_TEST_SOURCES` in
      `tools/cmake/Sources.cmake`.

## 2. Choosing and describing the target note

The only part of the tool a unit test can reach, so it is where every rule worth
asserting lives.

- [x] 2.1 Add `src/features/score/TargetNoteSelection.h`/`.cpp`: turn a
      `std::shared_ptr<const Score>` into a described target, or into "no score
      open", or into "this score has no pitched note". Three outcomes, not a
      bool and an out-parameter — the two empty cases say different things to a
      user.
- [x] 2.2 Implement the target rule: first part, measures in order, and within a
      measure the *earliest onset across every voice*, tie-broken by the lower
      voice number. Planned as "lowest voice index first", which is wrong: a
      measure's voices are independent timelines, so a piano part whose bass
      enters on beat one and whose treble rests until beat three would have
      reported the treble note as the score's first. Rests and grace notes are
      never targets.
- [x] 2.3 Describe the note from its **sounding** pitch, and carry the written
      spelling separately when the two differ, per `Note`'s own statement of
      which is for what.
- [x] 2.4 Report that the target is one note of a chord when its event carries
      more than one note, rather than presenting it as a single note.
- [x] 2.5 Carry the part name, staff number, voice number, and the measure's
      **printed** number — a string, because a pickup is "0" and a split bar is
      "12a".
- [x] 2.6 Convert the note's absolute onset (measure start plus measure-relative
      onset) and its end to seconds through the score's own `TempoMap`, and
      derive the duration from the two. Not from the tick duration at the
      starting tempo, which is wrong across a tempo change inside the note.
- [x] 2.7 Title fallback: work title, else movement title, else the first part's
      name, else a stated "untitled score". A file that names nothing still has
      to be identifiable.
- [x] 2.8 Add `src/tests/features/score/TargetNoteSelectionTests.cpp` over
      hand-built scores: a leading note, leading rests, a leading grace note, a
      leading chord, voice ordering, a transposing part, a printed measure
      number that is not an index, a score with no pitched notes, a null score,
      a score with no parts, a score with no measures, seconds at a non-default
      tempo, and seconds across a tempo change.
- [x] 2.9 Extend those tests over a **real corpus file** from
      `src/tests/resources/musicxml/`, imported through `importMusicXmlFile`, so
      what is asserted is what the importer actually produces rather than what a
      fixture assumed. Follow whatever `src/tests/platform/score/` already does
      to locate corpus files rather than inventing a second way.
- [x] 2.10 Add both sources to the app and test lists in
      `tools/cmake/Sources.cmake`.

## 3. Publishing the score into the holder

- [x] 3.1 Give `ScoreImportState` a `CurrentScore` member and have `apply`
      publish into it, keeping the rule intact: a result with no score publishes
      nothing.
- [x] 3.2 Keep `ScoreImportState::score()` and `hasScore()`, delegating to the
      holder, and expose the holder itself by `const` reference for the tool
      contract.
- [x] 3.3 Confirm `src/tests/application/shell/ui/score/ScoreImportStateTests.cpp`
      passes **unchanged**. If it needs editing, the refactor changed behaviour
      that it was written to pin, and that is a signal rather than a chore.
- [x] 3.4 Add a test that the holder a `ScoreImportState` exposes agrees with
      `score()` after a success, after a failure following a success, and before
      any import.

## 4. The tool contract

- [x] 4.1 Add `const score::CurrentScore& currentScore` to `ToolServices`,
      documented beside the existing fields with the same non-owning,
      outlives-every-tool guarantee.
- [x] 4.2 Pass it at both construction sites —
      `MainComponentWorkspacePresentation.cpp` and
      `MainComponentWorkspaceSnapshot.cpp`. Every existing factory keeps its
      signature; that it does is the claim `ToolServices`' own comment makes.
- [x] 4.3 Update the comment on `MainComponent::scoreImport` so it names the
      tool that now borrows it instead of saying nothing does, and restate why
      it must stay declared before `liveTools`.

## 5. The tool

- [x] 5.1 Add `src/features/score/TargetNoteComponent.h`/`.cpp`: a
      `ToolComponent` constructed with the `CurrentScore&`, holding the
      `shared_ptr` it last rendered.
- [x] 5.2 Poll the holder on a low-rate timer and re-render only when the
      pointer differs from the one held. A newly opened score is the least
      urgent update in the application; a redraw per tick would be waste.
- [x] 5.3 Paint the target: the spelled name large, then title, part/staff/voice,
      printed bar, sounding MIDI number, written spelling when it differs, start
      and duration in seconds. No staff, no glyph — invariant 10 means the model
      cannot supply a staff position and computing one is #32's.
- [x] 5.4 Paint the two empty states as sentences: no score open, naming the
      File ▸ Open Score command; and a score open with no pitched note in it.
- [x] 5.5 Implement `setTheme` and `resetToDefaults`. Persist nothing:
      `captureSettings` and `applySettings` keep their defaults, and the catalog
      entry declares no settings version.
- [x] 5.6 Give the tool a component id (`target-note-tool`), **and** an approved
      window state and a capture surface after all. Three tests fail without the
      state, and the tool's paint routine is `Component` code with no other
      verification, so leaving the harness out would have left the only untested
      part of the change unlooked-at. Only the *empty* state is capturable --
      see the findings document for why the populated one is not.
- [x] 5.7 Add the catalog entry to `BuiltInToolCatalog.h` and the factory to
      `BuiltInTools.cpp` — one line each, and nothing else in the shell.
- [x] 5.8 Add both sources to `target_sources(PracticeTakes ...)` via
      `tools/cmake/Sources.cmake`.

## 6. Verification

- [x] 6.1 `cmake --build build --target PracticeTakesTests` and
      `ctest --test-dir build --output-on-failure` — green, including the
      pre-existing score and shell tests.
- [x] 6.2 `python tools/scripts/quality/run_clang_format.py` over the new and
      changed files, and `run_clang_tidy.py` over the new `.cpp` files against a
      built tree.
- [x] 6.3 `python tools/scripts/quality/check_test_layout.py` — the new tests
      mirror `src/`, so nothing needs an exemption.
- [x] 6.4 Build and run the application: open a corpus score through File ▸ Open
      Score, open the Target Note tool, and confirm the note shown is the one the
      file starts on. Then open a second score and confirm the open tool follows
      it.
- [x] 6.5 Partially. Verified docked on its own and docked in the four-way
      split; floating and tabbed were not driven, because the harness has no
      state that opens *this* tool either way and reparenting is shell machinery
      the other tools' states already exercise. Recorded rather than claimed.

## 7. The findings, which are the point

- [x] 7.1 Write `docs/development/architecture/score-model-findings.md`: what
      the model got wrong, what it got right, and what the next consumer should
      expect. Each entry names the friction and what should happen about it.
- [x] 7.2 Include the findings this change already knows about: no spelled-name
      accessor in the model (fixed here); no shared reference pitch, so a
      sounding MIDI number cannot become a frequency outside the tuner; walking
      to the first pitched note is a hand-written nested loop every consumer
      will repeat; `Measure::start` plus `ScoreEvent::onset` being
      measure-relative means absolute position is a consumer-side sum; and
      whatever else the implementation meets.
- [x] 7.3 Add it to the index in `docs/development/README.md`, in the section
      the architecture documents are listed in.
- [x] 7.4 Correct `docs/development/agents/AGENT_GUIDE.md`: `src/platform/score`
      no longer "has no application consumer yet" — it has two, the import
      command and this tool — and the tool roster in the architecture summary
      gains the new tool.
- [x] 7.5 Answer `design.md`'s open questions in a section recorded after
      implementation, as the previous change did. Raising the issues under #13
      is left to the repository owner rather than done here: the findings
      document names each one and where it goes.
