# MusicXML Import

## Purpose

Define acceptance of `.musicxml`, `.xml`, and `.mxl` input, the documented supported subset, structural validation, and diagnostics that identify where and why content was rejected or dropped.

## Requirements

### Requirement: The importer accepts uncompressed and compressed MusicXML files
The importer SHALL accept MusicXML documents with the `.musicxml` and `.xml`
extensions and compressed `.mxl` containers. For a compressed container it SHALL
determine the root score document from the container's own manifest rather than
assuming a filename or entry order.

#### Scenario: An uncompressed score imports
- **WHEN** a valid partwise MusicXML document with a `.musicxml` or `.xml`
  extension is imported
- **THEN** the import succeeds and produces a score

#### Scenario: A compressed container names its root document
- **WHEN** a `.mxl` container holds several entries and its manifest names one
  of them as the root score
- **THEN** the importer reads the named root, not the first or largest entry

#### Scenario: A compressed container has no usable manifest
- **WHEN** a `.mxl` container has no manifest, or its manifest names an entry
  the container does not hold
- **THEN** the import fails with a status distinguishing it from a valid file
  and an error message naming the missing root document

### Requirement: The supported MusicXML subset is documented and enforced
The repository SHALL contain a document defining the MusicXML subset the
importer supports, listing constructs that are imported, constructs that are
recognised but deliberately dropped, and document types that are rejected. The
importer's behaviour SHALL match that document.

#### Scenario: A contributor needs to know whether a construct is supported
- **WHEN** a contributor or user asks whether a notation construct is imported
- **THEN** the subset document answers it without reading the importer source

#### Scenario: A rejected document type is identified as such
- **WHEN** a timewise MusicXML document, or an XML document that is not
  MusicXML, is imported
- **THEN** the import fails with a status that distinguishes an unsupported
  document type from malformed input

### Requirement: The importer converts supported notation into the score model
The importer SHALL convert parts including multi-staff parts, measures, notes,
rests, chords, voices, ties, pitches with their spelling, instrument
transpositions, clef, key and time signatures including mid-score changes,
tempo markings, lyric syllables with their verse number and syllabic position,
and dynamics, into the normalized score model.

#### Scenario: A vocal score with lyrics imports
- **WHEN** a multi-part vocal score with several lyric verses is imported
- **THEN** each part's notes carry their syllables with the correct verse number
  and syllabic position, and no syllable is attached to the wrong note

#### Scenario: A piano score with multiple voices imports
- **WHEN** a piano score using two staves and multiple voices per staff is
  imported
- **THEN** each event appears in the correct staff and voice at the correct
  position within its measure

#### Scenario: Voice content is written out of time order
- **WHEN** a measure writes its voices using cursor moves that revisit earlier
  positions
- **THEN** each voice's events are ordered by position in the model and no voice
  is displaced in time

#### Scenario: A measure holds more music than its time signature
- **WHEN** a measure's content is longer than the prevailing time signature
  allows, as in an edition using a mensuration sign rather than a bar length
- **THEN** the measure is widened to hold all of its music, a diagnostic
  reports the disagreement, and no note is shortened or dropped

#### Scenario: A pickup measure does not fill its time signature
- **WHEN** a score begins with a pickup measure shorter than the prevailing time
  signature
- **THEN** the measure is imported as a pickup without being reported as an
  error, and subsequent measures start at the correct positions

#### Scenario: A part is written for a transposing instrument
- **WHEN** a part declares a transposition, whether at its start or partway
  through the score
- **THEN** each of its notes carries both the pitch as written and the pitch as
  sounded, and the sounding pitch is spelled correctly rather than merely
  sounding correct

#### Scenario: Ties are distinguished from slurs
- **WHEN** a score contains both a tie between two notes of the same pitch and a
  slur across notes of different pitches
- **THEN** only the tie produces a tie link in the model

### Requirement: Unsupported content is reported without failing the import
Content outside the supported subset SHALL NOT cause a failure. Recognised but
unsupported constructs SHALL be dropped and reported as diagnostics.
Unrecognised elements SHALL be reported in aggregate by element name rather than
once per occurrence, so a file rich in unsupported markup does not produce an
unusable volume of diagnostics.

#### Scenario: A score contains unsupported notations
- **WHEN** a score contains articulations, ornaments, or chord symbols that the
  subset excludes
- **THEN** the import succeeds, the score contains the supported content, and a
  diagnostic reports what was dropped

#### Scenario: An export contains many unrecognised elements
- **WHEN** a file contains hundreds of occurrences of the same unrecognised
  element
- **THEN** the diagnostics contain a single summarised entry for that element
  name, including how many occurrences were seen

### Requirement: Invalid input fails with a specific, actionable reason
The importer SHALL distinguish between a missing file, an unreadable file, a
file exceeding the documented size limits, a file that is not MusicXML,
malformed XML, an inconsistent compressed container, an unsupported document
type, and a structurally invalid score. The failure SHALL carry a human-readable
message. No score SHALL be produced on failure, and no partially constructed
score SHALL be observable.

#### Scenario: The XML is malformed
- **WHEN** a file's XML is truncated or syntactically invalid
- **THEN** the import fails with the malformed-XML status, an explanatory
  message, and no score

#### Scenario: The file is not MusicXML at all
- **WHEN** a well-formed XML file with an unrelated root element is imported
- **THEN** the import fails with a status identifying it as not MusicXML, rather
  than reporting malformed XML

#### Scenario: A compressed file expands beyond the permitted size
- **WHEN** a `.mxl` container's entries expand beyond the documented
  uncompressed-size or expansion-ratio limit
- **THEN** the import fails with the size-limit status before the expansion is
  completed

#### Scenario: A failed import leaves no partial result
- **WHEN** an import fails for any reason
- **THEN** the result carries no score

#### Scenario: A document yields no music at all
- **WHEN** a partwise document parses but produces no notes and no rests
- **THEN** the import fails as structurally invalid, because a document with no
  events is a failed parse rather than an empty score

#### Scenario: A document contains only rests
- **WHEN** a partwise document produces rests but no notes
- **THEN** the import succeeds, because a passage of rests is valid notation

### Requirement: Diagnostics identify where in the score a problem occurred
Every diagnostic SHALL carry a severity and, where the problem is attributable to
a location, the part identifier, the measure number as printed in the source, the
voice, and the position within the measure. Measure numbers SHALL be reported as
the source prints them, including non-numeric forms.

#### Scenario: A problem is attributable to one measure
- **WHEN** a single measure in one part contains content that must be dropped or
  corrected
- **THEN** the diagnostic names that part and that measure's printed number

#### Scenario: A score uses non-numeric measure numbers
- **WHEN** a score numbers a pickup measure as zero or splits a bar into
  suffixed numbers
- **THEN** diagnostics report those measure numbers exactly as the source prints
  them

#### Scenario: A problem is not attributable to a location
- **WHEN** a problem concerns the document as a whole
- **THEN** the diagnostic carries no location rather than a misleading one

### Requirement: Import performs no network access and runs off the message thread
Importing SHALL NOT retrieve any external resource, including document type
definitions or schemas referenced by the file. Import SHALL be performed away
from the message thread so that a large file cannot block the user interface,
and away from the audio thread.

#### Scenario: A file references an external document type definition
- **WHEN** a score declares a document type referencing an external URL
- **THEN** the importer resolves nothing over the network and the import
  completes offline

#### Scenario: A large score is imported
- **WHEN** a score large enough to take noticeable time is imported
- **THEN** the user interface remains responsive during the import

### Requirement: Representative real-world scores import consistently
The test suite SHALL import a corpus of real notation-program exports covering at
least one multi-part vocal score and one piano score, and SHALL assert their
imported structure. Test coverage SHALL also include the notation and failure
cases the subset document describes.

#### Scenario: The corpus is imported in CI
- **WHEN** the unit-test suite runs
- **THEN** every score in the corpus imports successfully and its part count,
  measure count, and total musical length match the recorded expectations

#### Scenario: An importer change alters a corpus score's structure
- **WHEN** a change to the importer alters how a corpus score is converted
- **THEN** the corpus test fails rather than silently accepting the new
  structure
