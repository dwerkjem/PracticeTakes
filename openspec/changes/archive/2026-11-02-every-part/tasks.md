## 1. Layout types

- [x] 1.1 `StaffGroup` and `System::staffGroups`; empty means one group of every
      staff.
- [x] 1.2 `System::staffLyricVerses`, parallel to `staffOffsets`.

## 2. The engine

- [x] 2.1 Collect each part's pitched staves; name a part with none, an omitted
      staff, and a part with nothing in it, using the part's name when there are
      several and "this part" when there is one.
- [x] 2.2 Plan every drawn staff of every part as one list with `partOfStaff`.
- [x] 2.3 `spaceMeasure` reads each plan's own measure for directions and the
      whole-bar rest; the width uses the longest nominal duration.
- [x] 2.4 Lay out the bars every part has, and report the rest.
- [x] 2.5 One placement map per part; ties resolved within their part.
- [x] 2.6 Lyric rows per part, applied below its own staves only.
- [x] 2.7 Brace room when any part has more than one staff.
- [x] 2.8 Tempo marks from lower parts lifted to the top staff, duplicates
      dropped.

## 3. The painter

- [x] 3.1 One line joining every staff at the system start.
- [x] 3.2 A brace per part with more than one staff, spanning only its staves.
- [x] 3.3 Barlines and their repeat lines through one part's staves at a time.
- [x] 3.4 Dynamics placed below their own staff's lyric rows.

## 4. Verification

- [x] 4.1 Single-part identity: every part of every test score, alone, at three
      widths, dumped before and after; no difference.
- [x] 4.2 Tests for staves and groups, alignment across parts, lyric rows, ties
      per part, tempo once, and unequal lengths; each fails under the mutation it
      guards against.
- [x] 4.3 The sample-score surface asks about the voice, the braced piano, the
      barlines, the alignment and the tempo mark.
