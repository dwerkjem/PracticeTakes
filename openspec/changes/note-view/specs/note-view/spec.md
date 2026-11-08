## ADDED Requirements

### Requirement: The note being sung lights its key
The Note View SHALL light the key of the note being sung or played, shaded by
how far off it is, with an offset that agrees with the tuner's cents, and SHALL
mark the target's key so that the two are told apart at a glance in both
themes.

#### Scenario: Singing a little flat
- **WHEN** the singer holds A4 twelve cents flat and the target is A4
- **THEN** the A4 key is lit amber-green and marked as the target

### Requirement: A slide is drawn as a slide
The trail SHALL draw the last three seconds of pitch continuously across the
keys, breaking only where there was no pitch.

#### Scenario: A scoop
- **WHEN** the singer slides from G4 up to A4
- **THEN** the trail is one line from G4 to A4, not two points

### Requirement: Keys held on a MIDI keyboard light
Keys held on a connected MIDI keyboard SHALL light while held.

#### Scenario: A chord
- **WHEN** C4, E4 and G4 are held
- **THEN** those three keys are lit
