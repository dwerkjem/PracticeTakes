## Why

**A score can be imported and nothing draws it.** `src/platform/score` has two
consumers — the import summary window, which shows metadata, counts and
diagnostics and deliberately draws no staff, and the Target Note tool, which
shows one note as *text*. Thirty-five files of score model, and the only way to
see the music in a file is to read a sentence about it.

#32 is the issue for that, and it is the gate on most of Stage 3:

- **#200** (braced grand staff, per-hand staff assignment) is explicitly
  *"Blocked on: #32"*, and the 2026-10-05 roadmap refocus makes it the first
  piano capability — *"a piano score has to be readable before practising
  against it means anything."*
- **#33** (navigation, zoom, part selection, target-note overlay) has nothing to
  navigate or overlay until something is engraved.
- **#41** (live current-note and upcoming-note display) and **#52** (note-by-note
  review) are both pictures of a score.
- Stage 3's first exit criterion is *"A supported MusicXML file renders
  clearly."*

And the model was built for it. `Pitch` keeps step/alter/octave *and* the
sounding MIDI number, and the header says why in so many words: *"The renderer
(#32) needs step/alter/octave to pick a staff line and an accidental."* Invariant
10 keeps every coordinate out of the model precisely so that computing them is
this change's job. The groundwork is done and unused.

## What Changes

**#32 is estimated at 4–6 weeks and this change is not all of it.** What it is,
is the first pass that actually engraves: one part, one staff, continuous
scroll, drawn from the model with no engraving data in it. What it leaves is
written down in *Out of scope* below and in `tasks.md`, not left to be
discovered.

- Add **`src/features/score/engraving/`** — a JUCE-free layout pass turning a
  `std::shared_ptr<const score::Score>` into a `ScoreLayout`: a value holding
  every glyph with a position, in **staff spaces** rather than pixels. This is
  where every rule worth asserting about engraving lives, and it is unit tested
  with no display, following the `TargetNoteSelection` and `WorkspaceLayoutState`
  precedent.
  - **`StaffPosition`** — the one computation the whole renderer rests on: a
    `Clef` plus a spelled `Pitch` gives a staff position, ledger lines and a
    stem direction. C-sharp and D-flat are the same MIDI number and different
    lines, which is why this reads the spelling and not the number.
  - **`NoteValue`** — model ticks to a written note value: notehead, flag count,
    dot count, under a tuplet ratio. A duration that is not a notatable value is
    reported, never rounded silently.
  - **`KeySignatureLayout`** — where a key signature's accidentals sit, for a G,
    F or C clef on any line.
  - **`MeasureAccidentals`** — which notes need an accidental drawn, given the
    key and what the bar has already said. The rule nobody notices until it is
    wrong.
  - **`ScoreLayoutEngine`** — measures into systems, onsets into x positions.
- Add a **Score View tool** (`src/features/score/ScoreViewComponent`) — the
  consumer, and the fifth entry in the catalog. It polls `CurrentScore` through
  `ToolServices` exactly as the Target Note tool does, lays the score out for
  its own width, and scrolls. Zoom is three steps on the options menu.
- Add **`ScoreGlyphPainter`** — the JUCE half: noteheads, stems, flags, beamless
  tails, rests, accidentals, clefs, ties, dots, ledger lines, barlines, lyrics
  and dynamics, drawn from `juce::Path` primitives rather than from a music
  font.
- **Make the result photographable.** The harness can put a score into the
  application but not into a tool: `startScoreImport` also opens the summary
  window, and an overlapping window corrupts a capture — finding 7 of
  `score-model-findings.md`. One field on `ApprovedWindowState` suppresses the
  summary for a state that opens a tool, which closes that finding and is what
  makes a *renderer* verifiable at all.
- Add **one harness score** written to be a picture of notation: a key
  signature, accidentals, rests, dotted and beamed-length notes, a tie, a clef
  change, lyrics and a dynamic. The two existing fixtures are whole notes, which
  photograph nothing a renderer does.

## Out of scope, and why

Each of these is a decision, not an omission. Every one is reported by the
layout rather than silently skipped.

- **Beams.** Eighths and shorter get flags. Flags are correct notation, just not
  idiomatic for a run of them; beaming needs a grouping pass over the time
  signature that is worth its own change. The layout says so in its notices.
- **More than one part, and more than one staff of a part.** The first part's
  first staff is drawn and the rest are named in a notice. Part selection is
  #33's and the braced grand staff is #200's — the issue that is *"renderer
  work, not model work"* and is waiting on this one.
- **Page presentation.** Continuous vertical scroll only. #33 owns navigation.
- **Volta brackets and D.C./D.S. jumps.** `Measure::repeats` carries them
  uninterpreted; repeat barlines are drawn and the rest is a notice.
- **Slurs, articulations, ornaments, hairpins, fingering, cue notes.** The model
  does not carry most of them, by invariant 10 or by subset.
- **A SMuFL music font.** Bravura would make the glyphs beautiful and brings a
  licence, a binary asset and a packaging question with it. Primitives first.
