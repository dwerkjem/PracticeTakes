## ADDED Requirements

### Requirement: The score model is independent of any rendering widget
The score model SHALL contain no engraving or presentation data: no coordinates,
sizes, fonts, page or system breaks, stem directions, beam groupings, or
widget-specific types. It SHALL be usable by code that links no GUI module, and
its headers SHALL NOT include `JuceHeader.h`, so that model logic can be
compiled into the unit-test target and exercised without a display.

#### Scenario: Model compiles into the test target
- **WHEN** the model headers and sources are added to `PracticeTakesTests`
- **THEN** they compile and link against the test target's module set without
  requiring `JuceHeader.h` or any GUI module the target does not already link

#### Scenario: Positional information is the renderer's responsibility
- **WHEN** a consumer needs the on-screen position, size, or stem direction of a
  note
- **THEN** the model provides no such field and the consumer computes it from
  musical content

### Requirement: The model describes a score with parts, measures, voices, and events
The model SHALL represent a score as a work-level root that owns an ordered list
of parts; each part SHALL own an ordered list of measures; each measure SHALL own
its voices; and each voice SHALL own its events. An event SHALL be a note, a
chord, or a rest, and SHALL carry its staff, its onset relative to the start of
its measure, and its duration.

#### Scenario: A multi-part score preserves part identity
- **WHEN** a score with four vocal parts is represented in the model
- **THEN** each part is addressable by a stable, unique, non-empty identifier
  taken from the source, and its display name and abbreviation are available
  separately from that identifier

#### Scenario: A chord is a single event
- **WHEN** three pitches sound simultaneously in one voice
- **THEN** the model contains one chord event carrying three pitches, not three
  overlapping note events

### Requirement: Every pitch retains both its spelling and its sounding number
A pitched note SHALL store its notated spelling — step, chromatic alteration,
and octave — and its sounding MIDI note number, and the two SHALL agree.

#### Scenario: Enharmonic spellings are distinguishable
- **WHEN** a score contains a C-sharp in one part and a D-flat in another at the
  same sounding pitch
- **THEN** the two events carry the same sounding number and different spellings

#### Scenario: Spelling and sounding number stay consistent
- **WHEN** any pitched note in the model is inspected
- **THEN** its sounding number equals the number implied by its step,
  alteration, and octave

### Requirement: The score has one musical time base shared by every part
The model SHALL express every duration and position in a single score-wide
musical time unit, independent of any per-part duration units used by the source
file. Measure `n` SHALL begin at the same absolute position in every part, and
SHALL have the same nominal duration in every part.

#### Scenario: Parts declaring different source resolutions align
- **WHEN** a score's parts declare different source duration resolutions and one
  part changes its resolution mid-score
- **THEN** all parts' measures share the same start positions and nominal
  durations in the model's time base

#### Scenario: Every event has an absolute position
- **WHEN** a consumer asks for the position of any event
- **THEN** it can obtain that event's absolute position from the start of the
  score without inspecting the source file

### Requirement: The model provides a tempo map that converts musical time to seconds
The model SHALL include a tempo map ordered by musical position, with no
duplicate positions and at least one entry. If the source declares no tempo, a
documented default SHALL be inserted. The map SHALL convert a musical position to
elapsed seconds and back.

#### Scenario: Tempo changes produce correct elapsed times
- **WHEN** a score changes tempo partway through
- **THEN** converting a position after the change to seconds accounts for the
  duration spent at each preceding tempo

#### Scenario: A score with no tempo marking is still playable
- **WHEN** a score declares no tempo anywhere
- **THEN** the tempo map contains the documented default and conversion succeeds

### Requirement: Model invariants hold for every constructed score
A score handed to a consumer SHALL satisfy all of the following: events within a
voice are in ascending order and do not overlap; every duration is non-negative
and only grace events have zero duration; a voice's events do not extend past its
measure's nominal duration except in a pickup measure, where they may be shorter;
every tie chain has exactly one start and one stop and links events of equal
pitch; part identifiers are unique and non-empty; and every diagnostic references
only parts, measures, and voices that exist in the score.

#### Scenario: Voice content exceeds its measure
- **WHEN** source content would place more musical time in a voice than the
  measure's time signature allows
- **THEN** the constructed score still satisfies the measure-duration invariant
  and the excess is reported as a diagnostic

#### Scenario: A tie has no matching end
- **WHEN** source content starts a tie that is never stopped
- **THEN** the constructed score contains no dangling tie link and the unmatched
  tie is reported as a diagnostic

#### Scenario: Parts disagree about measure count
- **WHEN** one part in the source has fewer measures than another
- **THEN** the constructed score has the same measure count in every part and
  the discrepancy is reported as a diagnostic

### Requirement: A score is immutable once shared and is safe to read from multiple threads
A score SHALL be fully constructed before it is made available to any consumer,
and SHALL NOT be mutated afterwards. Consumers SHALL obtain shared read-only
ownership, so that concurrent readers require no lock and no consumer can observe
a partially updated score.

#### Scenario: Two consumers read the same score concurrently
- **WHEN** two components hold the same score and read it from different threads
- **THEN** neither takes a lock and neither can modify what the other sees

#### Scenario: The current score is replaced while a consumer is reading
- **WHEN** the owner replaces the current score with a newly imported one
- **THEN** a consumer already holding the previous score continues to read a
  valid, unchanged score until it releases its own reference

### Requirement: The score model is never read from the real-time audio thread
No code reachable from an audio callback SHALL read, copy, reference-count, or
otherwise access the score model. Any audio-thread consumer of score data SHALL
receive a preallocated, bounded representation prepared away from the audio
thread and published without allocation or locking, consistent with the
application's existing audio-thread boundary.

#### Scenario: Playback needs score events
- **WHEN** a future playback feature needs note events during the audio callback
- **THEN** those events are read from a preallocated structure produced on
  another thread, and the audio callback holds no reference to the score model

#### Scenario: Reviewing a change that touches the audio callback
- **WHEN** a change adds score access to code reachable from an audio callback
- **THEN** the documented audio-thread boundary identifies it as a violation
