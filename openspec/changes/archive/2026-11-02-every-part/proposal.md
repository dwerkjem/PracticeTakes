## Why

The Score View drew a score's first part and named the rest as not shown. For
the commonest score a practising musician opens -- a song -- that is the voice
line alone, and the piano, which holds most of the music, missing. The sample
score the application now ships (Mahler, "Die zwei blauen Augen") made that the
first thing a new user sees: a melody, and a sentence saying the accompaniment
is not there.

The grand staff (`grand-staff`) already lays out several staves in one system,
spaced together. A second part is the same problem one level up.

## What Changes

- **Every part is drawn**, top to bottom in score order, each on its own staves.
  A part on two staves keeps its brace.
- **One spacing for the whole system**: every staff of every part is spaced
  together, so a moment in the music lands at one x on all of them.
- **Barlines break between parts** and run through each part's own staves; one
  line joins every staff at the system's start.
- **Lyrics push down only the staves below their part**, so a voice's words
  leave no gap inside the piano.
- **A tempo mark is drawn once**, above the top staff, however many parts the
  file wrote it into.
- **What cannot be drawn is still named**: a part on an unpitched staff, and bars
  only some parts have.
- **Placements are kept per part**, because a note's address is only unique
  within its part; ties are resolved against their own part's notes.

## Capabilities

### New Capabilities
- `every-part`: drawing every part of a score in each system, aligned, with
  per-part braces, barlines and lyric rows.

### Modified Capabilities
None: the grand staff's requirements still hold for each part.

## Impact

- `ScoreLayout.h`: `StaffGroup`, `System::staffGroups`,
  `System::staffLyricVerses`.
- `ScoreLayoutEngine.cpp`: parts collected and planned together; directions read
  from each part's own measure; tempo marks lifted to the top staff.
- `ScoreGlyphPainter`: barlines and braces drawn per part.
- **A single part lays out exactly as before.** Every glyph of every part of
  every test score, laid out alone at three widths, was dumped before and after
  and compared: no difference.

## Out of scope

- Brackets for `<part-group>`, which the importer does not read.
- Choosing which parts to show (#33). Every part is shown.
- Part names at the left of each system.
