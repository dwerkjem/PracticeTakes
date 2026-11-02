## MODIFIED Requirements

### Requirement: What is not drawn is reported
The application SHALL report, in the tool, every part of the open score it has
not drawn. It SHALL NOT draw such content in a position that is wrong. Beaming
SHALL no longer be among what is reported; what remains of it SHALL be.

#### Scenario: Further parts and staves
- **WHEN** the score has more than one part, or a part has more than one staff
- **THEN** the tool names what it has not drawn

#### Scenario: Notation the renderer does not support
- **WHEN** the score contains notation this pass does not draw — volta
  brackets, jump markings
- **THEN** the tool says so

#### Scenario: Import diagnostics are not hidden
- **WHEN** the importer reported diagnostics for the open score
- **THEN** the tool does not present the drawn notation as a complete rendering
  of the file

#### Scenario: Beaming is no longer a gap
- **WHEN** a score holding runs of eighths is drawn
- **THEN** the layout does not report that eighths are drawn with flags rather
  than beamed

#### Scenario: A run of grace notes
- **WHEN** two or more grace notes fall together
- **THEN** they are drawn with flags and the layout reports that grace notes are
  not beamed
