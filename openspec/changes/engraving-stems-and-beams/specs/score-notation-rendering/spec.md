## MODIFIED Requirements

### Requirement: An imported score renders as staff notation
The score view SHALL draw the open score as conventional staff notation: five
staff lines per staff, barlines at measure boundaries, a clef, key signature and
time signature at the start of each system, and a notehead for every pitched
event positioned on the staff degree its written spelling and the prevailing
clef determine.

Every notehead shorter than a whole note SHALL additionally carry a **stem**,
and every note shorter than a quarter SHALL carry either a **flag** or a
**beam**, so that a reader can tell one duration from another without counting
anything.

#### Scenario: A score is drawn after import
- **WHEN** a score is imported and published to `CurrentScore` while the score
  view is open
- **THEN** the view draws that score's staves, barlines, clefs, key and time
  signatures, and noteheads, replacing whatever it was showing before

#### Scenario: Notehead placement follows the written spelling
- **WHEN** an event carries a note whose written spelling is F sharp in treble
  clef
- **THEN** its notehead is placed on the staff degree for F, not on the degree
  for G, and a sharp is drawn before it when the prevailing key signature does
  not already supply one

#### Scenario: Nothing is drawn before a score is open
- **WHEN** the score view is open and no score has been published
- **THEN** it shows an empty state rather than an empty staff or a blank panel,
  and does not fail

#### Scenario: Durations are distinguishable on sight
- **WHEN** a measure holds a quarter note, an eighth note and a dotted half note
- **THEN** the quarter has a stem and no flag, the eighth has a stem and either
  a flag or a beam, and the dotted half has a stem and an augmentation dot, so
  that no two of the three are drawn identically

## ADDED Requirements

### Requirement: A note's written value is derived from its duration
The layout SHALL derive the notehead, flag count and dot count of an event from
its duration in ticks, scaled by its tuplet ratio, since the model carries no
notated note type. A duration that is not a dotted power-of-two value SHALL be
drawn as the largest value that fits **and** reported as a diagnostic, rather
than silently drawn as something shorter than it lasts.

#### Scenario: A triplet eighth is written as an eighth
- **WHEN** an event lasts two thirds of an eighth note and carries a tuplet
  ratio of three in the time of two
- **THEN** it is drawn as an eighth note, not as a dotted or shortened value

#### Scenario: A dotted value is drawn with its dots
- **WHEN** an event lasts a half note plus a quarter note
- **THEN** it is drawn as a half note with one augmentation dot

#### Scenario: An undrawable duration is reported
- **WHEN** an event's duration is not any value a notehead and dots can express
- **THEN** the largest value that fits is drawn and a diagnostic names the
  measure, rather than the difference going unmentioned

### Requirement: Stem direction follows voice, then chord, then staff position
The layout SHALL give every stemmed event one stem, whose direction is decided
by the first of these that applies: the event's voice when the staff carries
more than one voice in that measure, the chord's note furthest from the middle
line, or the single note's own position with the middle line stemming down.

A chord SHALL take exactly one stem, drawn from its outermost notehead, never
one per note.

#### Scenario: Two voices on one staff oppose
- **WHEN** a staff carries two voices in a measure
- **THEN** every stem of the upper voice points up and every stem of the lower
  voice points down, whatever each individual note's position would otherwise
  give

#### Scenario: A chord takes one stem
- **WHEN** an event carries three notes
- **THEN** one stem is drawn, from the notehead furthest from the middle line,
  and the chord's other noteheads carry none

#### Scenario: A note on the middle line stems down
- **WHEN** a single note sits on the middle line of its staff
- **THEN** its stem points down

### Requirement: Beams group by the prevailing time signature
The layout SHALL beam consecutive beamable events — flagged, not rests, in one
voice on one staff — that fall within one beat group, and SHALL break the beam
at every group boundary. The group SHALL be three divisions of the beat type in
a compound signature and one division in a simple one. A signature is compound
when its beat count divides by three and either exceeds three or its beat type
is an eighth or shorter — so 6/8, 9/8 and 3/8 are compound and 3/4 is not. A
group of one event SHALL keep its flag rather than becoming a beam of one.

#### Scenario: Six-eight reads as two groups of three
- **WHEN** a 6/8 measure holds six eighth notes in one voice
- **THEN** they are drawn as two beamed groups of three, not as one group of six
  or three groups of two

#### Scenario: Three-four reads as three groups
- **WHEN** a 3/4 measure holds six eighth notes in one voice
- **THEN** they are drawn as three beamed groups of two

#### Scenario: Three-eight is one group, not three
- **WHEN** a 3/8 measure holds three eighth notes in one voice
- **THEN** they are drawn as one beamed group of three, where the same beat
  count over a quarter-note beat — 3/4 — would group by the quarter

#### Scenario: A lone short note keeps its flag
- **WHEN** an eighth note is the only beamable event in its beat group
- **THEN** it is drawn with a flag and no beam

#### Scenario: A rest breaks a beam
- **WHEN** a beat group holds an eighth note, an eighth rest and an eighth note
- **THEN** no beam joins the two notes across the rest

### Requirement: A beam is positioned by the layout, not by the painter
The layout SHALL express every beam as corner positions in staff spaces, with
its slope already computed and clamped, and SHALL adjust the length of every
stem in a beamed group so that it ends on the beam. The painting surface SHALL
compute no slope, no stem length and no corner of its own.

#### Scenario: Stems meet the beam they belong to
- **WHEN** a group of notes at differing pitches is beamed
- **THEN** each stem's far end lies on the beam at that stem's own horizontal
  position

#### Scenario: A wide leap does not produce a steep beam
- **WHEN** a beamed group's first and last notes are an octave apart
- **THEN** the beam's slope is clamped rather than following the full interval

#### Scenario: Sixteenths carry a second beam
- **WHEN** a beamed group holds sixteenth notes
- **THEN** a second beam is drawn inside the first, spanning only the events
  that carry it
