## Why

The score view draws one staff of one part. A piano score therefore shows its
right hand and nothing else, with a notice saying the left hand was not drawn.
For a practice application that is the most visible gap left in the score view:
half of every keyboard score is missing, and keyboard music is a large share of
what anyone will open. It is #200, and it is the last change before a release.

## What Changes

- **A multi-staff part is drawn as one braced system** holding all its staves,
  top to bottom: a brace at the left, and barlines running through every staff
  and the space between them, as a piano part is printed.
- **Notes at the same moment line up across staves.** Horizontal spacing is
  decided once per measure for all staves together, so beat two of the left hand
  is directly under beat two of the right. This is the substance of the change:
  laying each staff out independently would draw two correct staves that
  disagree about where the beats are.
- **Each staff keeps its own clef and its own accidentals.** A clef change on
  the lower staff changes only the lower staff, and an accidental applies to its
  own staff, which is the convention.
- **A single-staff score is unchanged**, number for number.

### Non-goals

- **Choosing which part to draw** (#33). The first part is still the one drawn;
  this change draws *all of its staves*.
- **Cross-staff notation**: a beam or chord that moves between staves, or a
  stem from one staff's note into the other. These are drawn on the staff their
  note belongs to.
- **Placement refinements**, which are `engraving-polish`'s.

## Capabilities

### Added Capabilities
- `grand-staff`: drawing every staff of a multi-staff part as one braced system,
  with notes at the same moment aligned across staves, barlines through every
  staff, and clefs and accidentals kept per staff. Its own capability, as
  `engraving-stems-and-beams` is, because the renderer's capability is not yet
  archived into `openspec/specs/` for a delta to modify.

## Impact

- **Changed**: `ScoreLayoutEngine.cpp`. Its per-measure planning splits into a
  per-staff vertical phase and a per-measure horizontal phase taken across all
  staves at once.
- **Changed, additively**: `ScoreLayout.h`. Every positioned glyph gains the
  index of the staff it is on, and `System` gains each staff's vertical offset.
  A single-staff layout has one staff at offset zero, so every existing consumer
  and test reads it exactly as before.
- **Changed**: `ScoreGlyphPainter` draws each staff's lines at its offset,
  barlines through all of them, and the brace — a SMuFL glyph, stretched to the
  staves' span.
- **Removed**: the notice that only the top staff is shown.
- **Builds on**: `score-notation-renderer` and `engraving-stems-and-beams`, both
  on `main`.

## Long-term compatibility

Nothing is persisted. The lasting commitment is the layout's shape: once glyphs
carry a staff index and systems carry staff offsets, the target-note overlay and
anything else that finds a note's glyph must honour both. Single-staff systems
remain the special case of one staff at offset zero, not a separate path, so the
two cannot drift.
