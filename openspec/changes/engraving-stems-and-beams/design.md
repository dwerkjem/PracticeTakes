## Context

`src/platform/score/engraving/` lays a score out into placed glyphs and draws
them through `src/features/score/notation/`. It places noteheads, accidentals,
rests, clefs, key and time signatures, ledger lines, barlines and braces. It
places nothing that carries **duration**.

The consequence is visible in the first capture of the `score-view-page`
surface: twelve bars of music in which every note looks the same length. A
reader gets pitch and gets no rhythm at all. That is the gap this change
closes, and it is why the proposal calls it the largest one left.

Constraints inherited and unbent:

- **The engraving layer stays JUCE-free and font-free.** Stem and beam geometry
  is arithmetic, which makes it the most testable thing in the change and the
  part most worth testing hardest.
- **The painter decides nothing.** A beam arrives as a quadrilateral with four
  corners already computed; a stem as a line with two ends. If the painter has
  to work out a slope, the split has been broken.
- **Staff spaces and staff steps, never pixels.** Laying out twice at two sizes
  still has to produce identical numbers.
- **Invariant 10.** Nothing computed here goes back into `Score`.

One thing the model does *not* carry matters more here than anywhere so far:
**there is no notated note type.** The importer reads `<type>` and keeps only
ticks, so "is this an eighth note" is a question answered by arithmetic on
`ScoreEvent::duration` rather than by reading a field. Decision 1 is about that.

## Goals / Non-Goals

**Goals:**

- A reader can tell a quarter from an eighth from a dotted half, on sight.
- Beam grouping reflects the time signature, so 6/8 reads as two groups of
  three.
- A chord takes one stem; two voices on a staff take opposed stems.
- All of it is assertable without a display, with named cases for the beaming
  rules.

**Non-Goals:**

- Beams across the brace. Out of scope in `engraved-notation-renderer` and
  still out of scope here; the existing diagnostic stays.
- Feathered beams, beamed grace notes, cross-beat beaming by `<beam>` elements
  from the file. The model does not carry the file's own beaming and this
  change does not add it: grouping is computed from the metre.
- Tuplet brackets and numbers. Tuplet *durations* are honoured when deciding a
  note's written value (decision 1); drawing the bracket is not here.
- Collision avoidance between a stem and anything else. `engraving-polish`.
- Mid-system signature changes (finding 10). A separate defect, filed and not
  folded in, because it is a correctness bug in the head layout rather than
  anything to do with duration.

## Decisions

### 1. A note's written value is derived from ticks, and the derivation is one function

There is no `<type>` in the model, so the renderer has to answer "how many flags
and how many dots" from `ScoreEvent::duration` alone. That derivation is the
foundation of everything else in this change — the notehead, the flag count, the
beam count, the dots — so it is one function with one test file rather than
arithmetic scattered through the layout.

```
NoteValue { Glyph notehead; int flags; int dots; }
noteValueFor(Tick duration, const TupletRatio& tuplet)
```

Two things make it less obvious than it looks.

**Tuplets.** A triplet eighth lasts two thirds of an eighth. Its *written* value
is still an eighth, so the duration is scaled by `actual/normal` before the
decomposition — otherwise every triplet in every score draws as a dotted
something, which is the kind of wrong that looks like a font problem.

**Dots.** A dotted value is `base * (2 - 2^-dots)`. Rather than solve for dots,
the function walks down from the largest base value that fits and checks whether
one, two or three dots make up the remainder exactly. A duration that is not any
dotted power of two — which a repaired or tuplet-free odd duration can be — gets
the largest value that fits and **a diagnostic**, because drawing it as
something shorter than it is, silently, is exactly the failure mode the
renderer's diagnostic rule exists for.

*Alternative considered:* carry `<type>` through the importer into the model.
Better in the long run and a model change rather than a renderer change, so it
belongs to whoever next touches the importer. It would also not remove this
function: a model is allowed to be repaired, so a renderer still cannot assume
type and duration agree.

### 2. Stem direction is decided per *event*, not per note, and per voice before that

Three rules, applied in order, so that each is a fact about a different scope:

1. **If the staff has more than one voice in this measure**, the upper voice
   stems up and the lower stems down, by `Voice::number`. This beats everything
   below it: two voices sharing a staff with direction chosen note by note is
   unreadable, and a reader uses stem direction as the only thing telling the
   voices apart.
2. **Otherwise, a chord takes the direction of its note furthest from the middle
   line.** One stem for the whole chord, from the outermost notehead.
3. **A single note on or above the middle line stems down, below it stems up.**

The middle line is step 4. "On the line" is the only tie, and it goes down,
which is the convention.

Direction is therefore a property of the `PreparedEvent`, decided once in the
preparation pass where the voice is still in hand, rather than recomputed at
placement time where it is not.

### 3. Beam grouping comes from the time signature, and the groups are a named table

A beaming rule that ignores the metre does not merely look wrong — it
misrepresents what the bar *is*. So the group length is computed from the
prevailing `TimeSignature`:

- **Compound** (`beats` divisible by 3 and `beats > 3`): the group is three
  divisions of `beatType`. 6/8 and 9/8 and 12/8 group in dotted quarters.
- **Simple**: the group is one division of `beatType`. 3/4 groups in quarters,
  2/2 in halves.

A beam runs over consecutive beamable events — flags ≥ 1, not a rest, same
voice, same staff — that fall inside one group. A group boundary breaks a beam
even when the notes continue, which is the whole point.

Two or more notes make a beam; one leaves a flag.

*Alternative considered:* honour the `<beam>` elements the file writes. That is
what the engraver intended and it is strictly better information. The model does
not carry it, and adding it is an importer change with its own round-trip
question, so this change computes instead and the proposal's long-term section
pins the computed rules in named tests so that changing them later is a visible
diff.

### 4. Beam geometry is a quadrilateral, computed once, and stems are shortened to meet it

A beam is placed as four corners in staff-space coordinates, not as "a line from
here to there with a thickness" — the painter then fills a path and makes no
decision. Slope is taken from the first and last notehead of the group and
**clamped**, because an unclamped slope over a wide leap produces a beam at
forty-five degrees that no engraver would set.

Stems in a beamed group do not keep their own length: each is extended or
shortened so its end lands on the beam. That is the arithmetic worth testing
hardest, and the test is that every stem in a group ends exactly on the beam's
line at its own x.

Secondary beams (sixteenths and shorter) are additional quadrilaterals at a
fixed offset inward from the primary, spanning only the notes that carry them.

### 5. Stems and flags take horizontal room, so column widths change

A stem on the right of a notehead and a flag hanging off it occupy space the
current spacing function does not account for. Column width already takes the
larger of the musical spacing and what the glyphs need, so this is a change to
the second input rather than a new mechanism — but it will visibly re-space
every existing page, which is intended rather than a regression.

### 6. Rests stay where they are, with dots

Per-voice rest offsetting — two voices on a staff putting their rests above and
below the middle rather than on top of each other — follows the same voice
information as decision 2 and is included. Rest *dots* follow from decision 1
for free.
