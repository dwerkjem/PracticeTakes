# onset-timing Specification

## Purpose
Judging when each note of a take began against when the score says it should:
expected attacks, performed onsets from the keyboard and the microphone, their
alignment, and an early, on-time, late or absent verdict for each attack.
## Requirements
### Requirement: Expected attacks come from the score
The expected attacks of a part SHALL be one moment at each distinct onset tick
of its notes. A note tied from an earlier note SHALL NOT be an attack. Rests and
grace notes SHALL NOT be attacks. Each moment SHALL carry its notes.

#### Scenario: A tied note is not judged
- **WHEN** a part holds a half note tied to a quarter
- **THEN** only the half note's onset is an expected attack

### Requirement: Performed onsets from the keyboard and the microphone
Keyboard note-ons SHALL be onsets, with note-ons close together gathered into
one chord onset at the earliest of them. The microphone's audio SHALL be
searched for onsets, each with a strength.

#### Scenario: A synthetic attack is found in time
- **WHEN** the audio holds an attack at a known frame
- **THEN** an onset is found within 10 ms of it

### Requirement: Onsets are aligned to attacks in order
Onsets SHALL be matched to attacks in time order, each onset to at most one
attack, only within a window around the attack. Repeated notes of the same
pitch SHALL each be matched by their timing. An attack with no onset SHALL be
absent, and an onset matched to no attack SHALL be extra.

#### Scenario: Four repeated notes, one missed
- **WHEN** four repeated quarter notes are expected and the second is not played
- **THEN** the first, third and fourth are matched to their onsets and the second
  is absent

### Requirement: Each attack is judged in milliseconds and beats
A matched attack SHALL be judged early, on time or late, using a tolerance that
scales with the beat between a minimum and a maximum. Its error SHALL be given
in milliseconds and in beats, corrected for latency. A match with a weak onset
SHALL be marked uncertain. The results SHALL stay aligned through tempo
changes.

#### Scenario: Late by a tenth of a second at 120 bpm
- **WHEN** a note is played 100 ms after its beat at 120 bpm
- **THEN** it is late by 100 ms, or 0.2 beats

#### Scenario: Through a tempo change
- **WHEN** a score slows from 120 to 60 bpm and every note is played on time
- **THEN** every attack after the change is on time
