## ADDED Requirements

### Requirement: The clock can be locked while a take records
The practice clock SHALL offer a lock. While locked, requests to seek, to
change the tempo scale, or to set or toggle the loop SHALL be ignored. Play,
pause, stop and the metronome's settings SHALL remain available.

#### Scenario: A tempo change during a take
- **WHEN** the clock is locked and the tempo scale is set to 50%
- **THEN** the scale is unchanged

### Requirement: The renderer records the input it is handed
While armed for a take, the clock's renderer SHALL copy each span of input it
plays, count-in included, into preallocated single-producer buffers, each span
stamped with its clock frame. With no input it SHALL record silence. A span
that does not fit SHALL be counted as an overflow and not recorded. The
renderer SHALL be handed the input the analysis tools are given: the tone or
the device, none while muted.

#### Scenario: Muted mid-take
- **WHEN** the microphone is muted while a take records
- **THEN** silence is recorded for those frames, and the timeline has no gap
