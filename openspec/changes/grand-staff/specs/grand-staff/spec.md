## ADDED Requirements

### Requirement: A multi-staff part is drawn as one braced system
When the drawn part has more than one staff, the score view SHALL draw all of
its pitched staves in each system, top to bottom, joined at the left by a brace.

A piano part drawn as its right hand alone is half of the music, and the half a
reader is most likely to need is the one missing.

#### Scenario: A piano part
- **WHEN** a part with two staves is drawn
- **THEN** each system shows both staves, the second below the first, with a
  brace joining them at the left

#### Scenario: Nothing about the lower staff is reported missing
- **WHEN** both staves of a two-staff part are drawn
- **THEN** no notice says a staff was not shown

### Requirement: Notes at the same moment line up across staves
Notes and rests that begin at the same moment SHALL be drawn at the same
horizontal position on every staff of a system.

Each staff could be spaced correctly on its own and still disagree with the
other about where the beats fall, which reads as two rhythms.

#### Scenario: Both hands on the beat
- **WHEN** both staves of a measure have a note on beat one and beat three
- **THEN** each beat's notes on the two staves are drawn at the same x

#### Scenario: Room needed on one staff only
- **WHEN** a note on the upper staff carries an accidental and the note at the
  same moment on the lower staff does not
- **THEN** both notes move right together, and stay aligned

#### Scenario: A note on one staff only
- **WHEN** the lower staff has a note at a moment where the upper staff has
  none
- **THEN** that note is placed where that moment falls on the shared spacing,
  between the upper staff's notes either side of it

### Requirement: Barlines run through every staff
Each barline SHALL be drawn once per system, from the top line of the first
staff to the bottom line of the last, crossing the space between them.

#### Scenario: A barline in a piano part
- **WHEN** a measure of a two-staff part ends
- **THEN** one barline is drawn through both staves and the gap between them

### Requirement: Each staff keeps its own clef and accidentals
Each staff SHALL be drawn in its own clef, and an accidental SHALL apply only to
later notes on its own staff within the measure.

#### Scenario: A treble and a bass staff
- **WHEN** a two-staff part puts a treble clef on the first staff and a bass
  clef on the second
- **THEN** each staff is drawn in its own clef, and its notes are placed on the
  lines that clef names

#### Scenario: A clef change on one staff
- **WHEN** the lower staff changes clef mid-system and the upper does not
- **THEN** only the lower staff draws the new clef, and the notes after the
  change still line up with the upper staff's

#### Scenario: An accidental on one staff
- **WHEN** an F sharp is drawn on the upper staff and an F in the same octave
  follows on the lower staff in the same measure
- **THEN** the lower staff's F is read without the sharp, and carries one only if
  it is itself sharp

### Requirement: A single-staff score is drawn as before
A part with one staff SHALL be laid out exactly as it was before multi-staff
systems existed.

#### Scenario: The same numbers
- **WHEN** a single-staff score is laid out at a given width
- **THEN** every glyph's position is the same as before this change, and no
  brace is drawn

### Requirement: A staff that cannot be drawn is reported, not the part
A staff written in a clef without pitches SHALL be left out of an otherwise
pitched part, with a notice naming it. The part SHALL be refused only when none
of its staves can be drawn.

#### Scenario: A percussion staff inside a pitched part
- **WHEN** a two-staff part has a treble staff and a percussion staff
- **THEN** the treble staff is drawn and a notice says the other was not
