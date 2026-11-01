## Why

With stems and beams drawn, the page is readable. It is not yet *good*, and the
difference matters more here than it would in most applications: a musician
reads a score at speed and anything that makes them stop — a collision, a
cramped bar, a tie that reads as two struck notes — costs more than it looks
like it should.

This change closes the gap between "readable" and "looks like a published
score". It is deliberately last, because every item in it is a refinement of
something that must already exist.

## What Changes

This list was written before `score-notation-renderer` landed and has been cut
back to what that change and `engraving-stems-and-beams` do **not** already do.
Ties, lyrics, dynamics, tempo marks, key and time signatures, ledger lines and
repeat barlines are all drawn today; they are named here only where something
about them is still missing.

- **A reserved indent on the first system**, and the part name set against it.
  Key signatures, time signatures and a head laid out as clef-then-key-then-time
  at its own computed width landed in `score-notation-renderer` rather than
  waiting for this change, because the spec delta there already required them.
  What is left is the *first* system's indent, a page-level convention rather
  than a head-level one.
- **Collision avoidance**, which is the largest item and the one the renderer
  deliberately declined to guess at. Accidentals stack into columns rather than
  overlapping — today an accidental is drawn a fixed distance left of its
  notehead, so a sharp on the upper note of a second overlaps its neighbour. A
  notehead a second away from its neighbour offsets to the correct side of the
  stem rather than always to the right, which is what `offsetSeconds` does now
  by its own admission. Ledger lines of adjacent notes merge rather than
  doubling.
- **Ties that cross a system break.** Ties within a system are drawn. A tie
  whose ends land on different systems is counted and reported rather than
  drawn, which is honest but is still a tie the reader cannot see -- and an
  unseen tie reads as two struck notes.
- **Vertical placement of what is already drawn.** Dynamics, tempo marks and
  lyrics all reach the page, each in a reserved band. What they do not do is
  avoid each other or the notes: a low note and a dynamic claim the same space
  and neither moves.
- **Better spacing.** The current rule is proportional with a minimum, which is
  a first approximation; real engraving also accounts for the content of
  neighbouring columns, so a bar dense with accidentals does not crush the bar
  after it. `engraving-stems-and-beams` already made each gap clear a notehead
  and its flag; what is left is the accidentals, and it falls out of stacking
  them — a slot reserves the width its stack actually uses rather than one
  constant (design.md decision 3).
- **Volta brackets and jump marks.** Repeat barlines are drawn, but the endings
  under them are not, so a first and second ending read as two bars played in
  turn -- which is the wrong music, not a missing ornament. `RepeatMarkings`
  carries the ending numbers and the jump text (Segno, Coda, D.S. al Coda,
  Fine); the renderer reports both as undrawn and nothing draws them.
- **Multi-measure rests.** A run of whole-bar rests condensed to one bar with a
  count, which is the convention for a single part -- and a single part is what
  the score view draws.
- **Page breaks that fall at musical places** rather than wherever the greedy
  fill runs out of width, and justification so a system fills its width instead
  of ending ragged.

### Non-goals

- **Slurs.** Drawing one is the easy half. The importer drops `<slur>` on
  purpose -- "slurs are phrasing marks; this importer reads only sounding ties"
  -- so the model has nothing to draw. Adding them is an import and model change
  first, with its own compatibility questions, and belongs in a change of its
  own rather than at the end of an engraving list.
- **Anything not on the list above.** "Looks like a published score" has no
  end state; the list is the scope.

## Capabilities

### Added Capabilities
- `engraving-polish`: collision rules for accidentals, seconds and ledger
  lines; ties across a system break; vertical placement of text decided by the
  layout against what is already on the staff; volta brackets and jump marks;
  multi-measure rests; justified systems with a first-system indent. Its own
  capability, as `engraving-stems-and-beams` is, because the renderer's
  capability is not yet archived into `openspec/specs/` for a delta to modify.
  Everything the renderer and the beaming change already require stays
  required.

## Impact

- **Changed, broadly**: most of `src/features/score/engraving/`. Collision
  avoidance in particular is not a local addition — it is a pass over a placed
  column, and the layout types need to carry enough to run it.
- **Changed**: `ScoreLayout.h` grows half ties, volta brackets, chord ledger
  lines, a vertical position for text, and an accidental x. The painter already
  draws text in the UI typeface; what changes is that it stops deciding where.
- **Moved**: two placement rules go from `ScoreGlyphPainter` to the layout —
  where an accidental sits, and how far above or below the staff text sits —
  because both depend on neighbours the painter never sees. See design.md
  decisions 2 and 6.
- **Risk**: this is the change most likely to sprawl. "Looks like a published
  score" has no end state, so the scope above is a list and anything not on it
  is a later change. A named list is the mitigation.
- **Builds on**: `engraving-stems-and-beams`, now on `main` (#238), so nothing
  blocks this change. Justification and collision avoidance both need to know
  where stems and beams are, which is why it had to come first.

## Long-term compatibility

Same as its predecessor: nothing is persisted, and the lasting commitment is
that the page keeps meaning what it meant. Two specific obligations follow from
items here. Lyrics and directions are **user-visible content from the file**, so
failing to draw one must remain a diagnostic rather than a silent omission, the
rule `score-notation-rendering` already sets. And once systems are justified,
the preview images in `score-library` change appearance, so the preview cache
needs its version bumped by this change rather than serving stale images that no
longer match the application.
