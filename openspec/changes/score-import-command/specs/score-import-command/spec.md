## ADDED Requirements

### Requirement: A user can open a MusicXML score from the application
The application SHALL provide a File menu containing an Open Score command. The
command SHALL present a file chooser offering the file types the importer
accepts. Choosing a file SHALL import it; dismissing the chooser SHALL leave the
application unchanged.

#### Scenario: The command is available from a menu
- **WHEN** the application is running
- **THEN** a File menu is present and offers an Open Score command

#### Scenario: A user chooses a score file
- **WHEN** a user invokes Open Score and selects a MusicXML file
- **THEN** the file is imported and the outcome is presented

#### Scenario: A user dismisses the chooser
- **WHEN** a user invokes Open Score and cancels without choosing a file
- **THEN** no import is attempted and any previously opened score remains
  current

#### Scenario: The chooser offers the accepted file types
- **WHEN** the file chooser is shown
- **THEN** it offers the uncompressed and compressed MusicXML extensions the
  importer accepts, and does not restrict the user to a single one of them

### Requirement: Import runs off the message thread
Importing SHALL be performed on a thread other than the message thread, and the
finished score SHALL be delivered back to the message thread. The user interface
SHALL remain responsive for the duration of an import. No import work SHALL run
on the audio thread.

#### Scenario: A large score is imported
- **WHEN** a score large enough to take noticeable time is imported
- **THEN** the user interface continues to redraw and respond throughout, and
  the result appears when the import finishes

#### Scenario: The window closes while an import is running
- **WHEN** the component that launched an import is destroyed before the import
  finishes
- **THEN** the application shuts down without crashing and without delivering a
  result to a destroyed component

#### Scenario: A second import is requested while one is running
- **WHEN** a user invokes Open Score while an import is already running
- **THEN** the application does not start two competing imports, and the user is
  not left without feedback about which file is being read

### Requirement: A successful import is summarised for the user
On a successful import the application SHALL present a summary of the score
identifying what was read: the work and movement titles, the composer, the
software the file was written by, the number of parts with each part's name and
staff count, the number of measures, the total musical length, and the tempo in
force at the start of the score.

#### Scenario: A vocal score is opened
- **WHEN** a multi-part vocal score imports successfully
- **THEN** the summary names each part and reports the measure count and total
  length

#### Scenario: The file records its encoding software
- **WHEN** a score declares the program that exported it
- **THEN** the summary reports that program, because it is the first thing
  needed when a file misbehaves

#### Scenario: The score has no metadata
- **WHEN** a score declares no title, composer, or encoding software
- **THEN** the summary omits those fields rather than showing empty labels, and
  still reports the structural counts

### Requirement: Import diagnostics are shown to the user
The application SHALL present the diagnostics an import produced. Each
diagnostic SHALL be shown with its severity, its message, and its musical
location where it has one. A diagnostic reported for many occurrences SHALL show
its occurrence count rather than being repeated. An import that produced no
diagnostics SHALL say so explicitly.

#### Scenario: A score contains unsupported content
- **WHEN** a score imports successfully but content was dropped or repaired
- **THEN** the diagnostics are listed with their severities and, where they have
  one, the part and the measure number as printed in the source

#### Scenario: A diagnostic covers many occurrences
- **WHEN** a diagnostic reports an element seen many times
- **THEN** it appears once with its occurrence count

#### Scenario: A score imports cleanly
- **WHEN** a score imports with no diagnostics at all
- **THEN** the summary states that nothing was dropped or repaired, rather than
  showing an empty list that could be mistaken for a failure to report

### Requirement: A failed import reports why
When an import fails the application SHALL present the reason, distinguishing
the importer's failure statuses from one another rather than reporting a generic
error. A failed import SHALL leave any previously opened score current.

#### Scenario: A malformed file is opened
- **WHEN** a file whose XML is malformed is chosen
- **THEN** the application reports that the file is not valid XML, with the
  importer's message

#### Scenario: A file that is not MusicXML is opened
- **WHEN** a well-formed XML file that is not a MusicXML score is chosen
- **THEN** the application reports that it is not a MusicXML score, and does not
  report it as malformed

#### Scenario: A compressed container is inconsistent
- **WHEN** a compressed container without a usable manifest is chosen
- **THEN** the application reports the container problem specifically, including
  the entry that could not be found

#### Scenario: A previously opened score survives a failed import
- **WHEN** a score is open and a subsequent import fails
- **THEN** the previously opened score remains the current score

### Requirement: The application owns the current score
The application shell SHALL hold the most recently imported score as the current
score, as a shared immutable value that any number of readers may hold without a
lock. Replacing the current score SHALL NOT invalidate a reference another reader
is already using.

#### Scenario: A second score is opened
- **WHEN** a score is open and a different score is imported successfully
- **THEN** the newly imported score becomes the current score

#### Scenario: A reader holds a score that is replaced
- **WHEN** a reader holds a reference to the current score and a different score
  is imported
- **THEN** the reader's score remains valid and unchanged for as long as it is
  held

#### Scenario: No score has been opened
- **WHEN** the application starts and no score has been imported
- **THEN** there is no current score, and this is distinguishable from an empty
  score
