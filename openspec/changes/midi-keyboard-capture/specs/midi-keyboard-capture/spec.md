## ADDED Requirements

### Requirement: A MIDI input device can be selected and is remembered
The application SHALL list the MIDI input devices currently available and let a
user choose one of them as the input. The choice SHALL survive restarting the
application. The application SHALL function normally when no MIDI device is
selected and when none is available.

#### Scenario: Choosing a device
- **WHEN** a MIDI input device is available and a user selects it
- **THEN** the application reads events from that device

#### Scenario: The choice survives a restart
- **WHEN** a device has been selected and the application is restarted with that
  device still connected
- **THEN** the same device is in use without the user selecting it again

#### Scenario: No device at all
- **WHEN** no MIDI input device is available
- **THEN** the application runs normally, says that no device is available, and
  does not report an error condition

#### Scenario: The remembered device is not present
- **WHEN** the application starts and the remembered device is not connected
- **THEN** the application identifies the device it is waiting for by name
  rather than by an opaque identifier, and does not select a different device on
  the user's behalf

### Requirement: Note and pedal events reach the application on the message thread
The application SHALL deliver note-on, note-off and sustain-pedal events from the
selected device to a consumer on the message thread. Each event SHALL carry the
channel it arrived on, a timestamp, and — for a note event — the note number and
velocity. Pedal events SHALL state whether the pedal is down or up.

#### Scenario: A key is pressed and released
- **WHEN** a key on the selected device is pressed and then released
- **THEN** a note-on carrying that note number and its velocity is delivered,
  followed by a note-off for the same note

#### Scenario: A note-on with zero velocity
- **WHEN** the device sends a note-on whose velocity is zero
- **THEN** it is delivered as a note-off for that note, not as a note-on

#### Scenario: The sustain pedal
- **WHEN** the sustain pedal is pressed and then released
- **THEN** a pedal-down event and later a pedal-up event are delivered

#### Scenario: A partly depressed pedal
- **WHEN** the device sends a sustain-pedal value at or above the midpoint of
  its range
- **THEN** the pedal is reported as down, rather than only at the maximum value

#### Scenario: Messages the application does not use
- **WHEN** the device sends a message that is not a note or a sustain pedal
  change
- **THEN** no event is delivered for it and nothing is reported as an error

#### Scenario: A malformed message
- **WHEN** a message arrives that is shorter than its status byte requires
- **THEN** no event is delivered for it, and the application continues reading
  the device

#### Scenario: Several consumers
- **WHEN** more than one part of the application is reading MIDI events
- **THEN** each receives every event, and one that stops reading does not
  prevent another from receiving them

### Requirement: The MIDI callback is bounded
The code the MIDI driver calls SHALL NOT allocate, take a lock, block, perform
input or output, log, or touch the user interface. Events SHALL cross to the
message thread through preallocated storage.

#### Scenario: Events arrive faster than they are consumed
- **WHEN** events arrive faster than a consumer drains them, for long enough to
  exhaust that consumer's buffer
- **THEN** the driver callback neither blocks nor allocates, and the consumer is
  able to determine that events were lost rather than being given an incomplete
  picture it cannot distinguish from a complete one

#### Scenario: A message with a large payload
- **WHEN** a message arrives whose payload is too large to be held inline
- **THEN** the callback does not copy it, and delivers nothing for it

### Requirement: A device that disappears is reported and recovered from
The application SHALL detect that the selected device has become unavailable,
SHALL report that state, and SHALL resume reading from it without user action
when it becomes available again. Losing a device SHALL NOT terminate the
application or prevent the rest of it from working.

#### Scenario: The keyboard is unplugged mid-session
- **WHEN** the selected device is disconnected while the application is running
- **THEN** the application reports that it is not connected and continues to run

#### Scenario: The keyboard is plugged back in
- **WHEN** a disconnected device that is still the selected one becomes
  available again
- **THEN** the application resumes reading events from it without the user
  selecting it again

#### Scenario: Audio and MIDI are independent
- **WHEN** no audio input device is available, or the audio device is being
  recovered
- **THEN** MIDI events continue to be delivered, and the reverse also holds

### Requirement: The event stream is verifiable without a MIDI device
The translation from what a device sends into the application's own events SHALL
be testable without MIDI hardware and without a user-interface or device
framework. The behaviour of the buffer between the driver callback and a
consumer SHALL be testable the same way.

#### Scenario: The translation rules are asserted directly
- **WHEN** the test suite runs on a machine with no MIDI device
- **THEN** note-on, note-off, zero-velocity note-on, the pedal threshold,
  ignored messages and malformed messages are each exercised and asserted

#### Scenario: Buffer behaviour is asserted directly
- **WHEN** the test suite runs
- **THEN** event ordering, the behaviour when the buffer is exhausted, and the
  count of lost events are each exercised and asserted

### Requirement: What a timestamp means is stated
Each delivered event SHALL carry a timestamp. The application SHALL state what
those timestamps may be compared against, and SHALL NOT present a relationship
between MIDI timing and audio timing that has not been measured.

#### Scenario: Comparing two MIDI events
- **WHEN** two events from the same device are compared
- **THEN** their timestamps give the interval between them

#### Scenario: Comparing MIDI against audio
- **WHEN** a reader looks for the offset between MIDI timing and audio timing
- **THEN** the application states that this has not been established and names
  where it will be, rather than offering a number that was not measured
