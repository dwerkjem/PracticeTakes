## ADDED Requirements

### Requirement: One authoritative position
The application SHALL have one practice clock, shared by every tool. Its
position SHALL be counted in frames of the audio callback that captures the
microphone, so that a captured sample and the clock's position at the same
frame denote the same instant. The clock SHALL publish, with each block, the
pair of its frame position and the high-resolution time of that block, so an
event timestamped on another thread can be placed on the clock's timeline. The
clock SHALL run whether or not an output device is open, and SHALL run from the
synthetic-input timer when no input device is open.

#### Scenario: Two tools see the same position
- **WHEN** the clock is playing and two tools read its position in the same
  message-thread tick
- **THEN** they read the same frame, tick and seconds

#### Scenario: The clock runs without an output device
- **WHEN** the input device is open and no output device is
- **THEN** the clock plays, its position advances, and the metronome is silent

### Requirement: Play, pause, stop and seek
The clock SHALL support playing, pausing, stopping and seeking to a score tick.
Pause SHALL hold the position. Stop SHALL return it to the start of the loop
region, or to the start of the score when no loop is set. Seek SHALL move the
position to the frame of the requested tick at the current tempo scale, and
SHALL be reported to listeners as a discrete event.

#### Scenario: Pause holds the position
- **WHEN** the clock is paused and resumed
- **THEN** it resumes from the frame at which it paused

#### Scenario: A seek is reported once
- **WHEN** the user seeks to bar 9
- **THEN** listeners receive exactly one seek event, and the position is the
  first tick of bar 9

### Requirement: The score's tempo map and meter are followed exactly
The clock SHALL convert between score ticks and frames using the score's tempo
map, exactly across tempo changes, and SHALL take beats per bar from the
score's time signatures. The audio thread SHALL NOT read the score model: the
message thread SHALL flatten the tempo map and meter into preallocated segments
that the audio thread reads.

#### Scenario: A tempo change mid-piece
- **WHEN** a score changes from 60 to 120 beats per minute at bar 5 and the
  clock plays through it
- **THEN** the metronome's beats fall every second before bar 5 and every half
  second after it, to the frame

### Requirement: A count-in before the take
Starting with a count-in SHALL begin the clock a whole number of bars before
the start point, at that point's tempo and meter, sounding the metronome
throughout. During the count-in the position SHALL be negative, and listeners
SHALL be told the clock is counting in. The count-in length SHALL be
configurable from zero to four bars.

#### Scenario: One bar of 3/4 before bar 1
- **WHEN** a piece in 3/4 is started with a one-bar count-in
- **THEN** three clicks sound, the first accented, and the third is followed
  one beat later by position zero

### Requirement: A metronome locked to the position
The metronome SHALL click on every beat while the clock plays, with the
downbeat of each bar accented, at every tempo scale. Each click SHALL start on
the frame of its beat. The clicks SHALL be generated at construction, and the
audio thread SHALL NOT allocate, lock or perform I/O to play them. The
metronome's volume SHALL be adjustable, and the metronome SHALL be able to be
turned off without stopping the clock. Muting the microphone SHALL NOT mute the
metronome.

#### Scenario: The click lands on the beat at half speed
- **WHEN** the tempo scale is 50% and the clock plays four beats
- **THEN** each click starts on the frame of its beat, computed at the halved
  tempo

#### Scenario: Muted microphone, audible metronome
- **WHEN** the microphone is muted while the clock plays
- **THEN** the metronome is still heard

### Requirement: Tempo scale
The clock SHALL play at a scale of the score's tempo between 25% and 200%.
Changing the scale while playing SHALL keep the current score position and
continue from it at the new tempo, without a jump.

#### Scenario: Slowing down mid-phrase
- **WHEN** the scale changes from 100% to 70% at beat 3 of bar 6
- **THEN** the next click is beat 4 of bar 6, and it arrives after the 70%
  beat interval

### Requirement: A loop without drift
The clock SHALL loop one region, set in score ticks. The loop's bounds SHALL be
converted to whole frames once, when the loop is set or the scale changes, and
each wrap SHALL move the position back by exactly the loop's length in frames.
Each wrap SHALL be reported to listeners as a discrete event.

#### Scenario: The ten-thousandth repetition starts on time
- **WHEN** a loop of bars 3–5 of a piece in 7/8 with a tempo change inside it
  wraps 10,000 times at 44,100 Hz
- **THEN** every repetition starts on the same frame offset as the first

### Requirement: Position notifications on the message thread
The clock SHALL notify its listeners on the message thread at display rate
with the current position in ticks, score seconds and frames, the clock's
state, whether it is counting in, and the tempo scale. Seeks, loop wraps and
reaching the end of the piece SHALL each be delivered once, as discrete events,
on the next notification.

#### Scenario: A wrap is never missed or doubled
- **WHEN** a loop wraps between two notifications
- **THEN** the next notification carries exactly one wrap event

### Requirement: The output path keeps the audio-thread contract
Rendering the clock SHALL happen inside the existing audio callback, after its
output is cleared, under the same contract: no allocation, no lock, no blocking
call, no I/O, bounded work, `noexcept` and annotated non-blocking. The
renderer SHALL be published to the callback atomically, and SHALL NOT be
destroyed while a callback may be running it. If the device cannot be opened
with outputs, it SHALL be opened input-only, as before, and the metronome SHALL
report itself unavailable.

#### Scenario: No output device, capture unaffected
- **WHEN** opening the device with outputs fails
- **THEN** the input opens as it did before this change, every analysis tool
  works, and Settings says the metronome is unavailable
