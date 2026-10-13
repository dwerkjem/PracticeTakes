## Why

With stems and beams drawn, the page is readable. It is not yet *good*, and the
difference matters more here than it would in most applications: a musician
reads a score at speed and anything that makes them stop — a collision, a
cramped bar, a missing key signature — costs more than it looks like it should.

This change closes the gap between "readable" and "looks like a published
score". It is deliberately last, because every item in it is a refinement of
something that must already exist.

## What Changes

- **Key signatures**, which are not drawn at all today. Accidental placement
  follows the clef, so a renderer that ignores them makes every transposed
  piece wrong on sight. This is the single largest correctness gap left, not a
  polish item, and it leads.
- **Time signatures**, likewise undrawn.
- **Proper system head layout**: clef, then key, then time, each at its own
  width rather than inside today's fixed 9-staff-space allowance, with a
  reserved indent on the first system.
- **Collision avoidance.** Accidentals in a chord stack into columns rather than
  overlapping; a notehead a second away from its neighbour offsets to the other
  side of the stem; ledger lines of adjacent notes merge rather than doubling.
- **Ties and slurs as drawn curves.** Tie *linkage* is in the model and is
  currently invisible on the page, so a tied note reads as two struck notes —
  which is a misreading, not a missing ornament.
- **Dynamics and tempo marks.** `Direction` is in the model, carries its staff
  and its onset, and nothing draws it.
- **Lyrics.** `LyricSyllable` is in the model, with verses, and nothing draws
  them. For singing — which is what the system-wide target note is scoped to —
  a vocal line without its words is half a score.
- **Better spacing.** The current power law is a first approximation; real
  engraving also accounts for the content of neighbouring columns, so a bar
  dense with accidentals does not crush the bar after it.
- **Multi-measure rests, repeat barlines and volta brackets.** `RepeatMarkings`
  is captured by the model, uninterpreted, explicitly so the renderer could draw
  it. Nothing does.
- **Page breaks that fall at musical places** rather than wherever the greedy
  fill runs out of width, and justification so a system fills its width instead
  of ending ragged.

## Capabilities

### Modified Capabilities
- `score-notation-rendering`: adds key and time signatures, collision rules,
  ties and slurs, directions, lyrics, repeat structures, justification, and a
  stated spacing quality bar. Everything already required stays required.

## Impact

- **Changed, broadly**: most of `src/platform/score/engraving/`. Collision
  avoidance in particular is not a local addition — it is a pass over a placed
  column, and the layout types need to carry enough to run it.
- **Changed**: `LayoutTypes.h` grows curves, text runs and spanning elements.
  Text means the painter needs the *UI* typeface as well as the music font,
  which it does not use today.
- **Risk**: this is the change most likely to sprawl. "Looks like a published
  score" has no end state, so the scope above is a list and anything not on it
  is a later change. A named list is the mitigation.
- **Blocked by**: `engraving-stems-and-beams`. Justification and collision
  avoidance both need to know where stems and beams are.

## Long-term compatibility

Same as its predecessor: nothing is persisted, and the lasting commitment is
that the page keeps meaning what it meant. Two specific obligations follow from
items here. Lyrics and directions are **user-visible content from the file**, so
failing to draw one must remain a diagnostic rather than a silent omission, the
rule `score-notation-rendering` already sets. And once systems are justified,
the preview images in `score-library` change appearance, so the preview cache
needs its version bumped by this change rather than serving stale images that no
longer match the application.
