## ADDED Requirements

### Requirement: An imported score is drawn as notation
The application SHALL draw the currently open score as musical notation on a
staff, in a tool the user can dock, float and tab like any other. The tool SHALL
follow the score the application has open, without the user asking it to
reload.

#### Scenario: A score is open
- **WHEN** a supported MusicXML file has been imported
- **THEN** the tool draws its first part on a staff, with the staff lines, the
  clef, the key signature, the time signature, the notes, the rests and the
  barlines the file specifies

#### Scenario: No score is open
- **WHEN** no score has been imported
- **THEN** the tool says so and names the command that opens one, and does not
  draw an empty staff

#### Scenario: A different score is opened
- **WHEN** a score is being drawn and the user opens a different file
- **THEN** the tool draws the new score, and the previously drawn score remains
  valid for as long as anything holds it

#### Scenario: A score with no notes
- **WHEN** a file imports successfully but holds no pitched note
- **THEN** the tool says so rather than drawing a blank staff

### Requirement: A note is drawn on the staff line its spelling names
The application SHALL place each note by its written spelling and the clef in
force, not by its sounding pitch number. Notes outside the staff SHALL carry
ledger lines.

#### Scenario: Enharmonic spellings are drawn differently
- **WHEN** a C-sharp and a D-flat in the same octave are drawn
- **THEN** they are on different staff positions, each with its own accidental

#### Scenario: A clef change
- **WHEN** a measure changes the clef
- **THEN** the new clef is drawn at that measure and every note after it is
  placed by the new clef

#### Scenario: A clef that reads an octave away from where it sounds
- **WHEN** the clef carries an octave change, as a vocal tenor clef does
- **THEN** notes are drawn where they read, not an octave away

#### Scenario: A note above or below the staff
- **WHEN** a note lies beyond the staff
- **THEN** it carries one ledger line for each line position it is beyond

#### Scenario: A staff with no pitch mapping
- **WHEN** the clef is a percussion, tablature or `none` clef
- **THEN** the staff is not drawn and the application says why, rather than
  placing the notes arbitrarily

### Requirement: A note is drawn with the written value its duration names
The application SHALL draw each note and rest with the notehead, flags and
augmentation dots its duration corresponds to, accounting for any tuplet ratio.
A duration that is not a notatable value SHALL be reported.

#### Scenario: Plain and dotted values
- **WHEN** a half note, a dotted quarter and a sixteenth are drawn
- **THEN** each has the notehead, flag count and dot count that value is written
  with

#### Scenario: A tuplet
- **WHEN** a note belongs to a triplet
- **THEN** it is drawn with the written value of the triplet, not with the value
  its raw duration would suggest

#### Scenario: A duration no written value matches
- **WHEN** a note's duration corresponds to no notatable value
- **THEN** the application reports it rather than drawing a value that is wrong

### Requirement: Accidentals are drawn where a reader needs them
The application SHALL draw an accidental on a note whose alteration differs from
the alteration in force for that step and octave, where the alteration in force
is set by the key signature at the start of each measure and then by any
accidental already drawn in that measure.

#### Scenario: An accidental outside the key
- **WHEN** a note is altered in a way the key signature does not provide
- **THEN** an accidental is drawn on it

#### Scenario: The same accidental twice in a bar
- **WHEN** a note repeats an alteration already drawn in that measure for the
  same step and octave
- **THEN** no second accidental is drawn

#### Scenario: Cancelling an accidental
- **WHEN** a note is natural in a measure where that step was altered, by the
  key signature or by an earlier accidental
- **THEN** a natural sign is drawn

#### Scenario: A new measure
- **WHEN** a measure begins
- **THEN** accidentals from the previous measure no longer apply, and the key
  signature's alterations apply again

#### Scenario: A note held from a tie
- **WHEN** a note is the continuation of a tie
- **THEN** no accidental is drawn on it

### Requirement: The key signature and time signature are drawn
The application SHALL draw the key signature's accidentals in the conventional
order and position for the clef in force, and the time signature where it
changes.

#### Scenario: Sharps and flats
- **WHEN** a key signature of sharps or flats is in force
- **THEN** its accidentals are drawn in the conventional order, at the positions
  that clef places them

#### Scenario: A key with no accidentals
- **WHEN** the key signature has neither sharps nor flats
- **THEN** no key signature accidentals are drawn

#### Scenario: A change part way through
- **WHEN** a measure changes the key or the time signature
- **THEN** the change is drawn at that measure

### Requirement: The score is laid out in systems that fit the available width
The application SHALL break the score into systems that fit the width it has,
stack them vertically, and let the user scroll through them. The layout SHALL be
deterministic: the same score at the same width SHALL produce the same
positions.

#### Scenario: A score longer than one line
- **WHEN** the score has more measures than fit across the tool
- **THEN** the measures continue on further systems below, each starting with
  the clef and key in force

#### Scenario: The tool is resized
- **WHEN** the tool's width changes
- **THEN** the score is laid out again for the new width

#### Scenario: Zoom
- **WHEN** the user chooses a larger or smaller size
- **THEN** the notation is drawn at that size and the systems break to suit it

### Requirement: What is not drawn is reported
The application SHALL report, in the tool, every part of the open score it has
not drawn. It SHALL NOT draw such content in a position that is wrong.

#### Scenario: Further parts and staves
- **WHEN** the score has more than one part, or a part has more than one staff
- **THEN** the tool names what it has not drawn

#### Scenario: Notation the renderer does not support
- **WHEN** the score contains notation this pass does not draw — beams, volta
  brackets, jump markings
- **THEN** the tool says so

#### Scenario: Import diagnostics are not hidden
- **WHEN** the importer reported diagnostics for the open score
- **THEN** the tool does not present the drawn notation as a complete rendering
  of the file

### Requirement: The drawn notation can be verified from a reference image
The capture harness SHALL be able to put an imported score in front of a tool
with nothing else on screen, so that what the renderer draws can be reviewed as
an image.

#### Scenario: A state that opens a score into a tool
- **WHEN** an approved window state names a score fixture and opens a tool
- **THEN** the score is imported and published, the tool draws it, and the
  import summary window is not shown

#### Scenario: A state that is about the summary
- **WHEN** an approved window state names a score fixture and opens no tool
- **THEN** the import summary window is shown exactly as before
