## Why

The renderer draws noteheads and nothing attached to them. A quarter note and a
half note are distinguishable only by the notehead being filled or hollow, and
an eighth note is indistinguishable from a quarter: no stem, no tail, no beam.
That makes the page **unreadable as rhythm** — a musician reads duration off
stems and flags before anything else — so the score view currently shows pitch
correctly and time not at all.

This is the first and largest gap between what is drawn and what a player can
read.

## What Changes

- **Stems.** Direction from the notehead's distance from the middle line, the
  usual rule: below the middle line the stem goes up on the right of the
  notehead, above it goes down on the left, and on the line it goes down.
  Length is one octave (3.5 staff spaces) by default, extended so that a note
  far outside the staff still reaches it.
- **Flags** — the tails on unbeamed eighths and shorter. The glyphs are already
  in the metrics table (`flag8thUp`, `flag8thDown`, `flag16thUp`,
  `flag16thDown`); nothing places them.
- **Beams** replacing flags where notes group. Grouping by beat from the
  prevailing time signature, which is what makes 6/8 read as two groups of
  three rather than three of two — a beaming rule that ignores the time
  signature is worse than no beaming, because it actively misrepresents the
  metre.
- **Stem direction within a chord and within a voice.** A chord takes one stem
  from its outermost note; two voices on one staff take opposed stems, up for
  the upper voice and down for the lower, which is how a reader tells them
  apart at all.
- **Augmentation dots**, since a dotted note is currently drawn as an undotted
  one of the wrong length. The glyph is already in the table.
- **Rests of the right duration** are already chosen but are not dotted and are
  not positioned per voice; both follow from the same work.
- **Beams that would cross the brace stay out of scope**, as
  `engraved-notation-renderer` decided, and keep their existing diagnostic.

## Capabilities

### Modified Capabilities
- `score-notation-rendering`: adds requirements for stems, flags, beams,
  augmentation dots, stem direction in chords and in multi-voice staves, and
  beam grouping from the time signature. The existing requirements are
  unchanged — this is notation the renderer does not yet draw, not notation it
  draws wrongly.

## Impact

- **New**: stem and beam layout in `src/platform/score/engraving/`, JUCE-free
  and unit tested like everything else there. Beam geometry — slope, thickness,
  where a stem ends when it meets a beam — is the part with real arithmetic in
  it and the part worth testing hardest.
- **Changed**: `LayoutTypes.h` grows a placed-stem and placed-beam type, and the
  score view paints them. The painter stays decision-free: a beam is a
  quadrilateral the layout has already positioned.
- **Changed**: column widths, because a stem and a flag occupy horizontal room
  the current spacing does not account for. This will visibly change spacing on
  every existing page, which is the point.
- **Blocked by**: nothing. The layout engine exists and this builds on it
  directly.

## Long-term compatibility

Nothing here is persisted and nothing crosses a process boundary, so there is no
format to keep compatible. The lasting commitment is the *rendering contract*
rather than a file format: once a user is reading rhythm off this page, a change
that silently alters beam grouping changes what the page says. Beaming rules
therefore belong in tests with named cases — 6/8, 3/4, cut time — so a future
change to them is a visible diff rather than a quiet re-reading of everyone's
scores.
