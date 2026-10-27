## Why

**The page is unreadable as rhythm.** The first engraving pass draws noteheads,
stems, flags and dots, so a quarter note and a half note differ by the notehead
being filled or hollow — and that part reads correctly. What it does not do is
beam. Every eighth and sixteenth in a run carries its own flag, which is correct
notation for a note alone in its beat and unidiomatic for four in a row.

That matters more than it looks. A musician reads duration off stems and beams
before anything else, and **a beam group states the metre**: six eighths beamed
3+3 read as 6/8 whatever the time signature says, and the same six beamed in
pairs read as 3/4. A page of flags states nothing at all, which is why #226 is
the first child of #231 and says *"do this first"* — every other visual change
is a refinement of a page that has to be readable first.

The gap is also already admitted. `ScoreLayout::notices` carries the sentence
*"Eighth notes and shorter are drawn with flags rather than beamed"*, and
`StemGlyph::flags` says in a comment that this pass does not beam. The notice
list is what makes a first pass shippable; it is not a place for a gap to live
permanently.

## What Changes

- Add **`BeamGrouping.{h,cpp}`** — JUCE-free, deciding which notes beam
  together from onsets, durations and the time signature. Simple metres group
  by the beat, compound metres by the dotted beat, an unreadable metre groups
  nothing. A run breaks at anything unbeamable, at a group boundary, and at a
  gap in time. A run of one is a flag.
- Add **`BeamGeometry.{h,cpp}`** — JUCE-free, placing a group's beams and the
  stems under them: a straight line through the outer stems' natural tips, its
  rise clamped, the whole line pushed away from the staff until every stem is
  long enough and clear of its chord's far notehead. Stacked levels, and
  partial beams for a level with no neighbour.
- Add **`BeamGlyph`** to `ScoreLayout`, and `beamLevels` to `StemGlyph`. Change
  `StemGlyph::tipPosition` to **`tipSpaces`**, a double: a beam has a slope, so
  the stems under it end between staff positions.
- **Beam groups overrule stem direction.** A group takes one direction, from the
  note furthest from the middle line, because a beam joins stems pointing the
  same way.
- A beamed stem draws **no flags**: the beam stands in for them.
- Derive the minimum room between two onsets from the notehead and flag widths,
  so a flagged eighth cannot touch what follows it.
- Replace the "not beamed" notice with one that says the grouping came from the
  **time signature** rather than from the file, and add notices for the two gaps
  that remain: a beam is not carried over a rest, and grace notes are not beamed.
- Draw beams in `ScoreGlyphPainter` as filled parallelograms with vertical ends.

## Impact

- **Affected specs:** `engraving-stems-and-beams` (new). The
  `score-notation-renderer` spec's requirement that eighths and shorter are
  drawn with flags is narrowed to notes a beam does not reach.
- **Affected code:** `src/features/score/engraving/` (two new files, three
  changed), `ScoreGlyphPainter`, `ScoreViewComponent` (one comment),
  `ApprovedWindowStates` (one description), `tools/cmake/Sources.cmake`,
  `tools/scripts/testing_suite/surfaces.py` (capture questions).
- **Out of scope:** beams crossing the brace, which keep their existing
  diagnostic; tuplet brackets and numbers; beaming over a rest; beamed grace
  notes; and the file's own `<beam>` elements, which the model does not hold.

## Long-term compatibility

`ScoreLayout` is a value with no persisted form and no wire format — nothing is
saved from it and nothing outside this layer reads it, so its shape is free to
change. The one commitment that outlives this change is the tool id
`score-view`, which a saved workspace may already hold and which is never reused
or dropped, only aliased.
