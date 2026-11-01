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
- **Slurs, and ties that cross a system break.** Ties within a system are drawn.
  A tie whose ends land on different systems is counted and reported rather than
  drawn, which is honest but is still a tie the reader cannot see. Slurs are not
  drawn at all.
- **Vertical placement of what is already drawn.** Dynamics, tempo marks and
  lyrics all reach the page, each in a reserved band. What they do not do is
  avoid each other or the notes: a low note and a dynamic claim the same space
  and neither moves.
- **Better spacing.** The current rule is proportional with a minimum, which is
  a first approximation; real engraving also accounts for the content of
  neighbouring columns, so a bar dense with accidentals does not crush the bar
  after it.
- **Multi-measure rests and volta brackets.** Repeat barlines are drawn.
  `RepeatMarkings` also carries endings and jumps, which the renderer reports as
  undrawn in a notice; nothing draws them.
- **Page breaks that fall at musical places** rather than wherever the greedy
  fill runs out of width, and justification so a system fills its width instead
  of ending ragged.

## Capabilities

### Modified Capabilities
- `score-notation-rendering`: adds collision rules, ties and slurs, directions,
  lyrics, repeat structures, justification, a first-system indent, and a stated
  spacing quality bar. Everything already required stays required.

## Impact

- **Changed, broadly**: most of `src/features/score/engraving/`. Collision
  avoidance in particular is not a local addition — it is a pass over a placed
  column, and the layout types need to carry enough to run it.
- **Changed**: `ScoreLayout.h` grows curves, text runs and spanning elements.
  Text means the painter needs the *UI* typeface as well as the music font,
  which it does not use today.
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
