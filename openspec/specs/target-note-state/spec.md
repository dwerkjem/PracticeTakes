# target-note-state Specification

## Purpose
The note the player is aiming at, held for the whole application, chosen
only from a part that plays one note at a time, and read by the tuner.
## Requirements
### Requirement: One target note for the whole application
The application SHALL hold one current target note, readable by every tool,
or the reason there is none.

#### Scenario: A song for voice and piano is opened
- **WHEN** the sample song is opened
- **THEN** the target is the voice's first note, and the tuner shows it

### Requirement: A target is one note at a time
A target SHALL be chosen only from a part that plays one note at a time. When
every part plays chords or overlapping voices, there SHALL be no target, and
the reason SHALL say so.

#### Scenario: A piano piece
- **WHEN** a piano solo is opened
- **THEN** there is no target, and the tuner says every part plays more than
  one note at a time

### Requirement: The tuner reads against the target
The tuner SHALL show the target, and while it hears a pitch, whether the player
is on it, how many cents sharp or flat within half a semitone, or how many
semitones above or below.

#### Scenario: Singing a little flat
- **WHEN** the target is B3 and the player sings 12 cents under it
- **THEN** the tuner says "Target B3 · 12 ¢ flat"

### Requirement: A workspace naming the old tool still opens
A workspace naming `target-note`, `score-target` or `targetNote` SHALL open a
tuner in its place.

#### Scenario: An old workspace
- **WHEN** a workspace saved with the Target Note tool docked is opened
- **THEN** a tuner is docked where it was
