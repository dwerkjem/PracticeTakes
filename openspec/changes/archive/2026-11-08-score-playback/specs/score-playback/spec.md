## ADDED Requirements

### Requirement: The score sounds with the practice clock
When playback is on and the clock plays, every pitched note of the open score
SHALL sound from its onset to its end, tied notes as one, at the clock's tempo
scale. Notes SHALL follow loops, seeks and tempo changes as the metronome does,
SHALL be silent during the count-in, and SHALL be released whenever the clock
stops, pauses, seeks or wraps.

#### Scenario: A loop comes round
- **WHEN** a two-bar loop plays at 60%
- **THEN** its notes sound at 60% each time round, and none sounds on past the
  loop's end

### Requirement: Each hand can be muted or soloed
Each staff of each part SHALL be a track that can be muted or soloed. While any
track is soloed only soloed tracks sound; a muted track never sounds.

#### Scenario: Practising the right hand
- **WHEN** a pianist mutes the piano's right hand
- **THEN** the left hand and every other part play on, and the right hand is
  silent

### Requirement: Playback is off until it is turned on
Playback SHALL be off by default and its switch SHALL be remembered. Without
the SoundFont, playback SHALL be unavailable with the reason shown, and the
clock SHALL work as before.

#### Scenario: A first launch
- **WHEN** the application starts for the first time and a score plays
- **THEN** only the metronome sounds, until Play the score is turned on

### Requirement: A part plays its own instrument
A part SHALL play the General MIDI program its file names, or an acoustic grand
piano when it names none.

#### Scenario: A string part
- **WHEN** a part's `<midi-program>` is 41
- **THEN** it plays as a violin
