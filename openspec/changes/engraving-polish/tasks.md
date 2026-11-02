Three tiers, each shippable on its own and in this order. Tier 1 is why the
change exists: each item in it makes the page say different music from the
file. Tier 3 can be cut without anyone playing a wrong note. See design.md
decision 1.

## 0. The fixture everything below is checked against

- [ ] 0.1 A harness score, `engraving-polish.musicxml`, under
      `src/tests/resources/harness-scores/`: a chord with a sharp on every note,
      a second under an up stem and under a down stem, a tie that crosses a
      system break at the default width, a `1.` and `2.` ending with a repeat
      barline, a Segno and a "D.S. al Coda", lyrics under notes with ledger
      lines, and four bars of rest in a row.
- [ ] 0.2 An approved state that opens it in the score view, and a capture
      surface, `score-view-engraving`, in `surfaces.py`. Captured before any
      change below, so every later tier has a picture to compare against.

## 1. Tier 1 — wrong music

### Accidentals and seconds

- [ ] 1.1 An accidental metrics table in staff spaces — width, and vertical
      extent above and below the sign's position, per `AccidentalKind` — so the
      layout can stack signs and stay font-free.
- [ ] 1.2 A test pinning that table to Bravura's bounds, beside the existing
      `noteheadWidth` check in `MusicFontTests`.
- [ ] 1.3 `NoteheadGlyph::accidentalX`, decided by the layout. The painter draws
      the sign there and stops right-aligning it itself.
- [ ] 1.4 Column stacking for a chord's accidentals: top to bottom, placed
      alternately from the outside in, each in the rightmost column it clears
      (design.md decision 3).
- [ ] 1.5 A slot's `leftPad` is the width the stack used, replacing the constant
      `accidentalWidth`.
- [ ] 1.6 `offsetSeconds` takes the stem direction: with the stem up the upper
      note of a second moves right, with it down the lower note moves left.
      Moved after stem direction is resolved.
- [ ] 1.7 Tests, by named case: one sharp; a C-major triad with a sharp on every
      note in two columns; a cluster needing three; a sharp on the shifted note
      of a second, clear of the other head; a second under a down stem, with the
      upper note attached to its stem.
- [ ] 1.8 Move the accidental rule in `notation-engraving.md` from the painter's
      list to the layout's, in the same commit as the code.

### Ties across a break

- [ ] 1.9 A tie whose ends fall on different systems is emitted as two halves:
      note to system end, and next system's content start to note.
- [ ] 1.10 Remove the `tiesAcrossSystems` notice — there is nothing left to
      report.
- [ ] 1.11 Tests: the two halves land on the two systems, curve the same way,
      and the second starts after the next system's clef and key.

### Voltas and jumps

- [ ] 1.12 Derive volta brackets from runs of consecutive measures sharing a
      non-empty `endingNumbers`; label as written; closed when the run ends on a
      repeat barline, open otherwise (design.md decision 8).
- [ ] 1.13 A run across a system break continues without repeating its label.
- [ ] 1.14 Jump marks: `Segno` and `Coda` as SMuFL signs at the measure start;
      any other string as text right-aligned at the measure end; an
      unrecognised string drawn as text rather than dropped.
- [ ] 1.15 Remove the ending and jump notices.
- [ ] 1.16 Tests: `1.` then `2.` as two brackets; `1. 2.` as one; closed against
      a repeat barline and open without one; a bracket across a break; Segno,
      Coda, and an unrecognised jump string.

## 2. Tier 2 — collisions

### Vertical placement

- [x] 2.1 `TextGlyph` gains a `y`. `drawText` stops computing offsets, and the
      duplicated `1.8` goes with it. `LyricGlyph` gained one too, for the same
      reason, and `drawSystem` lost its verse-count parameter.
- [x] 2.2 A per-system envelope above and below the staff from everything placed
      on it: noteheads and their ledger lines, stems, beams, accidentals.
      Per staff, not per system: a piano's hands have different envelopes.
- [x] 2.3 Bands placed clear of it: measure numbers, tempo above; lyrics, then
      dynamics below. Voltas and jumps join the above bands with 1.12–1.14.
- [x] 2.4 Variable system height: `topOfStaff` accumulates and `totalHeight` is
      the sum. `ScoreViewComponent` needed one line: it passed the verse count
      `drawSystem` no longer takes. A system grows by how far its notes pushed
      its text, not by where text already sat, so a system with nothing in the
      way keeps its old height exactly — pinned by a test.
- [x] 2.5 Tests: a note four ledger lines below moves that system's lyrics down
      and not the next system's; a tempo mark clears a note above the staff;
      changing `lyricRowHeight` moves the dynamics with the lyrics; a dynamic
      between a piano's staves clears the left hand's high notes; nothing in
      the way leaves every band where it was; determinism. Three mutations —
      lyrics ignoring the notes, tempo ignoring them, staves ignoring what they
      hold — each fail one. `score-view-sample` and `score-view-notation` gained
      a `text-clear-of-notes` question (#252).

### Ledger lines

- [ ] 2.6 Ledger lines emitted per event column: one line per staff position any
      note of the chord needs, spanning the union of their extents. Retire
      `NoteheadGlyph::ledger`.
- [ ] 2.7 Tests: two notes above the staff share their lines; a shifted note
      extends the line under it rather than gaining a second one.

## 3. Tier 3 — appearance

- [ ] 3.1 Justification: every system but the last stretched to
      `availableWidth`, by scaling the gaps between slots in proportion; headers
      keep their width. Applied while emitting, before ties, voltas and text
      read any x.
- [ ] 3.2 A determinism test: same score, same width, identical numbers.
- [ ] 3.3 `LayoutOptions::firstSystemIndent`, taken from system 0's width when
      packing, with the part name set in it.
- [ ] 3.4 Multi-measure rests: two or more consecutive whole-bar-rest measures
      with no clef, key or time change, repeat, ending, jump or direction,
      condensed to one H-bar with its count. Measure index ranges still cover
      the condensed bars.
- [ ] 3.5 Structural breaks: prefer a break at a repeat barline or the end of a
      volta run within the last two measures that fit, if the system left is at
      least 75% full.
- [ ] 3.6 Tests for each: a full system ends at `availableWidth` and the last
      does not; the indent; a rest run broken by a key change; a break moved
      back one bar to a repeat barline, and not moved when it would leave a
      system under 75%.

## 4. Verification and close-out

- [ ] 4.1 Recapture `score-view-engraving`, `score-view-notation` and
      `score-view-notation-large` at the end of each tier, and retake their
      references deliberately rather than discovering them as failures.
- [ ] 4.2 If `score-library` has landed first, bump its preview-cache version:
      justification changes every preview.
- [ ] 4.3 Update `notation-engraving.md` for every placement decision that moved
      from the painter to the layout.
