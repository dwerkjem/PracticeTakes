## ADDED Requirements

### Requirement: Every kept take is saved
A take that ends completed, stopped early or failed SHALL be saved to the
library's `Takes` folder. A cancelled take SHALL NOT be saved. Saving SHALL run
off the message thread and off the audio thread.

#### Scenario: A completed take
- **WHEN** a take reaches the end of the score
- **THEN** a folder for it appears in `Takes`

#### Scenario: A cancelled take
- **WHEN** a take is cancelled before anything was played
- **THEN** nothing is saved

### Requirement: A saved take holds everything its review needs
A saved take SHALL hold its manifest, the microphone audio when it used the
microphone, and a copy of the score file it was played against. Loading it
SHALL produce a take whose review is the same as the review of the take that
was saved, at the same settings.

#### Scenario: Round trip
- **WHEN** a take is saved and then loaded
- **THEN** its keyboard events, audio, start, scale, reference pitch, latencies
  and end state are those of the original, and judging it gives the same
  verdicts

#### Scenario: The song changed since
- **WHEN** the song a take was played against is edited after the take
- **THEN** the take is still judged against the score as it was when played

### Requirement: The take format is versioned
The manifest SHALL carry a format version. A reader SHALL read every version up
to its own, and SHALL refuse a newer major version, saying why, rather than
misread it.

#### Scenario: A take from a newer application
- **WHEN** a manifest names a major version newer than the reader's
- **THEN** the take is listed as unreadable by this version, and not loaded

### Requirement: A loaded take is not saved again
Reviewing a take from the library SHALL NOT write a second copy of it.

#### Scenario: Review twice
- **WHEN** the user reviews the same saved take twice
- **THEN** the `Takes` folder holds it once
