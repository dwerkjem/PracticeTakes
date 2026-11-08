## ADDED Requirements

### Requirement: A take records the microphone, the keyboard, or both
Starting a take SHALL record from every usable source: the microphone when an
input is open and not muted, and the MIDI keyboard when one is open. A take
SHALL NOT start with a score closed or no source usable. Recording SHALL begin
at the start of the count-in.

#### Scenario: A pianist who sings
- **WHEN** a take is started with the microphone and a keyboard both usable
- **THEN** the take holds the microphone's audio and the keyboard's events

#### Scenario: Nothing to record from
- **WHEN** the microphone is muted and no keyboard is open
- **THEN** Record is unavailable and says why

### Requirement: Every source shares the clock's timeline
Each recorded sample SHALL carry the practice clock's position exactly. Each
keyboard event SHALL be placed on the clock's timeline from its timestamp,
within one audio block. The take SHALL keep what is needed to convert a
position into a score tick, at the tempo scale it was recorded at.

#### Scenario: The downbeat in the audio
- **WHEN** a take counts in one bar and the performer plays on the downbeat
- **THEN** the audio sample at clock position zero is the downbeat's sample

### Requirement: A take's lifecycle is explicit
A take SHALL be ready, counting in, performing or paused while it runs. It
SHALL end as exactly one of these:
- completed, having reached the end of the piece;
- stopped early, kept with where it stopped;
- cancelled, and discarded;
- failed, with the reason, keeping what it recorded.

A take that did not reach the end SHALL NOT be reported as completed.

#### Scenario: Stopped at bar 14
- **WHEN** the performer stops a take in bar 14 of 32
- **THEN** the take is kept as stopped early at bar 14, not as completed

### Requirement: Nothing invalidates a take while it runs
While a take runs, the tempo scale, the loop and seeking SHALL be locked.
Opening another score SHALL end the take as failed, with that reason. Losing
every source SHALL end it as failed. An overflow of the recording SHALL end it
as failed rather than leave a gap.

#### Scenario: The tempo is locked
- **WHEN** the reader tries to change the tempo during a take
- **THEN** the tempo does not change

### Requirement: Recording keeps the audio-thread contract
Recording SHALL copy into preallocated, single-producer buffers from the audio
callback, without allocation, locks or blocking, and SHALL be driven through
the real callback under RealtimeSanitizer.

#### Scenario: Recording under the sanitizer
- **WHEN** the callback records a block
- **THEN** RealtimeSanitizer reports nothing
