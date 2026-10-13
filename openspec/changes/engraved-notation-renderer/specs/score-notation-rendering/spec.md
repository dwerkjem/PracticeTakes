## ADDED Requirements

### Requirement: Score layout is computed without a GUI
The engraving layer SHALL turn a `Score` and an available width into a laid-out
result — systems, staves, and the position of every drawn element — using no
JUCE types and including no `JuceHeader.h`, so that it compiles into
`PracticeTakesTests` and every layout decision is assertable without a display.
Positions SHALL be expressed in staff spaces, with any conversion to pixels
performed by the painting code and not by the layout layer.

#### Scenario: Layout compiles and runs in the test target
- **WHEN** the engraving sources are added to `PracticeTakesTests`
- **THEN** they compile and link without `JuceHeader.h` or any GUI module, and
  layout can be computed and inspected in a test with no display attached

#### Scenario: The painting surface makes no layout decision
- **WHEN** the score view paints a laid-out score
- **THEN** it draws the elements at the positions the layout layer produced, and
  computes no position, width, or staff assignment of its own

#### Scenario: Positions are scale-independent
- **WHEN** the same score is laid out and then drawn at two different staff
  sizes
- **THEN** the layout result is identical in both cases, and only the staff
  space to pixel scale applied at paint time differs

### Requirement: An imported score renders as staff notation
The score view SHALL draw the open score as conventional staff notation: five
staff lines per staff, barlines at measure boundaries, a clef, key signature and
time signature at the start of each system, and a notehead for every pitched
event positioned on the staff degree its written spelling and the prevailing
clef determine.

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

### Requirement: Simultaneous events across staves align vertically
The layout SHALL assign every event of every staff in a system to a column keyed
by its absolute onset tick, and every event sharing an absolute onset SHALL be
placed at the same horizontal position regardless of which staff or part it
belongs to.

#### Scenario: A piano chord aligns across the grand staff
- **WHEN** a two-staff piano part has a treble event and a bass event at the
  same absolute onset
- **THEN** both are laid out at the same horizontal position

#### Scenario: Unequal voices stay aligned at shared onsets
- **WHEN** one staff holds four quarter notes and the other holds eight eighth
  notes in the same measure
- **THEN** each quarter note aligns with the eighth note at its own onset, and
  the eighth notes between them fall between those positions

#### Scenario: Horizontal space reflects duration without being proportional to it
- **WHEN** a measure contains a half note followed by two quarter notes
- **THEN** the half note is allotted more horizontal space than either quarter
  note and less than the two of them together

### Requirement: A multi-staff part renders as one braced system
A part whose `staffCount` is greater than one SHALL contribute that many staves
to every system, drawn as a single braced group, and SHALL be visually distinct
from two separate single-staff parts.

#### Scenario: A piano part is braced
- **WHEN** a part declares two staves
- **THEN** its two staves are drawn joined by a brace spanning both

#### Scenario: A vocal part is not braced
- **WHEN** a part declares one staff
- **THEN** no brace is drawn for it

#### Scenario: Two separate parts are not braced together
- **WHEN** a score holds two single-staff parts
- **THEN** each is drawn as its own staff and no brace joins them to each other

### Requirement: Events render on the staff the file assigned them
Every event SHALL be drawn on the staff named by `ScoreEvent::staff` within its
part. The renderer SHALL NOT infer a staff from pitch, from voice number, or
from any other property.

#### Scenario: Treble and bass contents land where the file put them
- **WHEN** a two-staff piano part has events on staff 1 and staff 2
- **THEN** each event is drawn on the staff its own `staff` field names

#### Scenario: A low note written on the treble staff stays there
- **WHEN** an event on staff 1 carries a pitch below the treble staff
- **THEN** it is drawn on staff 1 on ledger lines, and is not relocated to
  staff 2

### Requirement: Clef state is tracked per staff
Each staff of each part SHALL carry its own running clef, advanced only by the
`Clef` entries that name that staff. A clef change on one staff SHALL NOT alter
the clef in force on any other staff.

#### Scenario: A mid-score clef change affects one staff only
- **WHEN** the bass staff of a piano part changes from bass to treble clef at a
  measure
- **THEN** that staff draws the new clef and places its subsequent notes against
  it, and the treble staff's clef and note placement are unchanged

#### Scenario: Each staff opens in its own clef
- **WHEN** a piano part declares a G clef on staff 1 and an F clef on staff 2
- **THEN** each staff draws its own clef at the start of every system

### Requirement: Notation that cannot be drawn is reported
Where the renderer encounters notation it does not draw, it SHALL record a
diagnostic naming the measure and the construct, and SHALL make those
diagnostics available to the user through the score view. It SHALL NOT place an
element at a position the source does not support, and SHALL NOT omit content
silently.

#### Scenario: A cross-staff beam is reported, and its notes are still placed
- **WHEN** a voice's events change staff within a measure, so a beam would cross
  the brace
- **THEN** each event is drawn on the staff it names, the beam is not drawn, and
  a diagnostic naming that measure records that the beam was omitted

#### Scenario: Diagnostics are reachable from the view
- **WHEN** a layout produces one or more diagnostics
- **THEN** the score view indicates that they exist and provides a way to read
  them

#### Scenario: A clean score reports nothing
- **WHEN** a score contains only notation this pass draws
- **THEN** the layout produces no diagnostics and the view shows no indication
  of any

### Requirement: The score view is a tool like any other
The score view SHALL be declared in the built-in tool catalog and built through
the tool registry, SHALL read the open score only through
`ToolServices::currentScore`, and SHALL detect a newly opened score by comparing
the score pointer it last laid out against the current one.

#### Scenario: The view opens from the Tools menu
- **WHEN** the user opens the score view from the Tools menu
- **THEN** it opens, docks, floats, tabs, themes, and restores from a saved
  workspace with no shell code naming it

#### Scenario: A newly opened score is picked up
- **WHEN** a different score is published while the view is open
- **THEN** the view observes that the score pointer differs from the one it last
  laid out, lays out the new score, and redraws

#### Scenario: The view never reaches into the shell
- **WHEN** the view needs the open score
- **THEN** it reads it from the `ToolServices` bundle it was constructed with,
  and does not reference `MainComponent` or any other tool
