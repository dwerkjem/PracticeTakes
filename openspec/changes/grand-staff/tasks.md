## 1. Layout types

- [ ] 1.1 `int staff = 0` on every positioned glyph in `ScoreLayout.h`.
- [ ] 1.2 `System::staffOffsets`, each staff's top line in staff spaces below
      `topOfStaff`; one entry of zero for a single staff.
- [ ] 1.3 `LayoutOptions::interStaffGap` and `braceWidth`.

## 2. The engine

- [ ] 2.1 Split `planMeasure` at its existing phase boundary: a per-staff
      vertical phase, and `spaceMeasure` taking every staff's plan for one
      measure.
- [ ] 2.2 `spaceMeasure`: one slot list from the union of the staves' onsets,
      each slot's padding the largest any staff needs; every staff's events and
      directions placed from it; one content width for the measure.
- [ ] 2.3 Plan every pitched staff of the part; omit an unpitched staff with a
      notice; refuse the part only when no staff is pitched.
- [ ] 2.4 Count events on staves that are not drawn, and only those.
- [ ] 2.5 Header width the widest of the staves'; content starting at the same
      x on every staff.
- [ ] 2.6 Barlines, the opening repeat barline and the measure number emitted
      once per measure, from the first staff.
- [ ] 2.7 Staff offsets and system height from `interStaffGap` and each staff's
      lyric rows; brace room reserved on braced systems.
- [ ] 2.8 Ties resolved across every staff's plans, each drawn on its first
      note's staff.
- [ ] 2.9 Remove the notice that only the top staff is shown.

## 3. The painter

- [ ] 3.1 Every glyph drawn at its staff's offset.
- [ ] 3.2 Staff lines per staff; barlines from the first staff's top line to the
      last staff's bottom line.
- [ ] 3.3 The SMuFL brace, stretched to the staves' span, and a systemic barline
      at the start of a braced system.

## 4. Tests

- [ ] 4.1 Single-staff layouts identical to before, glyph for glyph.
- [ ] 4.2 Two staves: both drawn, the second below the first, a brace, no
      notice about a missing staff.
- [ ] 4.3 Alignment: notes at the same onset at the same x; an accidental on
      one staff moving both; a note on one staff only falling between.
- [ ] 4.4 Per staff: each staff's own clef; a clef change on the lower staff
      alone; an accidental not carried to the other staff.
- [ ] 4.5 Barlines once per measure per system, not once per staff.
- [ ] 4.6 A percussion staff in a pitched part omitted with a notice.
- [ ] 4.7 The embedded Bravura has the brace glyph.

## 5. Seen on screen

- [ ] 5.1 Capture a piano score in the score view and check it: both hands,
      braced, beats aligned.
