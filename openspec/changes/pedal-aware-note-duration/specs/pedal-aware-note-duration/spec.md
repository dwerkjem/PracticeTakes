## ADDED Requirements

### Requirement: The sustain pedal's position is captured with its timestamp
The application SHALL record each sustain-pedal event with its position
(0–127), not only a down or up reading, and with a timestamp. Half-pedal values
SHALL be preserved.

#### Scenario: A half-pedal value
- **WHEN** the pedal reports a value between fully up and fully down
- **THEN** that value is recorded as sent, and whether it counts as down is
  decided by the configured threshold and not at capture time

#### Scenario: Down, then up
- **WHEN** the pedal is pressed fully and then released
- **THEN** both transitions are recorded with their timestamps, in order

### Requirement: The pedal threshold is a visible, persisted setting
The application SHALL let a user see and change the position at or above which
the pedal counts as down. It SHALL default to 64, SHALL survive a restart, and
SHALL NOT accept a value outside 1–127.

#### Scenario: Changing the threshold
- **WHEN** a user changes the threshold
- **THEN** notes released after the change are judged against the new value

#### Scenario: A file with no threshold
- **WHEN** settings written before this change are loaded
- **THEN** the threshold is 64 and the file is reported as migrated

#### Scenario: A value out of range in a file
- **WHEN** a settings file holds a threshold of 0 or above 127
- **THEN** it is clamped to the nearest valid value rather than rejected

### Requirement: Each note's sounding duration is derived from key and pedal
For every played note the application SHALL derive how long it sounded: until
the key came up if the pedal was up at that moment, otherwise until the pedal
next came up, and in either case no later than the same key being struck again.
A pedal pressed only after a key came up SHALL NOT extend that note.

#### Scenario: Pedal up at release
- **WHEN** a key is held for one second with the pedal up
- **THEN** the note's sounding duration is one second and it is reported as
  held by the finger

#### Scenario: Released early, held by the pedal
- **WHEN** a key is released after one second while the pedal is down, and the
  pedal comes up two seconds after that
- **THEN** the sounding duration is three seconds, the finger duration is one
  second, and the note is reported as held by the pedal

#### Scenario: Pedal pressed after release
- **WHEN** a key is released with the pedal up and the pedal is pressed
  afterwards
- **THEN** the note's sounding duration ends at the key release

#### Scenario: Re-striking a sustained note
- **WHEN** a key is struck again while its previous note is still held by the
  pedal
- **THEN** the previous note ends at the new strike and is reported as
  re-struck

#### Scenario: Pedal channels are independent
- **WHEN** the pedal is down on one MIDI channel and a note is released on
  another
- **THEN** that note is not treated as sustained

### Requirement: Duration is judged against the notated duration by sounding time
Given a notated duration, the application SHALL judge a note by its sounding
duration, not its finger duration, and SHALL report whether the finger or the
pedal was holding it.

#### Scenario: Released early but held by the pedal
- **WHEN** a note's finger duration is short of the notated duration but its
  sounding duration is not
- **THEN** it is not reported as short and is reported as pedal-held

#### Scenario: Released early with no pedal
- **WHEN** a note's key and sound both end short of the notated duration
- **THEN** it is reported as short

#### Scenario: Pedal-held beyond the notation
- **WHEN** a pedal-held note sounds longer than notated
- **THEN** it is not reported as long, and is still reported as pedal-held

#### Scenario: Finger-held beyond the notation
- **WHEN** a note is held by the finger beyond the notated duration
- **THEN** it is reported as long

### Requirement: A damaged event stream yields no verdict rather than a wrong one
The application SHALL NOT judge a note whose duration depends on state it can
no longer trust. It SHALL treat the pedal as unknown at the start of capture and
after any lost event, and SHALL end every held note as abandoned when events are
lost or the device disconnects.

#### Scenario: Events were lost
- **WHEN** the lost-event count advances while keys are held
- **THEN** each held note ends as abandoned and is not judged

#### Scenario: The device is unplugged mid-note
- **WHEN** the device disconnects while a key is held
- **THEN** that note ends as abandoned and no note remains held in any state

#### Scenario: Pedal state not yet known
- **WHEN** a note is released before any pedal event has been seen on its
  channel
- **THEN** it is reported with unknown pedal state and is not judged

### Requirement: Pedal use is reported, and not compared to markings the application does not read
The application SHALL report which notes were pedal-held. Because pedal markings
in a score are not imported, it SHALL say so wherever pedal use is reported and
SHALL NOT present a comparison against the score's pedalling.

#### Scenario: A score with pedal markings
- **WHEN** pedal use is shown for a score whose file contains pedal markings
- **THEN** the display states that the score's pedal markings are not read
