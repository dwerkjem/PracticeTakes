## ADDED Requirements

### Requirement: A part's General MIDI program is kept
The importer SHALL keep each part's first `<midi-instrument>`'s
`<midi-program>`, 1-128 in the file, as the part's General MIDI program 0-127.
A program out of range, or none, SHALL leave it unset.

#### Scenario: A violin part
- **WHEN** a part's `<midi-program>` is 41
- **THEN** its program is 40, which General MIDI calls a violin
