## 1. Layout types

- [x] 1.1 `int staff = 0` on every positioned glyph in `ScoreLayout.h`.
- [x] 1.2 `System::staffOffsets`, each staff's top line in staff spaces below
      `topOfStaff`; one entry of zero for a single staff.
- [x] 1.3 `LayoutOptions::interStaffGap` and `braceWidth`.

## 2. The engine

- [x] 2.1 Split `planMeasure` at its existing phase boundary: a per-staff
      vertical phase, and `spaceMeasure` taking every staff's plan for one
      measure.
- [x] 2.2 `spaceMeasure`: one slot list from the union of the staves' onsets,
      each slot's padding the largest any staff needs; every staff's events and
      directions placed from it; one content width for the measure.
- [x] 2.3 Plan every pitched staff of the part; omit an unpitched staff with a
      notice; refuse the part only when no staff is pitched.
- [x] 2.4 Count events on staves that are not drawn, and only those.
- [x] 2.5 Header width the widest of the staves'; content starting at the same
      x on every staff.
- [x] 2.6 Barlines, the opening repeat barline and the measure number emitted
      once per measure, from the first staff.
- [x] 2.7 Staff offsets and system height from `interStaffGap` and each staff's
      lyric rows; brace room reserved on braced systems.
- [x] 2.8 Ties resolved across every staff's plans, each drawn on its first
      note's staff.
- [x] 2.9 Remove the notice that only the top staff is shown.

## 3. The painter

- [x] 3.1 Every glyph drawn at its staff's offset.
- [x] 3.2 Staff lines per staff; barlines from the first staff's top line to the
      last staff's bottom line.
- [x] 3.3 The SMuFL brace, stretched to the staves' span, and a systemic barline
      at the start of a braced system.

## 4. Tests

- [x] 4.1 Single-staff layouts identical to before, glyph for glyph. Proven
      once rather than asserted forever: every glyph of every single-staff
      corpus score, at three widths and 17 significant digits, dumped from the
      engine before and after the change -- 110,863 lines, byte-identical, the
      same SHA-256. A golden file of that size would fail on every deliberate
      layout change after this one, so what is committed is the shape that
      guarantees it: one staff at offset zero, unbraced, every glyph on staff 0.
- [x] 4.2 Two staves: both drawn, the second below the first, a brace, no
      notice about a missing staff.
- [x] 4.3 Alignment: notes at the same onset at the same x; an accidental on
      one staff moving both; a note on one staff only falling between.
      Mutation-tested: with each staff spaced on its own -- the bug this change
      exists to prevent -- the alignment tests fail. Plain quarters over halves
      align by coincidence under independent spacing too, so the beat test uses
      a sixteenth-note run over half notes, which does not.
- [x] 4.4 Per staff: each staff's own clef; a clef change on the lower staff
      alone; an accidental not carried to the other staff.
- [x] 4.5 Barlines once per measure per system, not once per staff.
- [x] 4.6 A percussion staff in a pitched part omitted with a notice.
- [x] 4.7 The embedded Bravura has the brace glyph. Registered in
      `smufl::allGlyphs()`, which the font builds outlines from and the
      every-glyph test walks: unregistered, the brace drew nothing, silently.

## 5. Seen on screen

- [x] 5.1 Capture a piano score in the score view and check it: both hands,
      braced, beats aligned. `score-view-grand-staff`, from
      `grand-staff-sample.musicxml`. The first capture showed no brace (the
      glyph was unregistered) and then a hairline one (stretched vertically
      only); both fixed and recaptured. The fixture holds no dynamic, for the
      importer reason `notation-sample.musicxml` gives.
