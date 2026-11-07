## Context

What is on `main` after `score-notation-renderer` (#225) and
`engraving-stems-and-beams` (#238), surveyed for this design rather than taken
from the proposal's first draft, which predated both:

- **Accidentals are placed by the painter, one notehead at a time.**
  `ScoreGlyphPainter` right-aligns each sign `accidentalGap` before its own
  notehead using Bravura's bounds. `notation-engraving.md` lists this as one of
  the painter's sanctioned placement rules. The layout reserves a constant
  `accidentalWidth` of room per slot.
- **The upper note of a second always moves right.** `offsetSeconds` shifts it
  by `noteheadWidth` whatever the stem direction, and its own comment says a
  first pass "would be guessing at the rest of the collision problem too".
  Because a notehead's `x` includes that shift, the accidental on a shifted note
  is right-aligned to the *shifted* head and lands inside the unshifted one.
- **Ledger lines are drawn per notehead**, each centred on its own head, so two
  notes of a chord beyond the staff draw the shared ledger lines twice, at two
  widths.
- **A tie across a system break is counted and reported**
  (`tiesAcrossSystems`), not drawn.
- **Endings and jumps are reported, not drawn** ("Ending (volta) brackets are
  not drawn in this pass"). The model carries `RepeatMarkings::endingNumbers`
  per measure and `jumps` as as-written strings. It does not carry an ending's
  `stop`/`discontinue` type.
- **Text is placed vertically by the painter.** `drawText` hardcodes tempo at
  −2.1 spaces, measure numbers at −3.4, and dynamics at
  `staffHeight + 1.5 + verses × 1.8` — where `1.8` restates
  `LayoutOptions::lyricRowHeight` instead of reading it. Nothing considers
  where notes are, so a note with ledger lines above the staff runs into the
  tempo mark and one below runs into the lyrics.
- **Every system is the same height** (`systemHeight`, fixed per layout), and
  every system is as wide as its measures happen to be: packing is greedy and
  nothing is justified.
- **Slurs are not in the model.** The importer drops `<slur>` deliberately.
  This is why the proposal no longer lists them.

## Goals / Non-Goals

**Goals**

- Nothing on the page reads as different music from the file: no accidental
  that appears to belong to the wrong note, no tie that is invisible, no ending
  that reads as two bars in turn.
- Nothing drawn overlaps anything else drawn, for the cases named below.
- Every placement decision in the layout, testable without a display — which
  means moving two that are currently the painter's.
- Each tier below is shippable on its own.

**Non-Goals**

- Slurs (import and model first; see the proposal).
- Contour-hugging ("skyline per column") vertical placement. The per-system
  version below is the first one; decision 6 says what it costs.
- Cross-staff anything: the score view draws one staff of one part.

## Decisions

### 1. Order by what a misreading costs, in three shippable tiers

The proposal's list has no end state, and its own risk section says so. A named
list in a fixed order is the mitigation, ordered by what each item costs a
reader if it is missing:

| Tier | Items | Cost if missing |
|---|---|---|
| 1. Wrong music | accidental and second collisions; ties across a break; voltas and jump marks | the player plays something the file does not say |
| 2. Collisions | vertical text placement; ledger line merging | the reader has to stop and untangle |
| 3. Appearance | justification; first-system indent; multi-measure rests; structural system breaks | the page looks unfinished |

Tier 1 is the reason this change exists. Tier 3 can be cut without anyone
playing a wrong note.

### 2. Accidental placement moves from the painter into the layout

Stacking a chord's accidentals depends on its other accidentals, and the painter
sees one glyph at a time. So the placement rule `notation-engraving.md` gives
the painter moves: `NoteheadGlyph` gains an `accidentalX`, decided by the
layout, and the painter draws the sign there. The painter keeps what it should:
which glyph, at what weight.

The layout stays font-free, as the engraving layer must. It needs only two
numbers per accidental kind — width, and vertical extent above and below its
staff position — which come from a small table in staff spaces. A test checks
the table against Bravura's bounds, the same way `MusicFontTests` already pins
`noteheadWidth`, so the two cannot drift silently.

### 3. Chord accidentals stack in columns, outermost first

The conventional algorithm (Gould, *Behind Bars*, accidentals in chords):

1. Order the chord's accidentals top to bottom.
2. Place them alternately from the outside in — top, bottom, second from top,
   second from bottom — because the outer notes are the ones a reader finds
   first.
3. Each goes in the rightmost column where its vertical extent clears every
   accidental already in that column; a new column opens to its left when none
   does.

Columns are measured from the left edge of the chord's leftmost notehead, which
makes the second-offset case fall out for free: the sign on a shifted note is no
longer right-aligned to the shifted head.

The slot's `leftPad` becomes the width the stack actually used, replacing the
constant `accidentalWidth`, so a chord with three columns of accidentals gets
three columns of room and a single sharp does not reserve three.

**What "clears" means, decided while implementing.** Box against box from the
metrics table, with a small vertical clearance (`accidentalVerticalClearance`,
0.1 space) and no interlocking. An allowance letting sharps and naturals overlap
their boxes by a space — enough to put a triad's outer sharps, a fifth apart, in
one column — was tried first and rejected on a render: right-aligned in Bravura,
the upper sign's right upright runs into the lower one's. So a triad with a
sharp on every note takes three columns (top nearest, bottom next, middle
furthest), which the spec's "at least two columns, none overlapping" allows,
and two sharps share a column from a seventh. The clearance is for flats, whose
boxes clear a sixth apart by under a twentieth of a space and meet stem to stem.

Every event at one onset on one staff is one stack, whatever its voice: two
voices' heads share the slot's x, so their signs would collide exactly as one
chord's would. A grace note is stacked on its own, at grace size.

### 4. A second moves to the side away from the stem

With the stem up, noteheads hang left of the stem and the *upper* note of a
second moves right, across it. With the stem down, noteheads hang right of the
stem and the *lower* note moves left. `offsetSeconds` does the first in every
case; it gets the stem direction it currently does not know, which is why it
runs after stem direction is resolved rather than before.

### 5. A tie across a break is drawn as two halves

The convention: the first half runs from its note to the end of the system, the
second from the start of the next system's content — after its clef and key —
to its note. Both halves curve the same way, opposite the stem of the note they
leave from. The notice is removed, because there is no longer anything to report.

### 6. Text is placed by the layout, above and below a per-system envelope

`TextGlyph` gains a `y`, and `drawText` stops computing offsets. The layout
places each band — measure numbers, tempo, voltas and jumps above; lyrics, then
dynamics below — clear of the furthest extent of anything already placed on the
system: noteheads with their ledger lines, stems, beams and accidentals.

That makes system height variable: each system is as tall as its own envelopes
and bands, and `topOfStaff` accumulates rather than multiplying a constant.
`ScoreViewComponent` already reads `topOfStaff` and `totalHeight`, so it needs no
change.

**Per system, not per column.** One very low note pushes the whole system's
lyrics down, where an engraver would move only the syllables near it. The
per-column version needs a contour and a placement search; the per-system one
is a maximum. It is chosen here because it removes every collision this change
names, is deterministic by construction, and the contour can replace it later
without changing anything outside the layout.

This also removes the duplicated `1.8`: there is one `lyricRowHeight`, read in
one place.

### 7. Ledger lines belong to a chord, not to a notehead

The layout emits ledger lines per event column: one line per staff position
beyond the staff that any of the chord's notes need, spanning the union of
those notes' horizontal extents. Two notes on the same side of the staff then
share their lines; a note shifted for a second extends the line under it rather
than getting a second, offset one. `NoteheadGlyph::ledger` is retired in favour
of the column's.

### 8. Volta brackets come from runs of shared ending numbers

The model has `endingNumbers` per measure and nothing else, which is enough:

- A **bracket** is a maximal run of consecutive measures with the same, non-empty
  `endingNumbers`. Two adjacent runs with different numbers are two brackets.
- Its **label** is the numbers as written: `1.`, or `1. 2.` for a shared ending.
- Its right end is **closed** — the hook turns down — when the run's last measure
  ends on a repeat barline, and **open** otherwise. That is the convention MusicXML
  encodes as `stop` versus `discontinue`, recovered from the barline, because the
  model does not carry it and changing the model for one hook is not proportionate.
- A run that crosses a system break continues on the next system without
  repeating the label.

### 9. Jump marks by exact text, with a fallback

`jumps` is open-ended, as-written strings. `Segno` and `Coda` are drawn as their
SMuFL signs at the measure's start; every other string — "D.S. al Coda",
"D.C. al Fine", "Fine", "To Coda" — is drawn as text above the staff,
right-aligned at the measure's end, where the convention puts an instruction
that takes effect *after* the bar. An unrecognised string is drawn as text
rather than dropped, because a jump the reader cannot see sends them the wrong
way.

### 10. Justification stretches the space between notes, never the headers

A full system is stretched to `availableWidth`; the last system is left at its
natural width, which is the convention and avoids a two-bar final system
stretched across the page. The stretch is applied to the gaps between slots,
in proportion to each gap, so proportional spacing stays proportional. Clefs,
key signatures, time signatures and repeat barlines keep their widths.

This has to happen while a system is being emitted, before ties, voltas and text
read any x — so it is a factor `emitMeasure` applies, not a pass afterwards.

### 11. The first system is indented, and carries the part name

`LayoutOptions::firstSystemIndent`, taken out of system 0's available width when
packing, with the part name set in it. Every later system starts at the margin.

### 12. Multi-measure rests are a run of empty bars with nothing else in them

Two or more consecutive measures condense to one, drawn as the conventional
H-bar with the count above, when every one of them holds only a whole-bar rest
on the drawn staff *and* carries nothing else a reader needs: no clef, key or
time change, no repeat barline, ending, jump or direction. Any of those ends the
run, because condensing it would hide it. `System::firstMeasureIndex` and
`lastMeasureIndex` still cover the condensed range, so anything that finds a
measure by index is unaffected.

### 13. A system break prefers a structural boundary, within a tolerance

When the greedy fill would break, and a measure within the last two that fit
ends on a repeat barline or the end of a volta run, break there instead —
provided the system it leaves is at least 75% full. Otherwise break where the
fill ran out. Narrow on purpose: a "musical place" that needs judgement about
phrasing is not something this layer can know, and pretending otherwise would
produce breaks that are confidently wrong.

## Verification

- **Layout tests**, JUCE-free, in `src/tests/features/score/engraving/`: one per
  decision, by named case — a C-major triad with a sharp on every note stacks in
  three columns, and a chord spanning a seventh in two (decision 3); a second under a down stem moves left; a tie across a break emits
  two halves on two systems; a `1. 2.` bracket over two bars closed by a repeat
  barline; a low C under a lyric moves the lyric down and only that system's.
- **A determinism test** for justification: the same score at the same width
  produces identical numbers, as the renderer already requires.
- **A metrics test** pinning the accidental width table to Bravura.
- **A capture surface**, `score-view-engraving`, from a new harness fixture that
  exercises tier 1 and 2 at once — collisions, a cross-system tie, voltas, a jump
  and lyrics under low notes — because whether a stack of accidentals *looks*
  right is a question only a picture answers.

## Risks

- **Every page changes.** Justification and variable system height move
  everything, so every existing capture of the score view differs. That is the
  point, but it means the references are retaken once, deliberately, in the
  tier that changes them — not discovered as failures later.
- **Per-system envelopes waste vertical space** on a system with one outlier
  note. Accepted for this pass; decision 6.
- **Moving a sanctioned painter rule.** `notation-engraving.md` documents
  accidental placement as the painter's. The document changes with the code,
  so the rule and its description move together.
- **The preview cache.** If `score-library` lands before this, its previews are
  rendered through this layout, and justification changes them: that cache's
  version is bumped by whichever of the two lands second.
