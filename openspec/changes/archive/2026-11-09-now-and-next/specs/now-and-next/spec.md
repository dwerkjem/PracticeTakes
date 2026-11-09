## ADDED Requirements

### Requirement: The current note changes when it is heard
Now & Next SHALL show as current the item of the followed part sounding at the
clock's audible position: the clock's frame, less the output latency.

#### Scenario: Playing through a bar
- **WHEN** the clock plays from G4 into A4
- **THEN** the current note changes to A4 when A4 is heard, and its time bar
  starts full

### Requirement: Rests and ties are not new attacks
A rest SHALL be shown as a rest, and a note tied over SHALL be shown once,
lasting to the end of its tie.

#### Scenario: A tied half note
- **WHEN** a quarter G4 is tied to a half G4
- **THEN** one G4 lasting three beats is shown, marked tied, and no second G4
  appears

### Requirement: The next notes are readable
Now & Next SHALL show the next items in a strip that changes only when the
current item does. It SHALL mark a repeated note and a phrase start.

#### Scenario: A phrase after a rest
- **WHEN** a quarter rest separates two phrases
- **THEN** the strip shows the rest, then a breath mark before the next phrase's
  first note

### Requirement: High contrast for live use
Now & Next SHALL offer a high-contrast mode, saved with the tool.

#### Scenario: Turning it on
- **WHEN** High contrast is chosen from the tool's menu
- **THEN** the display turns black, white and yellow, and stays so after a
  restart

### Requirement: The target follows the score as it plays
While the clock plays a score, and the target came from that score, the shared
target SHALL follow the note being sung in the target's part. A target the
player chose SHALL NOT be replaced. A setting SHALL turn this off.

#### Scenario: The tuner following a song
- **WHEN** the sample plays and the target came from the score
- **THEN** the tuner's target changes from E4 to F#4 as the voice moves on
