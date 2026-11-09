## ADDED Requirements

### Requirement: The target can be chosen directly
The player SHALL be able to set the application's target note by clicking a key,
clicking a staff position with an accidental, typing a note name, using the
note they are singing or playing, stepping by a semitone or a step of the key,
or choosing a note in the score. Every way SHALL set the same shared target, and
every tool reading it SHALL show the new target.

#### Scenario: Typing a name
- **WHEN** the player types "Bb3" in the picker
- **THEN** the target is B♭3, and the tuner reads against it

#### Scenario: Singing it
- **WHEN** the player holds a steady A4 and chooses "Use the note I'm on"
- **THEN** the target is A4

### Requirement: An unsteady reading is not taken
"Use the note I'm on" SHALL be unavailable unless the reading has held one note,
within 30 cents of it, for 300 ms.

#### Scenario: A slide between notes
- **WHEN** the player is sliding between two notes
- **THEN** "Use the note I'm on" is unavailable

### Requirement: A chosen target survives a restart and, if asked, a new score
A chosen target SHALL be remembered across restarts. Opening a score SHALL
replace it with the score's target unless "Keep my target when a score opens"
is on.

#### Scenario: Keeping it
- **WHEN** the setting is on, the target is chosen as G4, and a score opens
- **THEN** the target stays G4
