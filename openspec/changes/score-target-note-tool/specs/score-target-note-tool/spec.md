## ADDED Requirements

### Requirement: The current score is reachable from a tool
The application SHALL make the score it currently has open available to tools
through the shared-service bundle every tool is constructed with. A tool SHALL
NOT have to reach into the application shell, into another tool, or into the
import machinery to read it.

#### Scenario: A tool is opened while a score is open
- **WHEN** a score has been imported and a tool that reads the current score is
  opened
- **THEN** that tool has the current score available to it

#### Scenario: A tool is opened before any score
- **WHEN** no score has been imported and a tool that reads the current score is
  opened
- **THEN** the tool reads an absent score rather than an invalid one, and
  continues to work

#### Scenario: A score is opened while the tool is already open
- **WHEN** a score is imported while a tool that reads the current score is
  already open
- **THEN** the tool presents the newly opened score without the shell having to
  address the tool directly

#### Scenario: A failed import leaves an open tool alone
- **WHEN** a score is open in a tool and a subsequent import fails
- **THEN** the tool continues to present the score that was already open

#### Scenario: Adding the tool costs no shell change
- **WHEN** a tool that reads the current score is added to the application
- **THEN** it is registered the same way every other tool is, and no code that
  coordinates the window, the workspace, or the menus names it

### Requirement: A score a tool reads cannot be changed under it
A score made available to a tool SHALL be immutable and SHALL remain valid for
as long as the tool holds it. Opening a different score SHALL NOT invalidate,
mutate, or free a score a tool is still reading.

#### Scenario: A second score is opened while the first is being read
- **WHEN** a tool holds the current score and a different score is imported
- **THEN** the score the tool holds stays valid and unchanged until the tool
  itself takes up the new one

#### Scenario: No score reaches the audio thread
- **WHEN** a score is open
- **THEN** no score data is read or referenced on the audio thread

### Requirement: One target note derived from the score is presented
The application SHALL provide a tool that presents a single target note derived
from the current score. The target SHALL be the first pitched note of the score
in source order. Rests and grace notes SHALL NOT be chosen as the target.

#### Scenario: A score beginning with a note
- **WHEN** the current score's first part begins with a pitched note
- **THEN** that note is the target presented

#### Scenario: A score beginning with a rest
- **WHEN** the current score's first part begins with one or more rests
- **THEN** the first pitched note after them is the target presented

#### Scenario: A score beginning with a grace note
- **WHEN** the first pitched event of the score is a grace note
- **THEN** the first pitched note that is not a grace note is the target
  presented

#### Scenario: A score beginning with a chord
- **WHEN** the first pitched event of the score is a chord
- **THEN** one note of that chord is the target presented, and the presentation
  states that the target is part of a chord rather than presenting it as a
  single note

#### Scenario: A score with no pitched notes at all
- **WHEN** the current score contains no pitched note in any part
- **THEN** the tool says so, and does not present a target note

#### Scenario: No score is open
- **WHEN** no score has been imported
- **THEN** the tool says that no score is open and names the command that opens
  one, rather than presenting an empty panel

### Requirement: The target note is described well enough to be found and played
The presentation of a target note SHALL identify what the note is, where in the
score it is, and when it occurs. It SHALL state the note's notated spelling, its
sounding pitch, the part, staff and voice it belongs to, the measure number as
printed in the file, and its start time and duration in seconds.

#### Scenario: The note's name reflects the file's own spelling
- **WHEN** the file writes the target note as F-sharp
- **THEN** it is presented as F-sharp and not as G-flat, and when the file writes
  G-flat it is presented as G-flat

#### Scenario: The measure number is the one printed in the file
- **WHEN** the target note sits in a measure the file numbers "0" or "12a"
- **THEN** that number is what is presented, rather than a position in a list

#### Scenario: Timing comes from the score's tempo
- **WHEN** a score declares a tempo other than the default, or changes tempo
  during the score
- **THEN** the start time and duration presented reflect the tempo in force,
  rather than a fixed assumption

#### Scenario: A transposing part
- **WHEN** the target note belongs to a part whose written pitch differs from
  its sounding pitch
- **THEN** the sounding pitch is presented as the target, and the written
  spelling is presented alongside it rather than silently replacing it

#### Scenario: A score with no title
- **WHEN** the current score declares neither a work title nor a movement title
- **THEN** the presentation still identifies which score is open rather than
  showing a blank heading

### Requirement: The score model can render a notated pitch as a name
The score model SHALL provide the conversion from a notated pitch to a readable
note name, so that a consumer of the model does not have to derive it. The name
SHALL preserve the notated spelling, including accidentals the file wrote.

#### Scenario: A natural note
- **WHEN** a pitch spelled C in octave 4 is named
- **THEN** the name states the step and the octave

#### Scenario: Enharmonic spellings are distinguished
- **WHEN** two pitches sounding the same MIDI number are spelled differently
- **THEN** their names differ

#### Scenario: A double accidental
- **WHEN** a pitch carries a double flat or double sharp
- **THEN** both accidentals appear in the name

### Requirement: What the score model got wrong is written down
The change SHALL record, in the repository's documentation, what consuming the
score model from the application revealed: the friction met, the accessors that
were missing, and the assumptions that did not survive a consumer. The record
SHALL be specific enough for each entry to become an issue.

#### Scenario: A reader asks what the first consumer learned
- **WHEN** the documentation index is consulted for the score model
- **THEN** a findings document is listed, and each of its entries names what was
  wrong and what should happen about it
