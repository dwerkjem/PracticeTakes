## Context

`layOutScore` draws `drawnStaff = 1` of `score->parts.front()`. Everything the
engine does is per staff and per measure:

- `planPart(part, staff)` walks the measures, tracking the clef for that staff
  and the key and time for the part, and calls `planMeasure` for each.
- `planMeasure` does everything for one staff of one measure: vertical position
  and accidentals, stem direction and beam groups, **then horizontal spacing**
  — one slot per distinct onset, each slot's x from its onset and the room its
  accidentals and grace notes need — then directions placed at their slot.
- `emitMeasure` writes one staff's glyphs into a `System`, *including* the
  things that belong to the whole system: barlines, the opening repeat barline,
  and the measure number.
- `System` is one staff: a `topOfStaff`, and glyph lists positioned in staff
  steps relative to it.

So the parts that are naturally per staff — pitch placement, accidentals,
stems, beams — already are. What is not is horizontal spacing, which has to be
shared, and the system-wide glyphs, which must be emitted once.

## Goals / Non-Goals

**Goals**

- Every pitched staff of the drawn part, in one system, braced.
- Notes at the same onset at the same x on every staff.
- Single-staff output identical to today, number for number.

**Non-Goals**

- Part selection (#33), cross-staff beams and chords, placement refinements.

## Decisions

### 1. Plan each staff vertically, then space each measure across all staves

`planMeasure` splits at the line it already has between its phases:

- **Vertical, per staff** — everything up to and including stem direction and
  beam groups. Unchanged in substance; it is the existing code.
- **Horizontal, per measure, across staves** — a new `spaceMeasure` that takes
  every staff's plan for one measure and builds *one* list of slots from the
  union of their onsets.

A slot's left padding is the largest any staff needs at that onset, so a sharp
in the right hand pushes the left hand's note at the same onset along with it,
and they stay aligned. Each slot's x then comes from the existing rule. Every
staff's events take their slot's x, and grace notes keep their own staff's
count. Measure content width is the same for every staff, because it is derived
from the shared slots.

With one staff, the union of onsets is that staff's onsets and the largest
padding is its own, so `spaceMeasure` produces exactly what `planMeasure`
produced before. That is what keeps single-staff output identical, and a test
pins it.

### 2. A shared header width, and content that starts at the same x

A measure's header — clef, key, time — can differ between staves: the lower
staff can change clef while the upper does not. Headers are therefore sized to
the widest staff's, and every staff's content starts at the same x. A staff
with a narrower header gets the difference as space, which is what an engraver
does.

### 3. System-wide glyphs are emitted once

`emitMeasure` gains the staff it is emitting. Barlines, the opening repeat
barline and the measure number are written only for the first staff, and the
painter draws a barline from the top line of the first staff to the bottom line
of the last. Writing them per staff would draw every barline twice, and still
not join the staves.

### 4. Glyphs carry a staff index; systems carry staff offsets

Every positioned glyph gains `int staff = 0`, the index of the staff it sits on
within its system, and `System` gains `staffOffsets`: each staff's top line, in
staff spaces below `topOfStaff`. The painter adds a glyph's staff offset to its
y, and otherwise draws exactly as it does now.

**Additive, rather than a list of staves inside each system.** The alternative
is cleaner on paper and touches every consumer and every test that reads
`system.noteheads`. Additive fields default to staff 0 at offset 0, so a
single-staff layout is read exactly as before and the change stays where it
belongs.

### 5. The gap between staves holds what hangs below the upper staff

Each staff after the first starts `interStaffGap` below the previous staff's
bottom line, plus a lyric row per verse of the staff above. Dynamics already
hang below their own staff, so the gap between the hands is where a piano part's
dynamics fall, as printed.

### 6. The brace is a SMuFL glyph, stretched

SMuFL's `brace` (U+E000) is drawn scaled vertically to span the first staff's
top line to the last staff's bottom line, at the left of the system, followed by
a systemic barline joining the staves. Room for both is reserved at the start of
every braced system. A test checks the embedded Bravura has the glyph, so its
absence would fail the suite rather than draw nothing.

### 7. Accidentals, stems, beams and ties stay per staff

Each staff has its own `MeasureAccidentals`, so an accidental applies to its own
staff — the convention. Stem directions and beam groups are resolved per staff,
as now. Ties resolve through `NoteRef`, which is already unique across staves,
and each tie is drawn on its first note's staff.

### 8. An unpitched staff is left out, not the whole part

A part is refused only when none of its staves is pitched. A staff in a
percussion clef inside an otherwise pitched part is omitted, and a notice says
which.

## Risks

- **The refactor is in the hottest file in the score view.** Mitigated by
  splitting at an existing phase boundary rather than rewriting either phase,
  and by the identical-output test for single-staff scores.
- **Glyph consumers that ignore the staff index** would draw every lower-staff
  glyph on the upper staff. Today the painter is the only consumer of positions;
  the long-term compatibility note in the proposal is for whatever comes next.
