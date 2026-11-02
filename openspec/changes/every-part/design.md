## Context

`layOutScore` planned one part's staves, spaced each measure across them with
`spaceMeasure`, and emitted them into systems with `staffOffsets`. Every piece
of that was already written for N staves; only the choice of which staves to
plan was tied to one part.

## Decisions

### 1. Plan every staff of every part as one list
The staves of all parts form one list in score order, with a parallel
`partOfStaff`. `spaceMeasure` then aligns them exactly as it aligns the two
hands of a piano, and every later step that loops over staves is unchanged.

### 2. Each staff reads its own part's measure
`spaceMeasure` read directions and the whole-bar-rest rule from the first
staff's measure, which was right while every staff was one part's. With several
parts, a piano's tempo or dynamics were silently dropped. Each plan now reads
`plan->source`. The measure's width uses the longest of the parts' nominal
durations.

### 3. Placements per part
`score::NoteRef` addresses a note within its part, so the first note of every
part has the same ref. One map would let one part's placement overwrite
another's, and a tie would be drawn on the wrong staff. One map per part.

### 4. Staff groups, not a brace flag
`System::staffGroups` lists each part's first and last staff. The painter braces
a group of more than one staff and draws each barline through one group at a
time. An empty list means one group of every staff, so a system built the old
way paints as it did.

### 5. Lyric rows per staff
A part's lyric verse count is applied below each of its staves, as it was for a
single part's staves, and nowhere else. A single part therefore lays out
identically; a voice over a piano gains no gap inside the piano.

### 6. Tempo marks once, on top
Exporters write a tempo mark into every part. Marks from below the first part
are moved to staff 0 and dropped where staff 0 already has the same text at the
same x. The first part's staves are not touched, which keeps a single part
identical.

### 7. Parts of unequal length
Systems hold the bars every part has; the rest are counted in a notice rather
than drawn against nothing.

## Risks

- **Tall systems.** An eleven-part motet stacks eleven staves per system. The
  view scrolls, so it is long rather than broken.
- **Dynamics and lyrics share the space under a voice**, as they did before.
  Collision avoidance is `engraving-polish`'s.
