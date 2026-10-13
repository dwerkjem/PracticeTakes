## 1. Model additions the renderer needs

These are findings 3, 4 and 6 of
`docs/development/architecture/score-model-findings.md`. They come first because
every later group uses them, and they are the parts of this change that are pure
JUCE-free logic with no font and no window.

- [ ] 1.1 Add `score::absoluteOnset(const Measure&, const ScoreEvent&)` to
      `src/platform/score/`, with a comment saying why the sum is not left to
      the caller (finding 4: correct in bar 1, silently wrong after).
- [ ] 1.2 Add event traversal to `score::` — visiting a part's events in
      ascending absolute onset, across measures and across the voices within a
      measure, yielding each event with its measure, voice, and absolute onset.
- [ ] 1.3 State the cross-voice tie-break rule in the traversal's header and
      make it repeatable (the Target Note tool already picks the lower
      `Voice::number`; match it rather than inventing a second rule).
- [ ] 1.4 Document the chord note ordering in `ScoreEvent.h` as **source
      order**, citing the importer's `isChordTone` branch, and warn against
      assuming `notes.front()` is lowest.
- [ ] 1.5 Tests in `src/tests/platform/score/` for 1.1–1.4, including a corpus
      test that pins chord source order against a real `.mxl` file whose chord
      is not written ascending.
- [ ] 1.6 Add the new sources and tests to `tools/cmake/Sources.cmake`, and
      update `docs/development/architecture/score-model-findings.md` to mark
      findings 3, 4 and 6 done.

## 2. Glyph metrics and the font asset

- [ ] 2.1 Fetch Bravura (OFL-1.1) and `bravura_metadata.json` from
      `steinbergmedia/bravura`, commit the font under `tools/packaging/fonts/`,
      and record its licence and provenance in that directory's `README.md`
      alongside the IBM Plex entry.
- [ ] 2.2 Add Bravura to the `juce_add_binary_data(PracticeTakesFonts ...)`
      target in `CMakeLists.txt`.
- [ ] 2.3 Add a JUCE-free glyph metrics table under
      `src/platform/score/engraving/` — codepoint, advance width, and the SMuFL
      anchors needed for the glyphs this pass draws (clefs, accidentals, rests,
      noteheads, flags, brace). Layout depends on this table and never on the
      font, which is what keeps decision 1 achievable.
- [ ] 2.4 Verify the embedded face loads and renders a known codepoint at
      startup, rather than discovering a missing glyph as a blank page.

## 3. The engraving layer — single staff

- [ ] 3.1 Define the layout result types in
      `src/platform/score/engraving/`: a laid-out page of systems, each of
      staves, each of placed elements positioned in staff spaces. No JUCE types,
      no `JuceHeader.h`.
- [ ] 3.2 Implement staff-degree placement: written spelling plus prevailing
      clef to a vertical staff position, including ledger lines above and below.
- [ ] 3.3 Implement running key signature state and accidental decision — draw
      an accidental only where the spelling differs from what the key signature
      and earlier accidentals in the measure already supply.
- [ ] 3.4 Implement the column model: collect distinct absolute onsets across
      the system, assign each event to its column, and width each column by the
      larger of the duration power law and the widest glyph group in it.
- [ ] 3.5 Implement greedy system breaking against the available width, with
      clef, key and time signature redrawn at the head of each system.
- [ ] 3.6 Implement barlines, measure grouping, and rests.
- [ ] 3.7 Tests in `src/tests/platform/score/engraving/` for 3.2–3.6:
      staff degrees against each clef, accidental suppression within a measure,
      column widths ordering by duration, and system breaking at a known width.

## 4. The engraving layer — grand staff

- [ ] 4.1 Emit `Part::staffCount` staves per part per system, each carrying its
      own running clef state advanced only by `Clef` entries naming that staff.
- [ ] 4.2 Place every event on `ScoreEvent::staff` within its part, with no
      inference from pitch or voice.
- [ ] 4.3 Share one column list across every staff of every part in a system, so
      equal absolute onsets align by construction.
- [ ] 4.4 Lay out the brace spanning the staves of a part with
      `staffCount > 1`, and the per-part vertical spacing around it.
- [ ] 4.5 Draw key and time signature on every staff of a part; clef per staff.
- [ ] 4.6 Tests covering #200's acceptance criteria as assertions on the layout:
      a two-staff part braced, contents on the staff the file assigned, a clef
      change on one staff leaving the other's placement untouched, and equal
      onsets at equal x across both staves.
- [ ] 4.7 Confirm `src/tests/resources/` holds a two-staff fixture with a
      mid-score clef change; author a minimal one if the corpus has none
      (`clean-import.musicxml` covers the basic two-staff case already).

## 5. Render diagnostics

- [ ] 5.1 Produce a `score::Diagnostic` list alongside the layout, in the shape
      the importer already uses.
- [ ] 5.2 Detect a voice whose events change staff within a measure and record
      "beam crosses staves, drawn unbeamed" naming the measure's printed number.
- [ ] 5.3 Record any other construct this pass does not draw, rather than
      omitting it silently.
- [ ] 5.4 Tests: a cross-staff fixture yields the diagnostic *and* places both
      notes on the staves they name; a clean score yields none.

## 6. The score view tool

- [ ] 6.1 Add the `score-view` entry to `builtInToolCatalog()` in
      `src/application/tools/BuiltInToolCatalog.h`.
- [ ] 6.2 Add its factory to `builtInToolRegistry()` in
      `src/application/tools/BuiltInTools.cpp`.
- [ ] 6.3 Write the `ToolComponent` under `src/features/score/notation/`:
      `setTheme`, `resetToDefaults`, no persisted settings, reading the score
      only through `ToolServices::currentScore`.
- [ ] 6.4 Poll on a timer and compare the score `shared_ptr` against the one
      last laid out; re-lay out on a change and on a width change, cache
      otherwise.
- [ ] 6.5 Paint the layout and nothing else — no position computed in `paint()`.
- [ ] 6.6 Vertical scrolling for a score taller than the panel.
- [ ] 6.7 An empty state for "no score open", and a way to read the render
      diagnostics when there are any.
- [ ] 6.8 Add all sources to `PRACTICE_TAKES_SOURCES` in
      `tools/cmake/Sources.cmake`.

## 7. Make it verifiable

The four edits `docs/development/architecture/adding-a-tool.md § Make it
verifiable` documents, three of which fail a test if skipped.

- [ ] 7.1 Add approved window states naming `score-view` in
      `src/application/testcontrol/ApprovedWindowStates.cpp` — one showing a
      score, one for the empty state.
- [ ] 7.2 Add `score-view` to `all-tools-docked`.
- [ ] 7.3 Bump the pinned tool count in the catalog test.
- [ ] 7.4 Add a surface in `tools/scripts/testing_suite/surfaces.py` naming the
      states and the questions a reviewer should answer about the picture.
- [ ] 7.5 Check whether finding 7 of the findings document still blocks
      photographing a populated score surface (the import summary window
      overlapping the capture); if it does, say so rather than approving a
      picture of the empty state as coverage.

## 8. Quality gates

- [ ] 8.1 `PracticeTakesTests` green, including the new engraving tests.
- [ ] 8.2 `check_test_layout.py` — `src/tests/platform/score/engraving/` mirrors
      `src/platform/score/engraving/`, so no exemption is needed.
- [ ] 8.3 `clang-format` via pre-commit, and `clang-tidy` locally before pushing
      (it only runs in CI otherwise).
- [ ] 8.4 Confirm nothing added here is reachable from the audio or MIDI
      callbacks — the score model is already forbidden there, and this change
      must not introduce the first exception.
- [ ] 8.5 Update `docs/development/architecture/ARCHITECTURE.md` and the
      layering section of `docs/development/agents/AGENT_GUIDE.md` with
      `src/platform/score/engraving/` and `src/features/score/notation/`.
- [ ] 8.6 A golden-image run over the new surfaces, with the result recorded
      through the testing suite.
