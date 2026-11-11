# take-playback Specification

## Purpose
TBD - created by archiving change take-playback. Update Purpose after archive.

## Requirements

### Requirement: A take can be heard
Playing a take SHALL sound its microphone recording and its keyboard notes from
where it started, at its own tempo scale, with the sustain pedal as it was. The
score cursor SHALL follow it, and the clock SHALL stop where the take ended.
The metronome and the score's own playback SHALL be silent while it plays.

#### Scenario: Play a sung take
- **WHEN** the reader plays a microphone take from Take Review
- **THEN** the recording is heard, and the Score View's cursor moves with it

#### Scenario: Play a keyboard take with the pedal
- **WHEN** a keyboard take released a key while the pedal was down
- **THEN** that note sounds until the pedal was lifted

### Requirement: The clock is as it was afterwards
When take playback stops, the clock's tempo scale, loop, metronome and mix
SHALL be what they were before it began.

#### Scenario: Stop
- **WHEN** a take is stopped part way through
- **THEN** the clock is at the score's own tempo scale with its loop and
  metronome as before

### Requirement: A take's audio can be exported
Exporting a take SHALL write a stereo WAV file of its microphone recording and
its keyboard rendered through the piano, the length of the take.

#### Scenario: A keyboard take
- **WHEN** a take played only on the keyboard is exported
- **THEN** the file holds its notes as played through the piano

### Requirement: Playing keeps the audio-thread contract
The audio thread SHALL NOT allocate, lock, block or free memory to play a take.

#### Scenario: Under the sanitizer
- **WHEN** the audio callback renders a take under RealtimeSanitizer
- **THEN** no violation is reported
