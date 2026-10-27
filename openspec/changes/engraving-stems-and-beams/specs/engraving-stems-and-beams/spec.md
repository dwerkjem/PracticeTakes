## ADDED Requirements

### Requirement: Notes shorter than a quarter are drawn with beams where they group
The application SHALL join the stems of notes that belong to one beam group
with beams, and SHALL draw a flag on every note shorter than a quarter that no
beam reaches. A note SHALL NOT carry both.

#### Scenario: Two eighths in one beat
- **WHEN** two eighth notes of one voice share a beat
- **THEN** their stems are joined by one beam and neither carries a flag

#### Scenario: An eighth alone in its beat
- **WHEN** an eighth note is the only beamable note in its beat
- **THEN** it keeps its flag, which is how a reader tells it from a quarter

#### Scenario: Shorter values carry more beams
- **WHEN** two sixteenth notes share a beat
- **THEN** their stems are joined by two beams, the second stacked away from the
  noteheads, and each stem reaches the outer one

### Requirement: Beam groups are chosen from the time signature
The application SHALL group notes for beaming by the metre in force, and SHALL
NOT beam a run across a group boundary. The grouping SHALL come from the time
signature rather than from any beaming the source file carried, and the
application SHALL say so.

#### Scenario: A compound metre groups by the dotted beat
- **WHEN** six eighth notes are drawn in 6/8
- **THEN** they are beamed as two groups of three

#### Scenario: A simple metre groups by the beat
- **WHEN** the same six eighth notes are drawn in 3/4
- **THEN** they are beamed as three groups of two, because beaming them 3+3
  would tell the reader the bar is 6/8

#### Scenario: Cut time groups by the half
- **WHEN** eight eighth notes are drawn in 2/2
- **THEN** they are beamed as two groups of four

#### Scenario: A metre that cannot be read
- **WHEN** the time signature has a non-positive beats or beat type
- **THEN** nothing is beamed, rather than everything being beamed together

#### Scenario: The file's own beaming
- **WHEN** any beam is drawn
- **THEN** the layout reports that beam groups were chosen from the time
  signature and that the source file's own beaming was not read

### Requirement: A beam group breaks where the music does
The application SHALL break a beam group at anything that is not beamed, at a
gap in time, and at a barline, and SHALL NOT beam across two voices. A group of
one note SHALL be drawn as a flag.

#### Scenario: A rest inside a beat
- **WHEN** a rest falls between two beamable notes of one beat
- **THEN** the run breaks at the rest, and the layout reports that a beam is not
  carried over one

#### Scenario: A note too long to beam
- **WHEN** a quarter note or longer falls between two eighths
- **THEN** the run breaks at it

#### Scenario: Silence between two notes
- **WHEN** two beamable notes in one beat are not adjacent in time
- **THEN** they are not beamed together

#### Scenario: A barline
- **WHEN** a beat's worth of eighths ends one measure and another begins the next
- **THEN** each measure's notes form their own group and no beam crosses the
  barline

#### Scenario: Two voices on one staff
- **WHEN** two voices each hold beamable notes in the same beat
- **THEN** each voice is beamed separately, and the two groups take opposite
  stem directions

### Requirement: A beam group takes one stem direction
The application SHALL give every note of a beam group the same stem direction,
chosen by the note furthest from the middle line, because a beam joins stems
that point the same way.

#### Scenario: A group spanning the middle line
- **WHEN** a group holds a note below the middle line and one above it
- **THEN** both stems point the same way, and that way is the one the note
  furthest from the middle line takes on its own

### Requirement: A beam is straight, and the stems under it are long enough to read
The application SHALL draw each beam as a straight bar, SHALL limit how far it
rises or falls across a group, and SHALL keep every stem in the group long
enough to read as a stem and clear of its chord's far notehead.

#### Scenario: A beam follows the shape of its notes
- **WHEN** a group rises in pitch
- **THEN** the beam slopes with it

#### Scenario: A wide leap inside a group
- **WHEN** a group's outer notes are far apart in pitch
- **THEN** the beam's rise is clamped, so the bar does not become a diagonal

#### Scenario: A note that would poke through the beam
- **WHEN** a note inside a group lies beyond the line its outer notes imply
- **THEN** the whole beam moves away from the staff, rather than one stem being
  shortened and the bar bent

### Requirement: A beam level with no neighbour is drawn as a partial beam
The application SHALL draw a stub rather than a full bar for a beam level
carried by a single note of a group, and SHALL point it at the note before it
where there is one.

#### Scenario: A sixteenth after a dotted eighth
- **WHEN** a dotted eighth and a sixteenth share a beat
- **THEN** one beam joins them and the second is a stub on the sixteenth's stem,
  reaching back towards the note before it and not as far as that note's stem

#### Scenario: A sixteenth first in its group
- **WHEN** the lone shorter note begins the group
- **THEN** its stub points forward instead

### Requirement: The room between two onsets clears a notehead and its flag
The application SHALL leave at least a notehead's and a flag's width between two
successive onsets, so a flagged note cannot touch what follows it.

#### Scenario: Two flagged eighths in successive beats
- **WHEN** two notes carrying flags are placed one after the other
- **THEN** the distance between their noteheads is at least the notehead width
  plus the flag width, whatever the configured minimum slot width says

## MODIFIED Requirements

### Requirement: What is not drawn is reported
The layout SHALL carry a sentence for every construct in the open score it does
not draw. Beaming SHALL no longer be among them; what remains of it SHALL be.

#### Scenario: Beaming is no longer a gap
- **WHEN** a score holding runs of eighths is drawn
- **THEN** the layout does not report that eighths are drawn with flags rather
  than beamed

#### Scenario: A run of grace notes
- **WHEN** two or more grace notes fall together
- **THEN** they are drawn with flags and the layout reports that grace notes are
  not beamed
