# every-part Specification

## Purpose
TBD - created by archiving change every-part. Update Purpose after archive.
## Requirements
### Requirement: Every part is drawn
The score view SHALL draw every part of the score in each system, top to bottom
in score order, each on its own staves.

A song drawn as its voice alone is missing the accompaniment, which is most of
what a pianist reading it needs.

#### Scenario: A voice and a piano
- **WHEN** a score with a one-staff voice part and a two-staff piano part is
  drawn
- **THEN** each system shows three staves: the voice, then both piano staves

#### Scenario: Nothing is reported missing
- **WHEN** every part of a score can be drawn
- **THEN** no notice says a part was not shown

### Requirement: Moments line up across parts
Notes and rests that begin at the same moment SHALL be drawn at the same
horizontal position on every staff of every part in a system.

#### Scenario: A dense part over a sparse one
- **WHEN** the voice has a run of sixteenths over half notes in the piano
- **THEN** the voice's notes on beats one and three are at the same x as the
  piano's

### Requirement: Each part keeps its own brace and barlines
A part on more than one staff SHALL be braced, and barlines SHALL run through
each part's staves without crossing into the next part.

#### Scenario: Voice over piano
- **WHEN** a voice is drawn above a two-staff piano
- **THEN** only the piano's staves are braced, and each barline is drawn through
  the voice's staff and, separately, through both piano staves

### Requirement: Lyrics make room only below their part
The space reserved for a part's lyrics SHALL be below that part's staves only.

#### Scenario: Words in the voice
- **WHEN** the voice has one verse of lyrics and the piano has none
- **THEN** the gap below the voice allows one lyric row, and the gap between the
  piano's staves allows none

### Requirement: A tempo mark is drawn once
A tempo mark SHALL be drawn once, above the top staff, whichever parts the file
writes it into.

#### Scenario: Written into every part
- **WHEN** both the voice and the piano carry the same tempo mark
- **THEN** it is drawn once, above the voice

#### Scenario: Written only into a lower part
- **WHEN** only the piano carries a tempo mark
- **THEN** it is drawn above the voice

### Requirement: What cannot be drawn is named
A part that cannot be drawn, and bars only some parts have, SHALL be reported in
the view's notices; every other part is still drawn.

#### Scenario: An unpitched part
- **WHEN** a score has a voice and a part written in percussion clef
- **THEN** the voice is drawn, and a notice names the other part and says it is
  not shown

#### Scenario: A part that runs longer
- **WHEN** one part has a bar the others do not
- **THEN** the bars every part has are drawn and a notice counts the rest

