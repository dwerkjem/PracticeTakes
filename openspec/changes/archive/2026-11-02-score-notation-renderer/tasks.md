Settled inputs, from `design.md` § Decisions:

| | |
|---|---|
| Location | `src/features/score/engraving/`, JUCE-free, beside the tool that uses it |
| Coordinates | Staff spaces, origin at the system's top staff line, y downwards |
| Staff positions | Integer **half** spaces from the top line: 0 top line, 4 middle, 8 bottom |
| Clef | A reference pitch on a line; `octaveChange` is undone, not applied |
| Key signature | One hand-checked G-clef table, plus 1 half space for C clef, 2 for F clef |
| Accidentals | A per-measure state machine; a tie continuation never draws one |
| Spacing | Proportional in ticks with a per-event floor |
| Stems | By pitch with one voice, by voice number with two or more |
| Not drawn | Reported in `ScoreLayout::notices` and shown by the tool |
| Verifiability | One `ApprovedWindowState` field suppresses the import summary |

## 1. Staff positions

The computation the whole renderer rests on. JUCE-free, no score needed.

- [x] 1.1 Add `src/features/score/engraving/StaffGeometry.h`: the staff-space
      unit, the half-space staff position type, and the conversions. Document
      that 0 is the top line and that one diatonic step is one half space.
- [x] 1.2 Add `StaffPosition.h`/`.cpp`: `staffPositionFor(const score::Clef&,
      const score::Pitch&)` returning the half-space position, from the clef's
      reference pitch and line.
- [x] 1.3 Treble, bass, alto and tenor all place middle C where a reader expects.
- [x] 1.4 Undo `clef.octaveChange` so a vocal tenor clef draws where it reads.
- [x] 1.5 Ledger lines above and below, counted per line position beyond the
      staff.
- [x] 1.6 Stem direction for a single voice: up below the middle line.
- [x] 1.7 Report an unpitched clef (`percussion`, `TAB`, `none`) as having no
      position rather than returning one.
- [x] 1.8 Tests in `src/tests/features/score/engraving/StaffPositionTests.cpp`,
      including the enharmonic case the model keeps its spelling for.

## 2. Written note values

- [x] 2.1 Add `NoteValue.h`/`.cpp`: ticks to notehead kind, flag count and dot
      count, from a breve down to a 128th, zero to two dots.
- [x] 2.2 Apply the tuplet ratio before matching, so a triplet eighth is an
      eighth.
- [x] 2.3 Return an explicit "no written value matches" result for a duration
      that is not notatable. Never round to the nearest.
- [x] 2.4 The same for rests.
- [x] 2.5 Tests in `NoteValueTests.cpp`, covering every base value, both dot
      counts, a triplet, and an unnotatable duration.

## 3. Key signatures and accidentals

- [x] 3.1 Add `KeySignatureLayout.h`/`.cpp`: the order of sharps and flats and
      the half-space position of each, for a G, F or C clef.
- [x] 3.2 Shift by the line difference for a clef of a known sign on an unusual
      line, and say so in a notice.
- [x] 3.3 Add `MeasureAccidentals.h`/`.cpp`: the alteration in force per step
      and octave, seeded from the key signature and updated as the bar goes on.
- [x] 3.4 A natural is drawn when an alteration in force is cancelled.
- [x] 3.5 A tie continuation draws no accidental.
- [x] 3.6 Tests for both, in `KeySignatureLayoutTests.cpp` and
      `MeasureAccidentalsTests.cpp`. Pin the treble and bass positions for all
      seven sharps and all seven flats against the engraved convention.

## 4. The layout pass

- [x] 4.1 Add `ScoreLayout.h`: the glyph types and the system/layout values.
      Positions in staff spaces; no JUCE type anywhere in the header.
- [x] 4.2 Add `ScoreLayoutEngine.h`/`.cpp`: `layOutScore(score, options)`.
- [x] 4.3 Track clef, key and time through the part, drawing each where it
      changes and repeating clef and key at every system start.
- [x] 4.4 Allocate x by onset within a measure, measure widths by content.
- [x] 4.5 Break into systems at the available width; stack them with a gap that
      leaves room for lyrics and dynamics.
- [x] 4.6 Noteheads, chords (one event, several pitches), stems, flags, dots,
      ledger lines.
- [x] 4.7 Rests, including a whole rest hung from the fourth line and a half
      rest sat on the third.
- [x] 4.8 Ties, as a curve between two noteheads, dropped with a notice when the
      two ends are in different systems.
- [x] 4.9 Barlines, including repeat barlines from `Measure::repeats`.
- [x] 4.10 Lyrics below the staff, one row per verse.
- [x] 4.11 Dynamics below and tempo marks above, from `Measure::directions`.
- [x] 4.12 Grace notes drawn small, taking no horizontal time.
- [x] 4.13 Stems by voice number when a measure has more than one voice.
- [x] 4.14 Every deliberate gap produces a notice: beams, further parts, further
      staves, voltas, jumps, an unnotatable duration, an unpitched clef.
- [x] 4.15 Tests in `ScoreLayoutEngineTests.cpp`, built on `ScoreBuilder` rather
      than on a file where possible. Include a determinism test — the same score
      laid out twice gives identical numbers.

## 5. The tool

- [x] 5.1 Add `ScoreViewComponent.h`/`.cpp`: a `ToolComponent` polling
      `ToolServices::currentScore` on a timer, comparing pointer identity
      exactly as `TargetNoteComponent` does.
- [x] 5.2 Scroll vertically through the systems with a `juce::Viewport`.
- [x] 5.3 Lay out again on a width change and on a zoom change, not on every
      paint.
- [x] 5.4 Three zoom steps on the options menu, with the current one ticked.
- [x] 5.5 The empty and no-notes cases as sentences, naming the command that
      opens a score.
- [x] 5.6 Show the layout's notices, and say when the import reported
      diagnostics.
- [x] 5.7 Add `ScoreGlyphPainter.h`/`.cpp`: every glyph drawn from `juce::Path`
      primitives, in one file so that adopting a music font later is one file.
- [x] 5.8 `setTheme` and `resetToDefaults`.

## 6. Registration

- [x] 6.1 One catalog entry in `BuiltInToolCatalog.h`.
- [x] 6.2 One factory in `BuiltInTools.cpp`.
- [x] 6.3 Sources in all three lists in `tools/cmake/Sources.cmake`.
- [x] 6.4 Bump the tool count in `ToolCatalogTests`.

## 7. Making it verifiable

- [x] 7.1 Add the summary-suppressing field to `ApprovedWindowState`, appended
      positionally after `scoreFixture`.
- [x] 7.2 Honour it in `MainComponent::finishScoreImport`, and clear it when a
      state that does not set it is applied.
- [x] 7.3 Add `src/tests/resources/harness-scores/notation-sample.musicxml`: one
      part, one staff, a key signature, accidentals, rests, dotted and
      flag-length notes, a tie, a clef change, lyrics and a dynamic — and only
      constructs the importer fully supports, so it stays a clean import.
- [x] 7.4 Approved states: the empty tool, and the tool drawing that fixture.
- [x] 7.5 `all-tools-docked` includes the new tool.
- [x] 7.6 Update the two approved-state invariants rather than deleting them: a
      state showing the summary opens nothing else; a state suppressing it opens
      exactly one tool.
- [x] 7.7 Pin the new fixture's import in `ApprovedScoreFixturesTests.cpp`.
- [x] 7.8 Surfaces in `tools/scripts/testing_suite/surfaces.py`, with the
      questions a reviewer should answer about an engraved staff.
- [x] 7.9 Update the harness-scores README's table and count.

## 8. Documentation

- [x] 8.1 Add `docs/development/architecture/notation-engraving.md`: the
      coordinate space, the layout pass, what is drawn, what is not and why.
      Add it to the `docs/development/README.md` index.
- [x] 8.2 `ARCHITECTURE.md` § the score model: the renderer is a third consumer.
- [x] 8.3 `AGENT_GUIDE.md` § source layering: `src/features/score` now holds an
      engraving layer as well as the Target Note tool.
- [x] 8.4 `score-model-findings.md`: finding 7 is resolved, and record what
      consuming the model for *drawing* revealed that consuming it for text did
      not.

## 9. Verification

- [x] 9.1 `PracticeTakesTests` green.
- [x] 9.2 `clang-format` and `clang-tidy` clean on every new file.
- [x] 9.3 `python tools/scripts/run_tests.py` green (surfaces.py is Python).
- [x] 9.4 Lay out the **real-score corpus**, not only hand-built scores:
      `ScoreLayoutCorpusTests.cpp` runs every `.mxl` in
      `src/tests/resources/musicxml/` through `layOutScore` and asserts the
      properties that must hold whatever is in a file -- every glyph finite and
      inside its system, systems stacked downwards, stems pointing the way they
      say they do, and the same score laying out the same way twice.
- [x] 9.5 Build and run the application and look at what it draws. Done under
      Xvfb against `notation-sample.musicxml` through both approved states, and
      three defects came out of it that no test had: ledger lines drawn at an
      offset from the notehead instead of at their own staff positions, a
      chord's stem length measured from the far notehead so that it grew with
      the chord, and a notehead sitting on the barline of its own measure. All
      three are fixed.
- [x] 9.6 The secret audit before each commit.
