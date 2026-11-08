## ADDED Requirements

### Requirement: Each written note is right, wrong, missed or not judged
Every written note of the judged part SHALL have exactly one outcome:
- correct, when its pitch was played at its matched moment;
- wrong, when another pitch was played in its place, keeping that pitch;
- missed, when nothing was played for it;
- pitch not judged, when its moment was played but the evidence carries no
  pitch.

#### Scenario: A wrong note in a chord
- **WHEN** a C major chord is written and C, E and A are played
- **THEN** C and E are correct, and G is wrong, played as A

#### Scenario: A sung take before pitch segmentation
- **WHEN** a microphone onset is matched to a written note
- **THEN** the note's pitch is not judged, not counted wrong

### Requirement: Extra notes are reported once and never attached elsewhere
A played note with no written note at its moment, or at an onset no moment
matched, SHALL be reported as extra, with its frame and pitch if known. An extra
note SHALL NOT be attached to an unrelated written note.

#### Scenario: A stray key between two notes
- **WHEN** an extra key is struck between two correctly played notes
- **THEN** both notes are correct and the stray key is extra

### Requirement: Missed notes are reported once
A written note SHALL be reported missed at most once, and a missed repeated note
SHALL NOT shift the outcomes of the repeats around it.

#### Scenario: The second of four repeats is missed
- **WHEN** four repeated notes are written and the second is not played
- **THEN** the second is missed and the others are correct

### Requirement: Ambiguous pairings are marked
When a played pitch could be the wrong note for more than one written note, the
pairing SHALL be marked ambiguous.

#### Scenario: Equidistant
- **WHEN** C and E are written, and D and a correct E are played
- **THEN** C is wrong, played as D, without ambiguity, because E's own pitch was
  played
